/*
 * lib/szip.c
 *
 * This is a seekable zip file, the format of which is based on
 * code available at https://github.com/glandium/faulty.lib
 *
 * Copyright: Mozilla
 * Author: Dhaval Giani <dgiani@mozilla.com>
 *
 * Based on code written by Mike Hommey <glandium@mozilla.com> as
 * part of faulty.lib .
 *
 * This code is available under the MPL v2.0 which is explicitly
 * compatible with GPL v2.
 */

#include <linux/zlib.h>
#include <linux/szip.h>
#include <linux/vmalloc.h>

#include <linux/string.h>

#define SZIP_MAGIC 0x7a5a6553

static int szip_decompress_seekable_chunk(struct szip_struct *szip,
		char *output, size_t offset, size_t chunk, size_t length)
{
	int is_last_chunk = (chunk == szip->nr_chunks - 1);
	size_t chunk_len = is_last_chunk ? szip->last_chunk_size
						: szip->chunk_size;
	z_stream zstream;
	int ret = 0;
	int flush;
	int success;

	memset(&zstream, 0, sizeof(zstream));

	if (length == 0 || length > chunk_len)
		length = chunk_len;

	if (is_last_chunk)
		zstream.avail_in = szip->total_size;
	else
		zstream.avail_in = szip->offset_table[chunk + 1]
					- szip->offset_table[chunk];

	zstream.next_in = szip->buffer + offset;
	zstream.avail_out = length;
	zstream.next_out = output;
	if (!szip->workspace)
		szip->workspace = vzalloc(zlib_inflate_workspacesize());
	zstream.workspace = szip->workspace;
	if (!zstream.workspace) {
		ret = -ENOMEM;
		goto out;
	}

	/* Decompress Chunk */
	/* **TODO: Correct return value for bad zlib format** */
	if (zlib_inflateInit2(&zstream, (int) szip->window_bits) != Z_OK) {
		ret = -EMEDIUMTYPE;
		goto out;
	}

	/* We don't have dictionary logic yet */
	if (length == chunk_len) {
		flush = Z_FINISH;
		success = Z_STREAM_END;
	} else {
		flush = Z_SYNC_FLUSH;
		success = Z_OK;
	}

	ret = zlib_inflate(&zstream, flush);

	/*
	 * Ignore Z_BUF_ERROR for now. I am sure it will bite us
	 * later on
	 */
	if (ret != success && ret != Z_BUF_ERROR) {
		ret = -EMEDIUMTYPE;
		goto out;
	}

	if (zlib_inflateEnd(&zstream) != Z_OK) {
		ret = -EMEDIUMTYPE;
		goto out;
	}

	ret = 0;
out:
	return ret;
}

int szip_seekable_decompress(struct szip_struct *szip, size_t start,
				size_t end, char *output, size_t length)
{
	int ret = 0;
	size_t chunk_nr;

	for (chunk_nr = start; chunk_nr <= end; chunk_nr++) {
		size_t len = min_t(size_t, length, szip->chunk_size);
		size_t offset = szip->offset_table[chunk_nr]
					- szip->offset_table[start];
		ret = szip_decompress_seekable_chunk(szip, output,
						offset, chunk_nr, len);
		if (ret)
			goto out;


		output += len;
		length -= len;
	}
out:
	return ret;
}

int szip_decompress(struct szip_struct *szip, char *output, size_t length)
{
	size_t header_size = 20;
	char *buf;
	buf = szip->buffer + header_size;
	szip_init_offset_table(szip, buf);
	szip->buffer = szip->buffer + szip->offset_table[0];
	return szip_seekable_decompress(szip, 0,
			szip->nr_chunks, output, length);
}

size_t szip_uncompressed_size(struct szip_struct *szip)
{
	return (szip->chunk_size * (szip->nr_chunks - 1))
					+ szip->last_chunk_size;
}

void szip_init_offset_table(struct szip_struct *szip, char *buf)
{
	szip->offset_table = vzalloc(sizeof(unsigned) * szip->nr_chunks);
	memcpy(szip->offset_table, buf , sizeof(unsigned) * szip->nr_chunks);
}

size_t szip_offset_table_size(struct szip_struct *szip)
{
	return sizeof(unsigned) * szip->nr_chunks;
}

/*
 * Initialize a szip structure looking at the buffer
 * Returns 0 on success
 *
 * XX: Fixup the return values. No magic numbers!
 */
int szip_init(struct szip_struct *szip, char *buf)
{
	char *ptr = buf;

	szip->buffer = buf;
	/* We don't implement it yet */
	szip->dictionary = NULL;

	memcpy(&szip->magic, ptr, sizeof(szip->magic));
	/* No need to decode the structure if its not an szip buffer */
	if (szip->magic != SZIP_MAGIC)
		return -1;

	ptr += sizeof(szip->magic);
	memcpy(&szip->total_size, ptr, sizeof(szip->total_size));

	ptr += sizeof(szip->total_size);
	memcpy(&szip->chunk_size, ptr, sizeof(szip->chunk_size));
	/*
	 * If chunk_size is not a multiple of PAGE_SIZE, its malformed
	 * No need to decode further
	 */
	if ((szip->chunk_size % PAGE_SIZE) ||
			(szip->chunk_size > 8 * PAGE_SIZE))
		return -2;

	ptr += sizeof(szip->chunk_size);
	memcpy(&szip->dict_size, ptr, sizeof(szip->dict_size));
	if (szip->dict_size)
		return -EINVAL;

	ptr += sizeof(szip->dict_size);
	memcpy(&szip->nr_chunks, ptr, sizeof(szip->nr_chunks));
	/* If there are no chunks, no need to decode further*/
	if (szip->nr_chunks < 1)
		return -3;

	ptr += sizeof(szip->nr_chunks);
	memcpy(&szip->last_chunk_size, ptr, sizeof(szip->last_chunk_size));
	/* Last Chunk Size is never 0 or greater than chunk size*/
	if (!szip->last_chunk_size || szip->last_chunk_size > szip->chunk_size)
		return -4;

	ptr += sizeof(szip->last_chunk_size);
	memcpy(&szip->window_bits, ptr, sizeof(szip->window_bits));

	ptr += sizeof(szip->window_bits);
	memcpy(&szip->filter, ptr, sizeof(szip->filter));
	if (szip->filter)
		return -EINVAL;

	ptr += sizeof(szip->filter);

	szip->workspace = NULL;

	return 0;
}

/*
 * We just allocated memory for the offset table, nothing else
 */
void free_szip(struct szip_struct *szip)
{
	vfree(szip->offset_table);
	vfree(szip->workspace);
}

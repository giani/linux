#ifndef __SZIP_H
#define __SZIP_H

#include <linux/zlib.h>
#include <linux/types.h>

#define SZIP_HEADER_SIZE (20)

struct szip_struct {
	u32 magic;
	u32 total_size;
	u16 chunk_size;
	u16 dict_size;
	u32 nr_chunks;
	u16 last_chunk_size;
	signed char window_bits;
	signed char filter;
	unsigned *offset_table;
	unsigned *dictionary;
	char *buffer;
	void *workspace;
};

extern int szip_decompress(struct szip_struct *, char *, size_t);
extern int szip_seekable_decompress(struct szip_struct *, size_t,
						size_t, char *, size_t);
extern size_t szip_uncompressed_size(struct szip_struct *);
extern int szip_init(struct szip_struct *, char *);
extern void szip_init_offset_table(struct szip_struct *szip, char *buf);
extern size_t szip_offset_table_size(struct szip_struct *szip);

#endif

// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc
 *
 * Author: Dhaval Giani (AMD) <dhaval@gianis.ca>
 *
 * Basic functional tests for EEVDF - Invariants
 *
 * Use the debugfs triggers to run them
 *
 */

#include <linux/debugfs.h>
#include <linux/sched.h>

#include "sched.h"

#ifdef CONFIG_SCHED_EEVDF_TESTING

bool eevdf_positive_lag_test;

static struct dentry *debugfs_eevdf_testing;
void debugfs_eevdf_testing_init(struct dentry *debugfs_sched)
{
	debugfs_eevdf_testing = debugfs_create_dir("eevdf-testing", debugfs_sched);

	debugfs_create_bool("eevdf_positive_lag_test", 0700,
				debugfs_eevdf_testing, &eevdf_positive_lag_test);

}

void test_eevdf_positive_lag(struct cfs_rq *cfs, struct sched_entity *se)
{
	static int eevdf_positive_lag_test_counter;
	u64 eevdf_average_vruntime;

	if (!eevdf_positive_lag_test)
		return;

	if (!se || !cfs)
		return;

	eevdf_average_vruntime = avg_vruntime(cfs);
	eevdf_positive_lag_test_counter++;

	if (se->vruntime > eevdf_average_vruntime) {
		trace_printk("FAIL: Lemma 1 failed - selected task has negative lag\n");
		eevdf_positive_lag_test = 0;
		eevdf_positive_lag_test_counter = 0;
		return;
	}

	if (eevdf_positive_lag_test_counter > 100) {
		eevdf_positive_lag_test = 0;
		eevdf_positive_lag_test_counter = 0;
		trace_printk("PASS: At least 100 selected tasks had positive lag\n");
	}
}

/*
 * we do, what we need to do
 */
#define __node_2_se(node) \
	rb_entry((node), struct sched_entity, run_node)

/*
 * Call with rq lock held
 *
 * return false on failure
 */
static bool test_eevdf_zero_lag(struct cfs_rq *cfs)
{
	u64 cfs_avg_vruntime;
	u64 calculated_avg_vruntime;

	u64 total_vruntime = 0;
	u64 nr_tasks = 0;

	struct sched_entity *se;
	struct rb_node *node;
	struct rb_root *root;

	cfs_avg_vruntime = avg_vruntime(cfs);

	/*
	 * Walk through the rb tree -> look at the se->vruntime value and add it
	 */

	total_vruntime = 0;
	nr_tasks = 0;

	root = &cfs->tasks_timeline.rb_root;

	for (node = rb_first(root); node; node = rb_next(node)) {
		se = __node_2_se(node);
		total_vruntime += se->vruntime;
		/*
		 * Let's check if the internals are consistent
		 * Also recursion, uggh, maybe we want to do this slightly
		 * differently?
		 */
		if (!entity_is_task(se)) {
			if (!test_eevdf_zero_lag(group_cfs_rq(se)))
				return false;
		}
		nr_tasks++;
	}

	if (cfs->curr) {
		total_vruntime += cfs->curr->vruntime;
		nr_tasks++;
	}

	/* If there are no tasks, there is no lag :-) */
	if (!nr_tasks)
		return true;

	calculated_avg_vruntime = total_vruntime / nr_tasks;

	return (calculated_avg_vruntime == cfs_avg_vruntime);
}

/* The average vruntime of the entire cfs_rq should be equal to the avg_vruntime(cfs_rq) */
void test_total_zero_lag(void)
{
	int cpu;
	struct rq *rq;
	struct rq_flags rf;
	struct cfs_rq *cfs;
	bool success;

	for_each_online_cpu(cpu) {

		rq = cpu_rq(cpu);
		guard(rq_lock_irqsave)(rq);

		cfs = &rq->cfs;

		success = test_eevdf_zero_lag(cfs);

		if (!success)
			break;
	}
	if (!success) {
		trace_printk("FAILED: tracked average vruntime doesn't match calculated average vruntime\n");
		return;
	}
	trace_printk("PASS: Tracked average runtime matches calculated average vruntime\n");
}

#endif /* CONFIG_SCHED_EEVDF_TESTING */

/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc
 * 
 * Author: Dhaval Giani (AMD) <dhaval@gianis.ca>
 *
 * This is a simple test to see if avg_vruntime is the average of all
 * the current runnable tasks on that cfs_rq
 *
 * Essentially ->
 * Walk through the rbtree, and get the vruntime, and track a running average
 *
 * This should be equal to (or not too far from it) to avg_vruntime.
 *
 * Run a kthread as an RT thread.
 *
 * This is designed for an uniprocessor system
 */

#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/sched.h>

#include "sched.h"

#ifdef CONFIG_SCHED_EEVDF_TESTING

#define __node_2_se(node) \
        rb_entry((node), struct sched_entity, run_node)

/*
 * This is the main thread function - Run at RT priority.
 * Since this is a UP system, there is no possibility of any
 * migrations. The goal is to get from rq to cfs_rq and then
 * to walk through the rb_tree and keeping track of the number
 * of threads and adding up the vruntime.
 *
 */
static int vruntime_calculator(void *data)
{
	struct rq *rq;
	int cpu;
	struct cfs_rq *cfs;
	struct sched_entity *se;

	struct rb_node *node;
	struct rb_root *root;

	u64 cfs_avg_vruntime;
	u64 nr_tasks;
	u64 cfs_running_avg_vruntime;
	u64 calculated_vruntime;
	s64 avg_difference;

	local_irq_disable();

	/*cpu = smp_processor_id();*/
	cpu = 0; /*UP only*/
	rq = cpu_rq(cpu);

	cfs = &rq->cfs;

	cfs_avg_vruntime = avg_vruntime(cfs);

	/*
	 * Walk through the rb tree -> look at the se->vruntime value and add it
	 */

	cfs_running_avg_vruntime = 0;
	nr_tasks = 0;

	root = &cfs->tasks_timeline.rb_root;
	node = rb_first(root);

	for (node = rb_first(root); node; node = rb_next(node)) {
		se = __node_2_se(node);
		cfs_running_avg_vruntime += se->vruntime;
		nr_tasks++;
	}

	/*
	 * Just adding for the sake of completeness, we should never enter
	 * this loop.
	 */
	if (cfs->curr) {
		cfs_running_avg_vruntime += cfs->curr->vruntime;
		nr_tasks++;
		trace_printk("ARGH!\n");
	}
	local_irq_enable();

	trace_printk("nr_tasks is %llu\n", nr_tasks);

	if (!nr_tasks) {
		trace_printk("No EEVDF tasks on CPU0, exit\n");
		return -1;
	}
	
	calculated_vruntime = cfs_running_avg_vruntime / nr_tasks;
	avg_difference = (s64)calculated_vruntime - (s64)cfs_avg_vruntime; /*Unneccessary paranoia)*/

	if (avg_difference)
		trace_printk("FAIL - error introduced. Lemma 2 has been violated\n");
	else
		trace_printk("PASS - calculated vruntime difference is the same as tracked. Total lag in the system is 0\n");

	return 0;
}

static int __init eevdf_avg_vruntime_init(void)
{
	struct task_struct *kt;

	kt = kthread_create(&vruntime_calculator, NULL, "eevdf-tester-%d", smp_processor_id());

	if(!kt) {
		trace_printk("Failed to launch kthread\n");
		return -1;
	}

	/* Set to RT priority */
	sched_set_fifo_low(kt);
	wake_up_process(kt);
	return 0;
}

static void __exit eevdf_avg_vruntime_exit(void)
{
}

bool eevdf_positive_lag_test = 0;

static struct dentry *debugfs_eevdf_testing;
void init_eevdf_testing_debugfs(struct dentry *debugfs_sched)
{
	debugfs_eevdf_testing = debugfs_create_dir("eevdf-testing", debugfs_sched);

	debugfs_create_bool("eevdf_positive_lag_test", 0700,
				debugfs_eevdf_testing, &eevdf_positive_lag_test);

}

#else /* CONFIG_SCHED_EEVDF_TESTING */

#endif /* CONFIG_SCHED_EEVDF_TESTING */

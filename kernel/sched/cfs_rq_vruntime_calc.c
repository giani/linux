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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/sched.h>

#include "sched.h"

u64 cfs_min_vruntime;
u64 nr_tasks;
u64 cfs_running_avg_vruntime;

static inline void read_min_vruntime(struct cfs_rq *cfs)
{
	cfs_min_vruntime = cfs->min_vruntime;
}

#define __node_2_se(node) \
        rb_entry((node), struct sched_entity, run_node)

/*
 * This is the main thread function - Run at RT 99 priority.
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

	u64 calculated_vruntime;

	trace_printk("Entered vruntime_calculator\n");

	/*cpu = smp_processor_id();*/
	cpu = 0; /*UP only*/
	rq = cpu_rq(cpu);

	cfs = &rq->cfs;

	read_min_vruntime(cfs);

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

	trace_printk("nr_tasks is %llu\n", nr_tasks);

	if (!nr_tasks) {
		trace_printk("No EEVDF tasks on CPU0, exit\n");
		return -1;
	}
	
	calculated_vruntime = cfs_running_avg_vruntime / nr_tasks;

	if (calculated_vruntime != cfs_min_vruntime)
		trace_printk("MISMATCH\n min_vruntime is %llu, calculated avg_vruntime is %llu, difference is %llu\n",
			cfs_min_vruntime, calculated_vruntime, calculated_vruntime - cfs_min_vruntime);
	else
		trace_printk("PASS: vruntime matches calculated average\n");

	return 0;
}

static int __init eevdf_avg_vruntime_init(void)
{
	struct task_struct *kt;
	trace_printk("Hello World\n");

	kt = kthread_create(&vruntime_calculator, NULL, "eevdf-tester-%d", smp_processor_id());

	if(!kt) {
		trace_printk("Failed to launch kthread\n");
		return -1;
	}
	kt->normal_prio = 99;
	wake_up_process(kt);
	return 0;
}

static void __exit eevdf_avg_vruntime_exit(void)
{
	trace_printk("Goodbye World\n");
}

MODULE_AUTHOR("Dhaval Giani");
MODULE_DESCRIPTION("EEVDF average vruntime test");
MODULE_LICENSE("GPL");

module_init(eevdf_avg_vruntime_init);
module_exit(eevdf_avg_vruntime_exit);

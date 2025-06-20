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

/*
 * Test parameters
 */
bool eevdf_positive_lag_test;
u8 eevdf_positive_lag_count = 10;

static int test_total_zero_lag(void *);
static void launch_test_zero_lag(void);

static int eevdf_zero_lag_show(struct seq_file *m, void *v)
{
	return 0;
}

static int eevdf_zero_lag_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, eevdf_zero_lag_show, NULL);
}

static ssize_t eevdf_zero_lag_write(struct file *filp, const char __user *ubuf,
				   size_t cnt, loff_t *ppos)
{
	launch_test_zero_lag();
	return 1;

}

static const struct file_operations eevdf_zero_lag_fops = {
	.open		= eevdf_zero_lag_open,
	.write		= eevdf_zero_lag_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int eevdf_lemma3_show(struct seq_file *m, void *v)
{
	return 0;
}

static int eevdf_lemma3_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, eevdf_lemma3_show, NULL);
}

static int test_total_lemma3(void *data);
static ssize_t eevdf_lemma3_write(struct file *filp, const char __user *ubuf,
				   size_t cnt, loff_t *ppos)
{
	test_total_lemma3(NULL);
	return 1;
}

static const struct file_operations eevdf_lemma3_fops = {
	.open		= eevdf_lemma3_open,
	.write		= eevdf_lemma3_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
static int eevdf_lemma4_show(struct seq_file *m, void *v)
{
	return 0;
}

static int eevdf_lemma4_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, eevdf_lemma4_show, NULL);
}

static int test_total_lemma4(void *data);
static ssize_t eevdf_lemma4_write(struct file *filp, const char __user *ubuf,
				   size_t cnt, loff_t *ppos)
{
	test_total_lemma4(NULL);
	return 1;
}

static const struct file_operations eevdf_lemma4_fops = {
	.open		= eevdf_lemma4_open,
	.write		= eevdf_lemma4_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static struct dentry *debugfs_eevdf_testing;
void debugfs_eevdf_testing_init(struct dentry *debugfs_sched)
{
	debugfs_eevdf_testing = debugfs_create_dir("eevdf-testing", debugfs_sched);

	debugfs_create_bool("eevdf_positive_lag_test", 0700,
				debugfs_eevdf_testing, &eevdf_positive_lag_test);
	debugfs_create_u8("eevdf_positive_lag_test_count", 0600,
				debugfs_eevdf_testing, &eevdf_positive_lag_count);
	debugfs_create_file("eevdf_zero_lag_test", 0700, debugfs_eevdf_testing,
				NULL, &eevdf_zero_lag_fops);

	debugfs_create_file("eevdf_lemma3_test", 0700, debugfs_eevdf_testing,
				NULL, &eevdf_lemma3_fops);

	debugfs_create_file("eevdf_lemma4_test", 0700, debugfs_eevdf_testing,
				NULL, &eevdf_lemma4_fops);
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
		trace_printk("FAIL: Lemma 1 violation - selected task has negative lag\n");
		trace_printk("  Task details:\n");
		trace_printk("    PID: %d\n", task_pid_nr(task_of(se)));
		trace_printk("    Name: %s\n", task_of(se)->comm);
		trace_printk("    Weight: %ld\n", se->load.weight);
		trace_printk("    vruntime: %llu\n", se->vruntime);
		trace_printk("    avg_vruntime: %llu\n", eevdf_average_vruntime);
		trace_printk("    lag: %lld\n", (s64)(eevdf_average_vruntime - se->vruntime));
		trace_printk("    slice: %llu\n", se->slice);
		trace_printk("    on_rq: %d\n", se->on_rq);
		trace_printk("    on_list: %d\n", !RB_EMPTY_NODE(&se->run_node));
		trace_printk("  Test progress: %d/%u tasks checked\n",
			    eevdf_positive_lag_test_counter, eevdf_positive_lag_count);
		eevdf_positive_lag_test = 0;
		eevdf_positive_lag_test_counter = 0;
		return;
	}

	if (eevdf_positive_lag_test_counter > eevdf_positive_lag_count) {
		eevdf_positive_lag_test = 0;
		eevdf_positive_lag_test_counter = 0;
		trace_printk("PASS: At least %u selected tasks had positive lag\n", eevdf_positive_lag_count);
	}
}


u64 calc_delta_fair(u64 delta, struct sched_entity *se);

/*
 * we do, what we need to do
 */
#define __node_2_se(node) \
	rb_entry((node), struct sched_entity, run_node)

static bool check_theorem1(struct sched_entity *se, u64 avg_vruntime)
{
	s64 lag, slice;

	lag = avg_vruntime - se->vruntime;
	slice = se->slice;

	if (lag <= -slice || lag >= slice) {
		trace_printk("FAIL: Theorem 1 violation - Task %d (%s) lag exceeds bounds\n",
			    entity_is_task(se) ? task_pid_nr(task_of(se)) : 0,
			    entity_is_task(se) ? task_of(se)->comm : "task_group");
		trace_printk("  Task details:\n");
		trace_printk("    PID: %d\n", entity_is_task(se) ? task_pid_nr(task_of(se)) : 0);
		trace_printk("    Name: %s\n", entity_is_task(se) ? task_of(se)->comm : "task_group");
		trace_printk("    Weight: %ld\n", se->load.weight);
		trace_printk("    vruntime: %llu\n", se->vruntime);
		trace_printk("    avg_vruntime: %llu\n", avg_vruntime);
		trace_printk("    lag: %lld\n", lag);
		trace_printk("    slice: %llu\n", slice);
		trace_printk("    bounds: [-%lld, %lld]\n", slice, slice);
		trace_printk("    on_rq: %d\n", se->on_rq);
		trace_printk("    on_list: %d\n", !RB_EMPTY_NODE(&se->run_node));
		trace_printk("    is_curr: %d\n", se == se->cfs_rq->curr);
		return false;
	}
	return true;
}

static bool test_eevdf_cfs_rq_zero_lag(struct cfs_rq *cfs, struct list_head *tg_se)
{
	u64 cfs_avg_vruntime, calculated_avg_vruntime;
	u64 total_vruntime = 0;
	u64 nr_tasks = 0;
	bool theorem1_ok = true;
	struct sched_entity *se;
	struct rb_node *node;
	struct rb_root *root;

	cfs_avg_vruntime = avg_vruntime(cfs);
	root = &cfs->tasks_timeline.rb_root;

	for (node = rb_first(root); node; node = rb_next(node)) {
		se = __node_2_se(node);
		WARN_ON_ONCE(__builtin_add_overflow(total_vruntime,
					se->vruntime, &total_vruntime));

		if (!entity_is_task(se))
			list_add_tail(&se->tg_entry, tg_se);
		nr_tasks++;

		if (!check_theorem1(se, cfs_avg_vruntime))
			theorem1_ok = false;
	}

	if (cfs->curr) {
		WARN_ON_ONCE(__builtin_add_overflow(total_vruntime,
					cfs->curr->vruntime, &total_vruntime));
		nr_tasks++;

		if (!check_theorem1(cfs->curr, cfs_avg_vruntime))
			theorem1_ok = false;
	}

	if (!nr_tasks)
		return true;

	calculated_avg_vruntime = total_vruntime / nr_tasks;

	if (!theorem1_ok) {
		trace_printk("FAIL: Theorem 1 violated - lag bounds exceeded\n");
		return false;
	}

	return (calculated_avg_vruntime == cfs_avg_vruntime);
}

/*
 * Call with rq lock held
 *
 * return false on failure
 */
static bool test_eevdf_zero_lag(struct cfs_rq *cfs)
{
	struct list_head tg_se = LIST_HEAD_INIT(tg_se);;
	struct list_head *se_entry;

	/*
	 * The base CFS runqueue will always have sched entities queued.
	 * Test it, and start populating the tg_se list.
	 *
	 * If it fails, short circuit and return fail.
	 */

	if (!test_eevdf_cfs_rq_zero_lag(cfs, &tg_se))
		return false;

	/*
	 * We made it here, let's walk through the list. Since it is
	 * setup as a queue, as we continue calling the rq test, it
	 * will add new task_groups to the list. Once drained, if we
	 * haven't failed, we will return true.
	 */

	list_for_each(se_entry, &tg_se) {
		struct sched_entity *se = list_entry(se_entry, struct sched_entity, tg_entry);
		if (!test_eevdf_cfs_rq_zero_lag(group_cfs_rq(se), &tg_se))
			return false;
	}

	/*
	 * WOOT! We succeeded!
	 */
	return true;

}

/*
 * The average vruntime of the entire cfs_rq should be equal
 * to the avg_vruntime(cfs_rq)
 */
static int test_total_zero_lag(void *data)
{
	int cpu;
	struct rq *rq;
	struct cfs_rq *cfs;
	bool success = false;

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
		return -1;
	}
	trace_printk("PASS: Tracked average runtime matches calculated average vruntime\n");
	return 0;
}

static void launch_test_zero_lag(void)
{
	struct task_struct *kt;

	kt = kthread_create(&test_total_zero_lag, NULL, "eevdf-tester-%d",
					smp_processor_id());
	if (!kt) {
		trace_printk("Failed to launch kthread\n");
		return;
	}

	wake_up_process(kt);
}

/*
 * Lemma 3: The lag of any client is always bounded by the quantum size q.
 */
static bool check_lemma3(struct sched_entity *se, u64 avg_vruntime)
{
	u64 completion_deadline;

	completion_deadline = se->deadline + se->slice;

	if (avg_vruntime > completion_deadline) {
		trace_printk("FAIL: Lemma 3 violation - Task %d (%s) request completion overdue\n",
			    entity_is_task(se) ? task_pid_nr(task_of(se)) : 0,
			    entity_is_task(se) ? task_of(se)->comm : "task_group");
		trace_printk("  Task details:\n");
		trace_printk("    PID: %d\n", entity_is_task(se) ? task_pid_nr(task_of(se)) : 0);
		trace_printk("    Name: %s\n", entity_is_task(se) ? task_of(se)->comm : "task_group");
		trace_printk("    Weight: %ld\n", se->load.weight);
		trace_printk("    vruntime: %llu\n", se->vruntime);
		trace_printk("    deadline: %llu\n", se->deadline);
		trace_printk("    slice: %llu\n", se->slice);
		trace_printk("    completion_deadline: %llu\n", completion_deadline);
		trace_printk("    current_time: %llu\n", avg_vruntime);
		trace_printk("    overdue_by: %lld\n", (s64)(avg_vruntime - completion_deadline));
		trace_printk("    on_rq: %d\n", se->on_rq);
		trace_printk("    on_list: %d\n", !RB_EMPTY_NODE(&se->run_node));
		return false;
	}
	return true;
}

static bool test_eevdf_cfs_rq_lemma3(struct cfs_rq *cfs, struct list_head *tg_se)
{
	u64 cfs_avg_vruntime;
	struct sched_entity *se;
	struct rb_node *node;
	struct rb_root *root;
	bool lemma3_ok = true;

	cfs_avg_vruntime = avg_vruntime(cfs);
	root = &cfs->tasks_timeline.rb_root;

	for (node = rb_first(root); node; node = rb_next(node)) {
		se = __node_2_se(node);
		if (!entity_is_task(se))
			list_add_tail(&se->tg_entry, tg_se);

		if (!check_lemma3(se, cfs_avg_vruntime))
			lemma3_ok = false;
	}

	if (cfs->curr) {
		if (!check_lemma3(cfs->curr, cfs_avg_vruntime))
			lemma3_ok = false;
	}

	return lemma3_ok;
}

static int test_total_lemma3(void *data)
{
	int cpu;
	struct rq *rq;
	struct cfs_rq *cfs;
	bool success = true;

	for_each_online_cpu(cpu) {
		rq = cpu_rq(cpu);
		guard(rq_lock_irqsave)(rq);
		cfs = &rq->cfs;
		success &= test_eevdf_cfs_rq_lemma3(cfs, NULL);
		if (!success) break;
	}
	if (!success) {
		trace_printk("FAILED: Lemma 3 violated on at least one CPU\n");
		return -1;
	}
	trace_printk("PASS: Lemma 3 holds on all CPUs\n");
	return 0;
}


/*
 * Lemma 4: The lag of any client is always non-positive at the time it is selected for service.
 */
static bool test_lemma4_selected_task(struct cfs_rq *cfs)
{
	struct sched_entity *se;
	u64 cfs_avg_vruntime;
	s64 lag;

	if (!cfs->curr)
		return true;

	se = cfs->curr;
	cfs_avg_vruntime = avg_vruntime(cfs);
	lag = cfs_avg_vruntime - se->vruntime;

	if (lag > 0) {
		trace_printk("FAIL: Lemma 4 violation - Selected task has positive lag\n");
		trace_printk("  Task details:\n");
		trace_printk("    PID: %d\n", entity_is_task(se) ? task_pid_nr(task_of(se)) : 0);
		trace_printk("    Name: %s\n", entity_is_task(se) ? task_of(se)->comm : "task_group");
		trace_printk("    Weight: %ld\n", se->load.weight);
		trace_printk("    vruntime: %llu\n", se->vruntime);
		trace_printk("    avg_vruntime: %llu\n", cfs_avg_vruntime);
		trace_printk("    lag: %lld\n", lag);
		trace_printk("    slice: %llu\n", se->slice);
		trace_printk("    on_rq: %d\n", se->on_rq);
		trace_printk("    on_list: %d\n", !RB_EMPTY_NODE(&se->run_node));
		return false;
	}
	return true;
}

static int test_total_lemma4(void *data)
{
	int cpu;
	struct rq *rq;
	struct cfs_rq *cfs;
	bool success = true;

	for_each_online_cpu(cpu) {
		rq = cpu_rq(cpu);
		guard(rq_lock_irqsave)(rq);
		cfs = &rq->cfs;
		success &= test_lemma4_selected_task(cfs);
	}
	if (!success) {
		trace_printk("FAILED: Lemma 4 violated on at least one CPU\n");
		return -1;
	}
	trace_printk("PASS: Lemma 4 holds on all CPUs\n");
	return 0;
}

#endif /* CONFIG_SCHED_EEVDF_TESTING */

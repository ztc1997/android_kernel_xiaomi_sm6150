// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018-2019 Sultan Alsawaf <sultan@kerneltoast.com>.
 */

#define pr_fmt(fmt) "cpu_input_limit: " fmt

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/input.h>
#include <linux/kthread.h>
#include <linux/moduleparam.h>
#include <linux/msm_drm_notify.h>
#include <linux/slab.h>
#include <linux/version.h>

/* The sched_param struct is located elsewhere in newer kernels */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 10, 0)
#include <uapi/linux/sched/types.h>
#endif

static unsigned int max_limit_freq_big __read_mostly =
	CONFIG_MAX_LIMIT_FREQ_PERF;

static unsigned short input_limit_duration __read_mostly =
	CONFIG_INPUT_LIMIT_DURATION_MS;

module_param(max_limit_freq_big, uint, 0644);

module_param(input_limit_duration, short, 0644);

enum {
	SCREEN_OFF,
	INPUT_LIMIT
};

struct limit_drv {
	struct delayed_work input_unlimit;
	struct notifier_block cpu_notif;
	struct notifier_block msm_drm_notif;
	wait_queue_head_t limit_waitq;
	unsigned long state;
};

static void input_unlimit_worker(struct work_struct *work);

static struct limit_drv limit_drv_g __read_mostly = {
	.input_unlimit = __DELAYED_WORK_INITIALIZER(limit_drv_g.input_unlimit,
						    input_unlimit_worker, 0),
	.limit_waitq = __WAIT_QUEUE_HEAD_INITIALIZER(limit_drv_g.limit_waitq)
};

static void update_online_cpu_policy(void)
{
	unsigned int cpu;

	/* Only one CPU from each cluster needs to be updated */
	get_online_cpus();
	cpu = cpumask_first_and(cpu_perf_mask, cpu_online_mask);
	cpufreq_update_policy(cpu);
	put_online_cpus();
}

static void __cpu_input_limit_kick(struct limit_drv *b)
{
	if (test_bit(SCREEN_OFF, &b->state))
		return;

	if (!input_limit_duration)
		return;

	set_bit(INPUT_LIMIT, &b->state);
	if (!mod_delayed_work(system_unbound_wq, &b->input_unlimit,
			      msecs_to_jiffies(input_limit_duration)))
		wake_up(&b->limit_waitq);
}

void cpu_input_limit_kick(void)
{
	struct limit_drv *b = &limit_drv_g;

	__cpu_input_limit_kick(b);
}

static void input_unlimit_worker(struct work_struct *work)
{
	struct limit_drv *b = container_of(to_delayed_work(work),
					   typeof(*b), input_unlimit);

	clear_bit(INPUT_LIMIT, &b->state);
	wake_up(&b->limit_waitq);
}

static int cpu_limit_thread(void *data)
{
	static const struct sched_param sched_max_rt_prio = {
		.sched_priority = MAX_RT_PRIO - 1
	};
	struct limit_drv *b = data;
	unsigned long old_state = 0;

	sched_setscheduler_nocheck(current, SCHED_FIFO, &sched_max_rt_prio);

	while (1) {
		bool should_stop = false;
		unsigned long curr_state;

		wait_event(b->limit_waitq,
			(curr_state = READ_ONCE(b->state)) != old_state ||
			(should_stop = kthread_should_stop()));

		if (should_stop)
			break;

		old_state = curr_state;
		update_online_cpu_policy();
	}

	return 0;
}

static int cpu_notifier_cb(struct notifier_block *nb, unsigned long action,
			   void *data)
{
	struct limit_drv *b = container_of(nb, typeof(*b), cpu_notif);
	struct cpufreq_policy *policy = data;

	if (action != CPUFREQ_ADJUST || cpumask_test_cpu(policy->cpu, cpu_lp_mask))
		return NOTIFY_OK;

	/* Unlimit when the screen is off */
	if (test_bit(SCREEN_OFF, &b->state)) {
		policy->max = policy->cpuinfo.max_freq;
		return NOTIFY_OK;
	}

	/*
	 * Limit to policy->max if the limit frequency is higher. When
	 * unlimiting, set policy->max to the absolute max freq for the CPU.
	 */
	if (test_bit(INPUT_LIMIT, &b->state))
		policy->max = max_limit_freq_big;
	else
		policy->max = policy->cpuinfo.max_freq;

	return NOTIFY_OK;
}

static int msm_drm_notifier_cb(struct notifier_block *nb, unsigned long action,
			  void *data)
{
	struct limit_drv *b = container_of(nb, typeof(*b), msm_drm_notif);
	struct msm_drm_notifier *evdata = data;
	int *blank = evdata->data;

	/* Parse framebuffer blank events as soon as they occur */
	if (action != MSM_DRM_EARLY_EVENT_BLANK)
		return NOTIFY_OK;

	/* Boost when the screen turns on and unlimit when it turns off */
	if (*blank == MSM_DRM_BLANK_UNBLANK) {
		clear_bit(SCREEN_OFF, &b->state);
	} else {
		set_bit(SCREEN_OFF, &b->state);
		wake_up(&b->limit_waitq);
	}

	return NOTIFY_OK;
}

static void cpu_input_limit_input_event(struct input_handle *handle,
					unsigned int type, unsigned int code,
					int value)
{
	struct limit_drv *b = handle->handler->private;

	__cpu_input_limit_kick(b);
}

static int cpu_input_limit_input_connect(struct input_handler *handler,
					 struct input_dev *dev,
					 const struct input_device_id *id)
{
	struct input_handle *handle;
	int ret;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "cpu_input_limit_handle";

	ret = input_register_handle(handle);
	if (ret)
		goto free_handle;

	ret = input_open_device(handle);
	if (ret)
		goto unregister_handle;

	return 0;

unregister_handle:
	input_unregister_handle(handle);
free_handle:
	kfree(handle);
	return ret;
}

static void cpu_input_limit_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id cpu_input_limit_ids[] = {
	/* Multi-touch touchscreen */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			BIT_MASK(ABS_MT_POSITION_X) |
			BIT_MASK(ABS_MT_POSITION_Y) }
	},
	/* Touchpad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) }
	},
	/* Keypad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) }
	},
	{ }
};

static struct input_handler cpu_input_limit_input_handler = {
	.event		= cpu_input_limit_input_event,
	.connect	= cpu_input_limit_input_connect,
	.disconnect	= cpu_input_limit_input_disconnect,
	.name		= "cpu_input_limit_handler",
	.id_table	= cpu_input_limit_ids
};

static int __init cpu_input_limit_init(void)
{
	struct limit_drv *b = &limit_drv_g;
	struct task_struct *thread;
	int ret;

	b->cpu_notif.notifier_call = cpu_notifier_cb;
	ret = cpufreq_register_notifier(&b->cpu_notif, CPUFREQ_POLICY_NOTIFIER);
	if (ret) {
		pr_err("Failed to register cpufreq notifier, err: %d\n", ret);
		return ret;
	}

	cpu_input_limit_input_handler.private = b;
	ret = input_register_handler(&cpu_input_limit_input_handler);
	if (ret) {
		pr_err("Failed to register input handler, err: %d\n", ret);
		goto unregister_cpu_notif;
	}

	b->msm_drm_notif.notifier_call = msm_drm_notifier_cb;
	b->msm_drm_notif.priority = INT_MAX;
	ret = msm_drm_register_client(&b->msm_drm_notif);
	if (ret) {
		pr_err("Failed to register msm_drm notifier, err: %d\n", ret);
		goto unregister_handler;
	}

	thread = kthread_run(cpu_limit_thread, b, "cpu_limitd");
	if (IS_ERR(thread)) {
		ret = PTR_ERR(thread);
		pr_err("Failed to start CPU limit thread, err: %d\n", ret);
		goto unregister_fb_notif;
	}

	return 0;

unregister_fb_notif:
	msm_drm_unregister_client(&b->msm_drm_notif);
unregister_handler:
	input_unregister_handler(&cpu_input_limit_input_handler);
unregister_cpu_notif:
	cpufreq_unregister_notifier(&b->cpu_notif, CPUFREQ_POLICY_NOTIFIER);
	return ret;
}
subsys_initcall(cpu_input_limit_init);

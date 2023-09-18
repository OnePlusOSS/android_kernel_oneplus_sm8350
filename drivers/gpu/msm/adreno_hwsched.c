// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#include "adreno.h"
#include "adreno_a6xx.h"
#include "adreno_a6xx_hwsched.h"
#include "adreno_snapshot.h"
#include "adreno_sysfs.h"
#include "adreno_trace.h"

/* This structure represents inflight command object */
struct cmd_list_obj {
	/** @cmdobj: Handle to the command object */
	struct kgsl_drawobj_cmd *cmdobj;
	/** @node: List node to put it in the list of inflight commands */
	struct list_head node;
};

/*
 * Number of commands that can be queued in a context before it sleeps
 *
 * Our code that "puts back" a command from the context is much cleaner
 * if we are sure that there will always be enough room in the ringbuffer
 * so restrict the size of the context queue to ADRENO_CONTEXT_DRAWQUEUE_SIZE - 1
 */
static u32 _context_drawqueue_size = ADRENO_CONTEXT_DRAWQUEUE_SIZE - 1;

/* Number of milliseconds to wait for the context queue to clear */
static unsigned int _context_queue_wait = 10000;

/* Use a kmem cache to speed up allocations for dispatcher jobs */
static struct kmem_cache *jobs_cache;
/* Use a kmem cache to speed up allocations for inflight command objects */
static struct kmem_cache *obj_cache;

static struct adreno_hwsched *to_hwsched(struct adreno_device *adreno_dev)
{
	struct a6xx_device *a6xx_dev = container_of(adreno_dev,
					struct a6xx_device, adreno_dev);
	struct a6xx_hwsched_device *a6xx_hwsched = container_of(a6xx_dev,
					struct a6xx_hwsched_device, a6xx_dev);

	return &a6xx_hwsched->hwsched;
}

static struct adreno_device *hwsched_to_adreno(struct adreno_hwsched *hwsched)
{
	struct a6xx_hwsched_device *a6xx_hwsched = container_of(hwsched,
					struct a6xx_hwsched_device, hwsched);

	return &a6xx_hwsched->a6xx_dev.adreno_dev;
}

static bool _check_context_queue(struct adreno_context *drawctxt, u32 count)
{
	bool ret;

	spin_lock(&drawctxt->lock);

	/*
	 * Wake up if there is room in the context or if the whole thing got
	 * invalidated while we were asleep
	 */

	if (kgsl_context_invalid(&drawctxt->base))
		ret = false;
	else
		ret = ((drawctxt->queued + count) < _context_drawqueue_size) ? 1 : 0;

	spin_unlock(&drawctxt->lock);

	return ret;
}

static void _pop_drawobj(struct adreno_context *drawctxt)
{
	drawctxt->drawqueue_head = DRAWQUEUE_NEXT(drawctxt->drawqueue_head,
		ADRENO_CONTEXT_DRAWQUEUE_SIZE);
	drawctxt->queued--;
}

static int _retire_syncobj(struct kgsl_drawobj_sync *syncobj,
				struct adreno_context *drawctxt)
{
	if (!kgsl_drawobj_events_pending(syncobj)) {
		_pop_drawobj(drawctxt);
		kgsl_drawobj_destroy(DRAWOBJ(syncobj));
		return 0;
	}

	/*
	 * If we got here, there are pending events for sync object.
	 * Start the canary timer if it hasnt been started already.
	 */
	if (!syncobj->timeout_jiffies) {
		syncobj->timeout_jiffies = jiffies + msecs_to_jiffies(5000);
			mod_timer(&syncobj->timer, syncobj->timeout_jiffies);
	}

	return -EAGAIN;
}

static bool _marker_expired(struct kgsl_drawobj_cmd *markerobj)
{
	struct kgsl_drawobj *drawobj = DRAWOBJ(markerobj);

	return (drawobj->flags & KGSL_DRAWOBJ_MARKER) &&
		kgsl_check_timestamp(drawobj->device, drawobj->context,
		markerobj->marker_timestamp);
}

static void _retire_timestamp(struct kgsl_drawobj *drawobj)
{
	struct kgsl_context *context = drawobj->context;
	struct kgsl_device *device = context->device;

	/*
	 * Write the start and end timestamp to the memstore to keep the
	 * accounting sane
	 */
	kgsl_sharedmem_writel(device->memstore,
		KGSL_MEMSTORE_OFFSET(context->id, soptimestamp),
		drawobj->timestamp);

	kgsl_sharedmem_writel(device->memstore,
		KGSL_MEMSTORE_OFFSET(context->id, eoptimestamp),
		drawobj->timestamp);

	/* Retire pending GPU events for the object */
	kgsl_process_event_group(device, &context->events);

	kgsl_drawobj_destroy(drawobj);
}

static int _retire_markerobj(struct kgsl_drawobj_cmd *cmdobj,
	struct adreno_context *drawctxt)
{
	if (_marker_expired(cmdobj)) {
		_pop_drawobj(drawctxt);
		_retire_timestamp(DRAWOBJ(cmdobj));
		return 0;
	}

	/*
	 * If the marker isn't expired but the SKIP bit
	 * is set then there are real commands following
	 * this one in the queue. This means that we
	 * need to dispatch the command so that we can
	 * keep the timestamp accounting correct. If
	 * skip isn't set then we block this queue
	 * until the dependent timestamp expires
	 */

	return test_bit(CMDOBJ_SKIP, &cmdobj->priv) ? 1 : -EAGAIN;
}

/*
 * Retires all expired marker and sync objs from the context
 * queue and returns one of the below
 * a) next drawobj that needs to be sent to ringbuffer
 * b) -EAGAIN for syncobj with syncpoints pending.
 * c) -EAGAIN for markerobj whose marker timestamp has not expired yet.
 * c) NULL for no commands remaining in drawqueue.
 */
static struct kgsl_drawobj *_process_drawqueue_get_next_drawobj(
				struct adreno_context *drawctxt)
{
	struct kgsl_drawobj *drawobj;
	unsigned int i = drawctxt->drawqueue_head;
	int ret = 0;

	if (drawctxt->drawqueue_head == drawctxt->drawqueue_tail)
		return NULL;

	for (i = drawctxt->drawqueue_head; i != drawctxt->drawqueue_tail;
			i = DRAWQUEUE_NEXT(i, ADRENO_CONTEXT_DRAWQUEUE_SIZE)) {

		drawobj = drawctxt->drawqueue[i];

		if (drawobj == NULL)
			return NULL;

		if (drawobj->type == CMDOBJ_TYPE)
			return drawobj;
		else if (drawobj->type == SYNCOBJ_TYPE)
			ret = _retire_syncobj(SYNCOBJ(drawobj), drawctxt);
		else if (drawobj->type == MARKEROBJ_TYPE) {
			ret = _retire_markerobj(CMDOBJ(drawobj), drawctxt);
			/* Special case where marker needs to be sent to GPU */
			if (ret == 1)
				return drawobj;
		} else {
			return ERR_PTR(-EINVAL);
		}

		if (ret == -EAGAIN)
			return ERR_PTR(-EAGAIN);
	}

	return NULL;
}

/**
 * hwsched_dispatcher_requeue_cmdobj() - Put a command back on the context
 * queue
 * @drawctxt: Pointer to the adreno draw context
 * @cmdobj: Pointer to the KGSL command object to requeue
 *
 * Failure to submit a command to the ringbuffer isn't the fault of the command
 * being submitted so if a failure happens, push it back on the head of the
 * context queue to be reconsidered again unless the context got detached.
 */
static inline int hwsched_dispatcher_requeue_cmdobj(
		struct adreno_context *drawctxt,
		struct kgsl_drawobj_cmd *cmdobj)
{
	unsigned int prev;
	struct kgsl_drawobj *drawobj = DRAWOBJ(cmdobj);

	spin_lock(&drawctxt->lock);

	if (kgsl_context_detached(&drawctxt->base) ||
		kgsl_context_invalid(&drawctxt->base)) {
		spin_unlock(&drawctxt->lock);
		/* get rid of this drawobj since the context is bad */
		kgsl_drawobj_destroy(drawobj);
		return -ENOENT;
	}

	prev = drawctxt->drawqueue_head == 0 ?
		(ADRENO_CONTEXT_DRAWQUEUE_SIZE - 1) :
		(drawctxt->drawqueue_head - 1);

	/*
	 * The maximum queue size always needs to be one less then the size of
	 * the ringbuffer queue so there is "room" to put the drawobj back in
	 */

	WARN_ON(prev == drawctxt->drawqueue_tail);

	drawctxt->drawqueue[prev] = drawobj;
	drawctxt->queued++;

	/* Reset the command queue head to reflect the newly requeued change */
	drawctxt->drawqueue_head = prev;
	spin_unlock(&drawctxt->lock);
	return 0;
}

/**
 * hwsched_queue_context() - Queue a context in the dispatcher list of jobs
 * @adreno_dev: Pointer to the adreno device structure
 * @drawctxt: Pointer to the adreno draw context
 *
 * Add a context to the dispatcher list of jobs.
 */
static int hwsched_queue_context(struct adreno_device *adreno_dev,
		struct adreno_context *drawctxt)
{
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);
	struct adreno_dispatch_job *job;

	/* Refuse to queue a detached context */
	if (kgsl_context_detached(&drawctxt->base))
		return 0;

	if (!_kgsl_context_get(&drawctxt->base))
		return 0;

	job = kmem_cache_alloc(jobs_cache, GFP_ATOMIC);
	if (!job) {
		kgsl_context_put(&drawctxt->base);
		return -ENOMEM;
	}

	job->drawctxt = drawctxt;

	trace_dispatch_queue_context(drawctxt);
	llist_add(&job->node, &hwsched->jobs[drawctxt->base.priority]);

	return 0;
}

void adreno_hwsched_set_fault(struct adreno_device *adreno_dev)
{
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);

	atomic_set(&hwsched->fault, ADRENO_HWSCHED_FAULT_RESTART);

	/* make sure fault is written before triggering dispatcher */
	smp_wmb();

	adreno_hwsched_trigger(adreno_dev);
}

static bool hwsched_in_fault(struct adreno_hwsched *hwsched)
{
	/* make sure we're reading the latest value */
	smp_rmb();
	return atomic_read(&hwsched->fault) != 0;
}

/**
 * sendcmd() - Send a drawobj to the GPU hardware
 * @dispatcher: Pointer to the adreno dispatcher struct
 * @drawobj: Pointer to the KGSL drawobj being sent
 *
 * Send a KGSL drawobj to the GPU hardware
 */
static int hwsched_sendcmd(struct adreno_device *adreno_dev,
	struct kgsl_drawobj_cmd *cmdobj)
{
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);
	struct kgsl_drawobj *drawobj = DRAWOBJ(cmdobj);
	struct kgsl_context *context = drawobj->context;
	struct adreno_context *drawctxt = ADRENO_CONTEXT(drawobj->context);
	int ret;
	struct cmd_list_obj *obj;

	obj = kmem_cache_alloc(obj_cache, GFP_KERNEL);
	if (!obj)
		return -ENOMEM;

	mutex_lock(&device->mutex);

	if (adreno_gpu_halt(adreno_dev) != 0) {
		mutex_unlock(&device->mutex);
		kmem_cache_free(obj_cache, obj);
		return -EBUSY;
	}


	if (kgsl_context_detached(context)) {
		mutex_unlock(&device->mutex);
		kmem_cache_free(obj_cache, obj);
		return -ENOENT;
	}

	hwsched->inflight++;

	if (hwsched->inflight == 1 &&
		!test_bit(ADRENO_HWSCHED_POWER, &hwsched->flags)) {
		ret = adreno_active_count_get(adreno_dev);
		if (ret) {
			hwsched->inflight--;
			mutex_unlock(&device->mutex);
			kmem_cache_free(obj_cache, obj);
			return ret;
		}
		set_bit(ADRENO_HWSCHED_POWER, &hwsched->flags);
	}

	ret = a6xx_hwsched_submit_cmdobj(adreno_dev, cmdobj);
	if (ret) {
		/*
		 * If the first submission failed, then put back the active
		 * count to relinquish active vote
		 */
		if (hwsched->inflight == 1) {
			adreno_active_count_put(adreno_dev);
			clear_bit(ADRENO_HWSCHED_POWER, &hwsched->flags);
		} else if (device->gmu_fault) {
			/*
			 * If we encountered a GMU fault, then trigger reset and
			 * recovery.
			 */
			adreno_get_gpu_halt(adreno_dev);

			adreno_hwsched_set_fault(adreno_dev);
		}

		hwsched->inflight--;
		kmem_cache_free(obj_cache, obj);
		mutex_unlock(&device->mutex);
		return ret;
	}

	drawctxt->internal_timestamp = drawobj->timestamp;

	obj->cmdobj = cmdobj;
	list_add_tail(&obj->node, &hwsched->cmd_list);
	mutex_unlock(&device->mutex);

	return 0;
}

/**
 * hwsched_sendcmds() - Send commands from a context to the GPU
 * @adreno_dev: Pointer to the adreno device struct
 * @drawctxt: Pointer to the adreno context to dispatch commands from
 *
 * Dequeue and send a burst of commands from the specified context to the GPU
 * Returns postive if the context needs to be put back on the pending queue
 * 0 if the context is empty or detached and negative on error
 */
static int hwsched_sendcmds(struct adreno_device *adreno_dev,
		struct adreno_context *drawctxt)
{
	int count = 0;
	int ret = 0;
	unsigned int timestamp;

	while (1) {
		struct kgsl_drawobj *drawobj;
		struct kgsl_drawobj_cmd *cmdobj;

		spin_lock(&drawctxt->lock);
		drawobj = _process_drawqueue_get_next_drawobj(drawctxt);

		/*
		 * adreno_context_get_drawobj returns -EAGAIN if the current
		 * drawobj has pending sync points so no more to do here.
		 * When the sync points are satisfied then the context will get
		 * reqeueued
		 */

		if (IS_ERR_OR_NULL(drawobj)) {
			if (IS_ERR(drawobj))
				ret = PTR_ERR(drawobj);
			spin_unlock(&drawctxt->lock);
			break;
		}
		_pop_drawobj(drawctxt);
		spin_unlock(&drawctxt->lock);

		timestamp = drawobj->timestamp;
		cmdobj = CMDOBJ(drawobj);
		ret = hwsched_sendcmd(adreno_dev, cmdobj);

		/*
		 * On error from hwsched_sendcmd() try to requeue the cmdobj
		 * unless we got back -ENOENT which means that the context has
		 * been detached and there will be no more deliveries from here
		 */
		if (ret != 0) {
			/* Destroy the cmdobj on -ENOENT */
			if (ret == -ENOENT)
				kgsl_drawobj_destroy(drawobj);
			else {
				/*
				 * If we couldn't put it on dispatch queue
				 * then return it to the context queue
				 */
				int r = hwsched_dispatcher_requeue_cmdobj(
					drawctxt, cmdobj);
				if (r)
					ret = r;
			}

			break;
		}

		drawctxt->submitted_timestamp = timestamp;

		count++;
	}

	/*
	 * Wake up any snoozing threads if we have consumed any real commands
	 * or marker commands and we have room in the context queue.
	 */

	if (_check_context_queue(drawctxt, 0))
		wake_up_all(&drawctxt->wq);

	if (!ret)
		ret = count;

	/* Return error or the number of commands queued */
	return ret;
}

static bool adreno_drawctxt_bad(struct adreno_context *drawctxt)
{
	return (kgsl_context_detached(&drawctxt->base) ||
		kgsl_context_invalid(&drawctxt->base));
}


static void hwsched_handle_jobs_list(struct adreno_device *adreno_dev,
	int id, unsigned long *map, struct llist_node *list)
{
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);
	struct adreno_dispatch_job *job, *next;

	if (!list)
		return;

	/* Reverse the list so we deal with oldest submitted contexts first */
	list = llist_reverse_order(list);

	llist_for_each_entry_safe(job, next, list, node) {
		int ret;

		if (adreno_drawctxt_bad(job->drawctxt)) {
			kgsl_context_put(&job->drawctxt->base);
			kmem_cache_free(jobs_cache, job);
			continue;
		}

		/*
		 * Due to the nature of the lockless queue the same context
		 * might have multiple jobs on the list. We allow this so we
		 * don't have to query the list on the producer side but on the
		 * consumer side we only want each context to be considered
		 * once. Use a bitmap to remember which contexts we've already
		 * seen and quietly discard duplicate jobs
		 */
		if (test_and_set_bit(job->drawctxt->base.id, map)) {
			kgsl_context_put(&job->drawctxt->base);
			kmem_cache_free(jobs_cache, job);
			continue;
		}

		ret = hwsched_sendcmds(adreno_dev, job->drawctxt);

		/*
		 * If the context had nothing queued or the context has been
		 * destroyed then drop the job
		 */
		if (!ret || ret == -ENOENT) {
			kgsl_context_put(&job->drawctxt->base);
			kmem_cache_free(jobs_cache, job);
			continue;
		}

		/*
		 * If the dispatch queue is full then requeue the job to be
		 * considered first next time. Otherwise the context
		 * either successfully submmitted to the GPU or another error
		 * happened and it should go back on the regular queue
		 */
		if (ret == -ENOSPC)
			llist_add(&job->node, &hwsched->requeue[id]);
		else
			llist_add(&job->node, &hwsched->jobs[id]);
	}
}

static void hwsched_handle_jobs(struct adreno_device *adreno_dev, int id)
{
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);
	unsigned long map[BITS_TO_LONGS(KGSL_MEMSTORE_MAX)];
	struct llist_node *requeue, *jobs;

	memset(map, 0, sizeof(map));

	requeue = llist_del_all(&hwsched->requeue[id]);
	jobs = llist_del_all(&hwsched->jobs[id]);

	hwsched_handle_jobs_list(adreno_dev, id, map, requeue);
	hwsched_handle_jobs_list(adreno_dev, id, map, jobs);
}

/**
 * hwsched_issuecmds() - Issue commmands from pending contexts
 * @adreno_dev: Pointer to the adreno device struct
 *
 * Issue as many commands as possible (up to inflight) from the pending contexts
 * This function assumes the dispatcher mutex has been locked.
 */
static void hwsched_issuecmds(struct adreno_device *adreno_dev)
{
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(hwsched->jobs); i++)
		hwsched_handle_jobs(adreno_dev, i);
}

void adreno_hwsched_trigger(struct adreno_device *adreno_dev)
{
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);

	kthread_queue_work(&kgsl_driver.worker, &hwsched->work);
}

/**
 * adreno_hwsched_issuecmds() - Issue commmands from pending contexts
 * @adreno_dev: Pointer to the adreno device struct
 *
 * Lock the dispatcher and call hwsched_issuecmds
 */
static void adreno_hwsched_issuecmds(struct adreno_device *adreno_dev)
{
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);

	/* If the dispatcher is busy then schedule the work for later */
	if (!mutex_trylock(&hwsched->mutex)) {
		adreno_hwsched_trigger(adreno_dev);
		return;
	}

	if (!hwsched_in_fault(hwsched))
		hwsched_issuecmds(adreno_dev);

	mutex_unlock(&hwsched->mutex);
}

/**
 * get_timestamp() - Return the next timestamp for the context
 * @drawctxt - Pointer to an adreno draw context struct
 * @drawobj - Pointer to a drawobj
 * @timestamp - Pointer to a timestamp value possibly passed from the user
 * @user_ts - user generated timestamp
 *
 * Assign a timestamp based on the settings of the draw context and the command
 * batch.
 */
static int get_timestamp(struct adreno_context *drawctxt,
		struct kgsl_drawobj *drawobj, unsigned int *timestamp,
		unsigned int user_ts)
{

	if (drawctxt->base.flags & KGSL_CONTEXT_USER_GENERATED_TS) {
		/*
		 * User specified timestamps need to be greater than the last
		 * issued timestamp in the context
		 */
		if (timestamp_cmp(drawctxt->timestamp, user_ts) >= 0)
			return -ERANGE;

		drawctxt->timestamp = user_ts;
	} else
		drawctxt->timestamp++;

	*timestamp = drawctxt->timestamp;
	drawobj->timestamp = *timestamp;
	return 0;
}

static inline int _check_context_state(struct kgsl_context *context)
{
	if (kgsl_context_invalid(context))
		return -EDEADLK;

	if (kgsl_context_detached(context))
		return -ENOENT;

	return 0;
}

static inline bool _verify_ib(struct kgsl_device_private *dev_priv,
		struct kgsl_context *context, struct kgsl_memobj_node *ib)
{
	struct kgsl_device *device = dev_priv->device;
	struct kgsl_process_private *private = dev_priv->process_priv;

	/* The maximum allowable size for an IB in the CP is 0xFFFFF dwords */
	if (ib->size == 0 || ((ib->size >> 2) > 0xFFFFF)) {
		pr_context(device, context, "ctxt %d invalid ib size %lld\n",
			context->id, ib->size);
		return false;
	}

	/* Make sure that the address is mapped */
	if (!kgsl_mmu_gpuaddr_in_range(private->pagetable, ib->gpuaddr,
		ib->size)) {
		pr_context(device, context, "ctxt %d invalid ib gpuaddr %llX\n",
			context->id, ib->gpuaddr);
		return false;
	}

	return true;
}

static inline int _verify_cmdobj(struct kgsl_device_private *dev_priv,
		struct kgsl_context *context, struct kgsl_drawobj *drawobj[],
		uint32_t count)
{
	struct kgsl_device *device = dev_priv->device;
	struct kgsl_memobj_node *ib;
	unsigned int i;

	for (i = 0; i < count; i++) {
		/* Verify the IBs before they get queued */
		if (drawobj[i]->type == CMDOBJ_TYPE) {
			struct kgsl_drawobj_cmd *cmdobj = CMDOBJ(drawobj[i]);

			list_for_each_entry(ib, &cmdobj->cmdlist, node)
				if (!_verify_ib(dev_priv,
					&ADRENO_CONTEXT(context)->base, ib))
					return -EINVAL;

			/*
			 * Clear the wake on touch bit to indicate an IB has
			 * been submitted since the last time we set it.
			 * But only clear it when we have rendering commands.
			 */
			device->flags &= ~KGSL_FLAG_WAKE_ON_TOUCH;
		}
	}

	return 0;
}

static inline int _wait_for_room_in_context_queue(
	struct adreno_context *drawctxt, u32 count)
{
	int ret = 0;

	/*
	 * There is always a possibility that dispatcher may end up pushing
	 * the last popped draw object back to the context drawqueue. Hence,
	 * we can only queue up to _context_drawqueue_size - 1 here to make
	 * sure we never let drawqueue->queued exceed _context_drawqueue_size.
	 */
	if ((drawctxt->queued + count) > (_context_drawqueue_size - 1)) {
		trace_adreno_drawctxt_sleep(drawctxt);
		spin_unlock(&drawctxt->lock);

		ret = wait_event_interruptible_timeout(drawctxt->wq,
			_check_context_queue(drawctxt, count),
			msecs_to_jiffies(_context_queue_wait));

		spin_lock(&drawctxt->lock);
		trace_adreno_drawctxt_wake(drawctxt);

		/*
		 * Account for the possibility that the context got invalidated
		 * while we were sleeping
		 */
		if (ret > 0)
			ret = _check_context_state(&drawctxt->base);
		else if (ret == 0)
			ret = -ETIMEDOUT;
	}

	return ret;
}

static unsigned int _check_context_state_to_queue_cmds(
	struct adreno_context *drawctxt, u32 count)
{
	int ret = _check_context_state(&drawctxt->base);

	if (ret)
		return ret;

	return _wait_for_room_in_context_queue(drawctxt, count);
}

static void _queue_drawobj(struct adreno_context *drawctxt,
	struct kgsl_drawobj *drawobj)
{
	/* Put the command into the queue */
	drawctxt->drawqueue[drawctxt->drawqueue_tail] = drawobj;
	drawctxt->drawqueue_tail = (drawctxt->drawqueue_tail + 1) %
			ADRENO_CONTEXT_DRAWQUEUE_SIZE;
	drawctxt->queued++;
	trace_adreno_cmdbatch_queued(drawobj, drawctxt->queued);
}

static int _queue_cmdobj(struct adreno_device *adreno_dev,
	struct adreno_context *drawctxt, struct kgsl_drawobj_cmd *cmdobj,
	uint32_t *timestamp, unsigned int user_ts)
{
	struct kgsl_drawobj *drawobj = DRAWOBJ(cmdobj);
	u32 j;
	int ret;

	ret = get_timestamp(drawctxt, drawobj, timestamp, user_ts);
	if (ret)
		return ret;

	/*
	 * If this is a real command then we need to force any markers
	 * queued before it to dispatch to keep time linear - set the
	 * skip bit so the commands get NOPed.
	 */
	j = drawctxt->drawqueue_head;

	while (j != drawctxt->drawqueue_tail) {
		if (drawctxt->drawqueue[j]->type == MARKEROBJ_TYPE) {
			struct kgsl_drawobj_cmd *markerobj =
				CMDOBJ(drawctxt->drawqueue[j]);

			set_bit(CMDOBJ_SKIP, &markerobj->priv);
		}

		j = DRAWQUEUE_NEXT(j, ADRENO_CONTEXT_DRAWQUEUE_SIZE);
	}

	drawctxt->queued_timestamp = *timestamp;

	_queue_drawobj(drawctxt, drawobj);

	return 0;
}

static void _queue_syncobj(struct adreno_context *drawctxt,
	struct kgsl_drawobj_sync *syncobj, uint32_t *timestamp)
{
	struct kgsl_drawobj *drawobj = DRAWOBJ(syncobj);

	*timestamp = 0;
	drawobj->timestamp = 0;

	_queue_drawobj(drawctxt, drawobj);
}

static int _queue_markerobj(struct adreno_device *adreno_dev,
	struct adreno_context *drawctxt, struct kgsl_drawobj_cmd *markerobj,
	u32 *timestamp, u32 user_ts)
{
	struct kgsl_drawobj *drawobj = DRAWOBJ(markerobj);
	int ret;

	ret = get_timestamp(drawctxt, drawobj, timestamp, user_ts);
	if (ret)
		return ret;

	/*
	 * See if we can fastpath this thing - if nothing is queued
	 * and nothing is inflight retire without bothering the GPU
	 */
	if (!drawctxt->queued && kgsl_check_timestamp(drawobj->device,
		drawobj->context, drawctxt->queued_timestamp)) {
		_retire_timestamp(drawobj);
		return 1;
	}

	/*
	 * Remember the last queued timestamp - the marker will block
	 * until that timestamp is expired (unless another command
	 * comes along and forces the marker to execute)
	 */
	 markerobj->marker_timestamp = drawctxt->queued_timestamp;
	 drawctxt->queued_timestamp = *timestamp;

	_queue_drawobj(drawctxt, drawobj);

	return 0;
}

int adreno_hwsched_queue_cmds(struct kgsl_device_private *dev_priv,
	struct kgsl_context *context, struct kgsl_drawobj *drawobj[],
	u32 count, u32 *timestamp)

{
	struct kgsl_device *device = dev_priv->device;
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);
	struct adreno_context *drawctxt = ADRENO_CONTEXT(context);
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);
	struct adreno_dispatch_job *job;
	int ret;
	unsigned int i, user_ts;

	/*
	 * There is always a possibility that dispatcher may end up pushing
	 * the last popped draw object back to the context drawqueue. Hence,
	 * we can only queue up to _context_drawqueue_size - 1 here to make
	 * sure we never let drawqueue->queued exceed _context_drawqueue_size.
	 */
	if (!count || count > _context_drawqueue_size - 1)
		return -EINVAL;

	for (i = 0; i < count; i++) {
		struct kgsl_drawobj_cmd *cmdobj;
		struct kgsl_memobj_node *ib;
		u32 numibs = 0;

		if (drawobj[i]->type != CMDOBJ_TYPE)
			continue;

		cmdobj = CMDOBJ(drawobj[i]);

		list_for_each_entry(ib, &cmdobj->cmdlist, node)
			numibs++;

		if (numibs > HWSCHED_MAX_NUMIBS)
			return -EINVAL;
	}

	ret = _check_context_state(&drawctxt->base);
	if (ret)
		return ret;

	ret = _verify_cmdobj(dev_priv, context, drawobj, count);
	if (ret)
		return ret;

	/* wait for the suspend gate */
	wait_for_completion(&device->halt_gate);

	job = kmem_cache_alloc(jobs_cache, GFP_KERNEL);
	if (!job)
		return -ENOMEM;

	job->drawctxt = drawctxt;

	spin_lock(&drawctxt->lock);

	ret = _check_context_state_to_queue_cmds(drawctxt, count);
	if (ret) {
		spin_unlock(&drawctxt->lock);
		kmem_cache_free(jobs_cache, job);
		return ret;
	}

	user_ts = *timestamp;

	for (i = 0; i < count; i++) {

		switch (drawobj[i]->type) {
		case MARKEROBJ_TYPE:
			ret = _queue_markerobj(adreno_dev, drawctxt,
					CMDOBJ(drawobj[i]),
					timestamp, user_ts);
			if (ret == 1) {
				spin_unlock(&drawctxt->lock);
				kmem_cache_free(jobs_cache, job);
				return 0;
			} else if (ret) {
				spin_unlock(&drawctxt->lock);
				kmem_cache_free(jobs_cache, job);
				return ret;
			}
			break;
		case CMDOBJ_TYPE:
			ret = _queue_cmdobj(adreno_dev, drawctxt,
						CMDOBJ(drawobj[i]),
						timestamp, user_ts);
			if (ret) {
				spin_unlock(&drawctxt->lock);
				kmem_cache_free(jobs_cache, job);
				return ret;
			}
			break;
		case SYNCOBJ_TYPE:
			_queue_syncobj(drawctxt, SYNCOBJ(drawobj[i]),
						timestamp);
			break;
		default:
			spin_unlock(&drawctxt->lock);
			kmem_cache_free(jobs_cache, job);
			return -EINVAL;
		}

	}

	spin_unlock(&drawctxt->lock);

	/* Add the context to the dispatcher pending list */
	if (_kgsl_context_get(&drawctxt->base)) {
		trace_dispatch_queue_context(drawctxt);
		llist_add(&job->node, &hwsched->jobs[drawctxt->base.priority]);
		adreno_hwsched_issuecmds(adreno_dev);

	} else
		kmem_cache_free(jobs_cache, job);

	return 0;
}

static void retire_cmdobj(struct kgsl_drawobj_cmd *cmdobj)
{
	struct kgsl_drawobj *drawobj = DRAWOBJ(cmdobj);
	struct kgsl_mem_entry *entry;
	struct kgsl_drawobj_profiling_buffer *profile_buffer;

	if (cmdobj != NULL) {
		entry = cmdobj->profiling_buf_entry;
		if (entry) {
			profile_buffer = kgsl_gpuaddr_to_vaddr(&entry->memdesc,
					cmdobj->profiling_buffer_gpuaddr);

			if (profile_buffer == NULL)
				return;

			kgsl_memdesc_unmap(&entry->memdesc);
		}
	}

	kgsl_drawobj_destroy(drawobj);
}

static int retire_cmd_list(struct adreno_device *adreno_dev)
{
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	int count = 0;
	struct cmd_list_obj *obj, *tmp;

	list_for_each_entry_safe(obj, tmp, &hwsched->cmd_list, node) {
		struct kgsl_drawobj_cmd *cmdobj = obj->cmdobj;
		struct kgsl_drawobj *drawobj = DRAWOBJ(cmdobj);

		if (!kgsl_check_timestamp(device, drawobj->context,
			drawobj->timestamp))
			continue;

		retire_cmdobj(cmdobj);

		list_del_init(&obj->node);

		kmem_cache_free(obj_cache, obj);

		hwsched->inflight--;

		count++;
	}

	return count;
}

/* Take down the dispatcher and release any power states */
static void hwsched_power_down(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);

	mutex_lock(&device->mutex);

	if (test_bit(ADRENO_HWSCHED_POWER, &hwsched->flags)) {
		adreno_active_count_put(adreno_dev);
		clear_bit(ADRENO_HWSCHED_POWER, &hwsched->flags);
	}

	mutex_unlock(&device->mutex);
}

void adreno_hwsched_queue_context(struct kgsl_device *device,
	struct adreno_context *drawctxt)
{
	struct adreno_device *adreno_dev = ADRENO_DEVICE(device);

	hwsched_queue_context(adreno_dev, drawctxt);
	adreno_hwsched_trigger(adreno_dev);
}

void adreno_hwsched_start(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);

	complete_all(&device->halt_gate);

	adreno_hwsched_trigger(adreno_dev);
}

static int _skipsaverestore_store(struct adreno_device *adreno_dev, bool val)
{
	return adreno_power_cycle_bool(adreno_dev,
		&adreno_dev->preempt.skipsaverestore, val);
}

static bool _skipsaverestore_show(struct adreno_device *adreno_dev)
{
	return adreno_dev->preempt.skipsaverestore;
}

static int _usesgmem_store(struct adreno_device *adreno_dev, bool val)
{
	return adreno_power_cycle_bool(adreno_dev,
		&adreno_dev->preempt.usesgmem, val);
}

static bool _usesgmem_show(struct adreno_device *adreno_dev)
{
	return adreno_dev->preempt.usesgmem;
}

static int _preempt_level_store(struct adreno_device *adreno_dev,
		unsigned int val)
{
	return adreno_power_cycle_u32(adreno_dev,
		&adreno_dev->preempt.preempt_level,
		min_t(unsigned int, val, 2));
}

static unsigned int _preempt_level_show(struct adreno_device *adreno_dev)
{
	return adreno_dev->preempt.preempt_level;
}

static void change_preemption(struct adreno_device *adreno_dev, void *priv)
{
	change_bit(ADRENO_DEVICE_PREEMPTION, &adreno_dev->priv);
}

static int _preemption_store(struct adreno_device *adreno_dev, bool val)
{
	if (!(ADRENO_FEATURE(adreno_dev, ADRENO_PREEMPTION)) ||
		(test_bit(ADRENO_DEVICE_PREEMPTION,
		&adreno_dev->priv) == val))
		return 0;

	return adreno_power_cycle(adreno_dev, change_preemption, NULL);
}

static bool _preemption_show(struct adreno_device *adreno_dev)
{
	return adreno_is_preemption_enabled(adreno_dev);
}

static unsigned int _preempt_count_show(struct adreno_device *adreno_dev)
{
	int count = a6xx_hwsched_preempt_count_get(adreno_dev);

	return count < 0 ? 0 : count;
}

static int _gmu_log_stream_enable_store(struct adreno_device *adreno_dev,
		bool val)
{
	struct a6xx_gmu_device *gmu = to_a6xx_gmu(adreno_dev);

	if (gmu->log_stream_enable == val)
		return 0;

	return adreno_power_cycle_bool(adreno_dev, &gmu->log_stream_enable, val);
}

static bool _gmu_log_stream_enable_show(struct adreno_device *adreno_dev)
{
	struct a6xx_gmu_device *gmu = to_a6xx_gmu(adreno_dev);

	return gmu->log_stream_enable;
}

static ADRENO_SYSFS_BOOL(preemption);
static ADRENO_SYSFS_U32(preempt_level);
static ADRENO_SYSFS_BOOL(usesgmem);
static ADRENO_SYSFS_BOOL(skipsaverestore);
static ADRENO_SYSFS_RO_U32(preempt_count);
static ADRENO_SYSFS_BOOL(gmu_log_stream_enable);

static const struct attribute *_hwsched_attr_list[] = {
	&adreno_attr_preemption.attr.attr,
	&adreno_attr_preempt_level.attr.attr,
	&adreno_attr_usesgmem.attr.attr,
	&adreno_attr_skipsaverestore.attr.attr,
	&adreno_attr_preempt_count.attr.attr,
	&adreno_attr_gmu_log_stream_enable.attr.attr,
	NULL,
};

void adreno_hwsched_dispatcher_close(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);

	kmem_cache_destroy(jobs_cache);
	kmem_cache_destroy(obj_cache);

	sysfs_remove_files(&device->dev->kobj, _hwsched_attr_list);
}

static void adreno_hwsched_init_replay(struct adreno_hwsched *hwsched)
{
	struct cmd_list_obj *obj, *tmp;

	list_for_each_entry_safe(obj, tmp, &hwsched->cmd_list, node) {
		struct kgsl_drawobj_cmd *cmdobj = obj->cmdobj;

		clear_bit(KGSL_FT_REPLAY, &cmdobj->fault_policy);
	}
}

static void force_retire_timestamp(struct kgsl_device *device,
	struct kgsl_drawobj *drawobj)
{
	kgsl_sharedmem_writel(device->memstore,
		KGSL_MEMSTORE_OFFSET(drawobj->context->id, soptimestamp),
		drawobj->timestamp);

	kgsl_sharedmem_writel(device->memstore,
		KGSL_MEMSTORE_OFFSET(drawobj->context->id, eoptimestamp),
		drawobj->timestamp);
}

static void adreno_hwsched_complete_replay(struct adreno_device *adreno_dev)
{
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	struct cmd_list_obj *obj, *tmp;
	u32 retired = 0;

	list_for_each_entry_safe(obj, tmp, &hwsched->cmd_list, node) {
		struct kgsl_drawobj_cmd *cmdobj = obj->cmdobj;
		struct kgsl_drawobj *drawobj = DRAWOBJ(cmdobj);
		struct kgsl_context *context = drawobj->context;

		/*
		 * Get rid of retired objects or objects that belong to detached
		 * or invalidated contexts
		 */
		if ((kgsl_check_timestamp(device, context, drawobj->timestamp))
			|| kgsl_context_invalid(context)
			|| kgsl_context_detached(context)) {

			retire_cmdobj(cmdobj);
			retired++;
			list_del_init(&obj->node);
			kmem_cache_free(obj_cache, obj);

			continue;
		}

		if (!test_and_set_bit(KGSL_FT_REPLAY, &cmdobj->fault_policy))
			a6xx_hwsched_submit_cmdobj(adreno_dev, cmdobj);
	}

	/* Signal fences */
	if (retired)
		kgsl_process_event_groups(device);

	hwsched->inflight = 0;

	/*
	 * We have nothing to replay. So clear the fault so that
	 * dispatcher starts accepting new submissions.
	 */
	if (list_empty(&hwsched->cmd_list)) {
		atomic_set(&hwsched->fault, 0);
		adreno_clear_gpu_halt(adreno_dev);
		adreno_hwsched_trigger(adreno_dev);
	}
}

static void do_fault_header(struct adreno_device *adreno_dev,
	struct kgsl_drawobj *drawobj)
{
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	struct adreno_context *drawctxt = ADRENO_CONTEXT(drawobj->context);
	u32 status, rptr, wptr, ib1sz, ib2sz;
	u64 ib1base, ib2base;

	adreno_readreg(adreno_dev, ADRENO_REG_RBBM_STATUS, &status);
	adreno_readreg(adreno_dev, ADRENO_REG_CP_RB_RPTR, &rptr);
	adreno_readreg(adreno_dev, ADRENO_REG_CP_RB_WPTR, &wptr);
	adreno_readreg64(adreno_dev, ADRENO_REG_CP_IB1_BASE,
			ADRENO_REG_CP_IB1_BASE_HI, &ib1base);
	adreno_readreg(adreno_dev, ADRENO_REG_CP_IB1_BUFSZ, &ib1sz);
	adreno_readreg64(adreno_dev, ADRENO_REG_CP_IB2_BASE,
			ADRENO_REG_CP_IB2_BASE_HI, &ib2base);
	adreno_readreg(adreno_dev, ADRENO_REG_CP_IB2_BUFSZ, &ib2sz);

	drawobj->context->last_faulted_cmd_ts = drawobj->timestamp;
	drawobj->context->total_fault_count++;

	pr_context(device, drawobj->context,
		"ctx %d ctx_type %s ts %d status %8.8X dispatch_queue=%d rb %4.4x/%4.4x ib1 %16.16llX/%4.4x ib2 %16.16llX/%4.4x\n",
		drawobj->context->id, kgsl_context_type(drawctxt->type),
		drawobj->timestamp, status,
		drawobj->context->gmu_dispatch_queue, rptr, wptr,
		ib1base, ib1sz,	ib2base, ib2sz);

	trace_adreno_gpu_fault(drawobj->context->id, drawobj->timestamp, status,
		rptr, wptr, ib1base, ib1sz, ib2base, ib2sz,
		adreno_get_level(drawobj->context->priority));
}

static struct cmd_list_obj *get_fault_cmdobj(struct adreno_device *adreno_dev)
{
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);
	struct cmd_list_obj *obj, *tmp;

	list_for_each_entry_safe(obj, tmp, &hwsched->cmd_list, node) {

		if (test_bit(CMDOBJ_FAULT, &obj->cmdobj->priv))
			return obj;
	}

	return NULL;
}

static void reset_and_snapshot(struct adreno_device *adreno_dev)
{
	struct kgsl_drawobj *drawobj = NULL;
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	struct kgsl_context *context = NULL;
	struct cmd_list_obj *obj = get_fault_cmdobj(adreno_dev);
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);

	if (device->state != KGSL_STATE_ACTIVE)
		return;

	if (!obj) {
		kgsl_device_snapshot(device, NULL, false);
		goto done;
	}

	drawobj = DRAWOBJ(obj->cmdobj);

	context = drawobj->context;

	do_fault_header(adreno_dev, drawobj);

	kgsl_device_snapshot(device, context, false);

	force_retire_timestamp(device, drawobj);

	if (context->flags & KGSL_CONTEXT_INVALIDATE_ON_FAULT) {
		adreno_mark_guilty_context(device, context->id);
		adreno_drawctxt_invalidate(device, context);
	}

	/* This must always happen after taking snapshot */
	clear_bit(CMDOBJ_FAULT, &obj->cmdobj->priv);

	/*
	 * Put back the reference which we incremented when marking the faulty
	 *  cmdobj
	 */
	kgsl_drawobj_put(drawobj);
done:
	a6xx_hwsched_restart(adreno_dev);
	adreno_hwsched_init_replay(hwsched);
}

static void adreno_hwsched_recovery(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);

	mutex_lock(&device->mutex);

	if ((atomic_cmpxchg(&hwsched->fault, ADRENO_HWSCHED_FAULT_RESTART,
		ADRENO_HWSCHED_FAULT_REPLAY) == ADRENO_HWSCHED_FAULT_RESTART))
		reset_and_snapshot(adreno_dev);

	adreno_hwsched_complete_replay(adreno_dev);

	mutex_unlock(&device->mutex);
}

static void adreno_hwsched_work(struct kthread_work *work)
{
	struct adreno_hwsched *hwsched = container_of(work,
			struct adreno_hwsched, work);
	struct adreno_device *adreno_dev = hwsched_to_adreno(hwsched);
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	int count = 0;

	mutex_lock(&hwsched->mutex);

	if (hwsched_in_fault(hwsched)) {
		adreno_hwsched_recovery(adreno_dev);
		mutex_unlock(&hwsched->mutex);
		return;
	}

	/*
	 * As long as there are inflight commands, process retired comamnds from
	 * all drawqueues
	 */
	count += retire_cmd_list(adreno_dev);

	/* Signal fences */
	kgsl_process_event_groups(device);

	/* Run the scheduler for to dispatch new commands */
	hwsched_issuecmds(adreno_dev);

	if (hwsched->inflight == 0) {
		hwsched_power_down(adreno_dev);
	} else {
		mutex_lock(&device->mutex);
		kgsl_pwrscale_update(device);
		kgsl_start_idle_timer(device);
		mutex_unlock(&device->mutex);
	}

	mutex_unlock(&hwsched->mutex);
}

void adreno_hwsched_init(struct adreno_device *adreno_dev)
{
	struct kgsl_device *device = KGSL_DEVICE(adreno_dev);
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);
	int i;

	memset(hwsched, 0, sizeof(*hwsched));

	mutex_init(&hwsched->mutex);

	kthread_init_work(&hwsched->work, adreno_hwsched_work);

	jobs_cache = KMEM_CACHE(adreno_dispatch_job, 0);
	obj_cache = KMEM_CACHE(cmd_list_obj, 0);

	INIT_LIST_HEAD(&hwsched->cmd_list);

	for (i = 0; i < ARRAY_SIZE(hwsched->jobs); i++) {
		init_llist_head(&hwsched->jobs[i]);
		init_llist_head(&hwsched->requeue[i]);
	}

	WARN_ON(sysfs_create_files(&device->dev->kobj, _hwsched_attr_list));
}

void adreno_hwsched_mark_drawobj(struct adreno_device *adreno_dev,
	u32 ctxt_id, u32 ts)
{
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);
	struct cmd_list_obj *obj, *tmp;
	struct kgsl_drawobj *drawobj = NULL;

	mutex_lock(&hwsched->mutex);

	list_for_each_entry_safe(obj, tmp, &hwsched->cmd_list, node) {

		drawobj = DRAWOBJ(obj->cmdobj);

		if (ctxt_id == drawobj->context->id &&
			ts == drawobj->timestamp) {

			if (kref_get_unless_zero(&drawobj->refcount))
				set_bit(CMDOBJ_FAULT, &obj->cmdobj->priv);
			break;
		}
	}

	adreno_hwsched_set_fault(adreno_dev);

	mutex_unlock(&hwsched->mutex);
}

void adreno_hwsched_parse_fault_cmdobj(struct adreno_device *adreno_dev,
	struct kgsl_snapshot *snapshot)
{
	struct adreno_hwsched *hwsched = to_hwsched(adreno_dev);
	struct cmd_list_obj *obj, *tmp;

	list_for_each_entry_safe(obj, tmp, &hwsched->cmd_list, node) {
		struct kgsl_drawobj_cmd *cmdobj = obj->cmdobj;

		if (test_bit(CMDOBJ_FAULT, &cmdobj->priv)) {
			struct kgsl_memobj_node *ib;

			list_for_each_entry(ib, &cmdobj->cmdlist, node) {
				adreno_parse_ib(KGSL_DEVICE(adreno_dev),
					snapshot, snapshot->process,
					ib->gpuaddr, ib->size >> 2);
			}
		}
	}
}

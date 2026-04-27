/* Copyright (c)2021, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM stmmac
#define TRACE_INCLUDE_FILE stmmac_trace

#if !defined(_TRACE_STMMAC_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_STMMAC_H_

#include <linux/tracepoint.h>
#include <linux/timekeeping.h>
/*****************************************************************************/
/* Trace events for stmmac module */
/*****************************************************************************/
DECLARE_EVENT_CLASS(stmmac_time_template,

	TP_PROTO(int queue),

	TP_ARGS(queue),

	TP_STRUCT__entry(
		__field(int, queue)

	),

	TP_fast_assign(
		__entry->queue = queue;
	),

	TP_printk("queue[%d]", __entry->queue)
);

DEFINE_EVENT
	(stmmac_time_template, stmmac_xmit_entry,

	TP_PROTO(int queue),

	TP_ARGS(queue)
);

DEFINE_EVENT
	(stmmac_time_template, stmmac_xmit_exit,

	TP_PROTO(int queue),

	TP_ARGS(queue)
);

DEFINE_EVENT
	(stmmac_time_template, stmmac_xmit_err,

	TP_PROTO(int queue),

	TP_ARGS(queue)
);

DEFINE_EVENT
	(stmmac_time_template, stmmac_rx_entry,

	TP_PROTO(int queue),

	TP_ARGS(queue)
);

DEFINE_EVENT
	(stmmac_time_template, stmmac_rx_pkt,

	TP_PROTO(int queue),

	TP_ARGS(queue)
);

DEFINE_EVENT
	(stmmac_time_template, stmmac_rx_exit,

	TP_PROTO(int queue),

	TP_ARGS(queue)
);

DEFINE_EVENT
	(stmmac_time_template, stmmac_disable_irq,

	TP_PROTO(int queue),

	TP_ARGS(queue)
);

DEFINE_EVENT
	(stmmac_time_template, stmmac_enable_irq,

	TP_PROTO(int queue),

	TP_ARGS(queue)
);

TRACE_EVENT(stmmac_irq_enter,

	TP_PROTO(int irq),

	TP_ARGS(irq),

	TP_STRUCT__entry(
		__field(int, irq)
	),

	TP_fast_assign(
		__entry->irq = irq;
	),

	TP_printk("irq %d", __entry->irq)
);

TRACE_EVENT(stmmac_irq_exit,

	TP_PROTO(int irq),

	TP_ARGS(irq),

	TP_STRUCT__entry(
		__field(int, irq)
	),

	TP_fast_assign(
		__entry->irq = irq;
	),

	TP_printk("irq %d", __entry->irq)
);

TRACE_EVENT(stmmac_irq_status,

	TP_PROTO(int queue, int status),

	TP_ARGS(queue, status),

	TP_STRUCT__entry(
		__field(int, queue)
		__field(int, status)
	),

	TP_fast_assign(
		__entry->queue = queue;
		__entry->status = status;
	),

	TP_printk("queue[%d], status ret[%d]",
		  __entry->queue, __entry->status)
);

DEFINE_EVENT
	(stmmac_time_template, stmmac_poll_enter,

	TP_PROTO(int queue),

	TP_ARGS(queue)
);

TRACE_EVENT(stmmac_poll_exit,

	TP_PROTO(int queue, int count),

	TP_ARGS(queue, count),

	TP_STRUCT__entry(
		__field(int, queue)
		__field(int, count)
	),

	TP_fast_assign(
		__entry->queue = queue;
		__entry->count = count;
	),

	TP_printk("queue[%d], pkt cnt[%d]",
		  __entry->queue, __entry->count)
);
#endif /* _TRACE_STMMAC_H_ */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../techpack/data-eth/drivers/emac_shim
#include <trace/define_trace.h>

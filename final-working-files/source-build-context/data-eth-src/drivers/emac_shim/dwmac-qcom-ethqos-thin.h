/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */
#ifndef	_DWMAC_QCOM_ETHQOS_H
#define	_DWMAC_QCOM_ETHQOS_H

//#include <linux/msm-bus.h>
#include <linux/ipc_logging.h>
#include "../include/linux/emac_ctrl_fe_api.h"

extern void *ipc_emac_log_ctxt;

#define IPCLOG_STATE_PAGES 50
#define __FILENAME__ (strrchr(__FILE__, '/') ? \
		strrchr(__FILE__, '/') + 1 : __FILE__)

#define DRV_NAME "qcom-ethqos"
#define ETHQOSDBG(fmt, args...) \
do {\
	pr_debug(DRV_NAME " %s:%d " fmt, __func__, __LINE__, ## args);\
	if (ipc_emac_log_ctxt) { \
		ipc_log_string(ipc_emac_log_ctxt, \
		"%s: %s[%u]:[stmmac] DEBUG:" fmt, __FILENAME__,\
		__func__, __LINE__, ## args); \
	} \
} while (0)
#define ETHQOSERR(fmt, args...) \
do {\
	pr_err(DRV_NAME " %s:%d " fmt, __func__, __LINE__, ## args);\
	if (ipc_emac_log_ctxt) { \
		ipc_log_string(ipc_emac_log_ctxt, \
		"%s: %s[%u]:[emac] ERROR:" fmt, __FILENAME__,\
		__func__, __LINE__, ## args); \
	} \
} while (0)
#define ETHQOSINFO(fmt, args...) \
do {\
	pr_info(DRV_NAME " %s:%d " fmt, __func__, __LINE__, ## args);\
	if (ipc_emac_log_ctxt) { \
		ipc_log_string(ipc_emac_log_ctxt, \
		"%s: %s[%u]:[stmmac] INFO:" fmt, __FILENAME__,\
		__func__, __LINE__, ## args); \
	} \
} while (0)



struct qcom_ethqos {
	struct platform_device *pdev;

	unsigned int emac_ver;
	bool suspended;

	struct mutex lock;
	struct workqueue_struct *wq;
	struct work_struct emac_fe_rdy_work;

	struct work_struct emac_fe_work;
	struct list_head emac_fe_ev_q;
	struct notifier_block emac_nb;
};

void *qcom_ethqos_get_priv(struct qcom_ethqos *ethqos);

#define QTAG_VLAN_ETH_TYPE_OFFSET 16
#define QTAG_UCP_FIELD_OFFSET 14
#define QTAG_ETH_TYPE_OFFSET 12
#define PTP_UDP_EV_PORT 0x013F
#define PTP_UDP_GEN_PORT 0x0140

#define DEFAULT_INT_MOD 1
#define AVB_INT_MOD 8
#define IP_PKT_INT_MOD 32
#define PTP_INT_MOD 1

enum dwmac_qcom_queue_operating_mode {
	DWMAC_QCOM_QDISABLED = 0X0,
	DWMAC_QCOM_QAVB,
	DWMAC_QCOM_QDCB,
	DWMAC_QCOM_QGENERIC
};

#endif

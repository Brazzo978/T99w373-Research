/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/compiler.h>/* for __maybe_unused */
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/jiffies.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/virtio.h>
#include <linux/scatterlist.h>
#include <linux/workqueue.h>
#include "../include/linux/emac_ctrl_fe_api.h"
#include <soc/qcom/boot_stats.h>
#include "emac_ctrl_fe_virtio.h"

/*global context ptr declaration*/
static struct emac_ctrl_fe_virtio_dev *emac_ctrl_fe_ctx;

static ATOMIC_NOTIFIER_HEAD(emac_ctrl_fe_notifier_chain);

static int __maybe_unused emac_ctrl_fe_notify(enum emac_ctrl_fe_gvm_event event, int nr_to_call , int *nr_calls)
{
	int ret;
	rcu_irq_enter_irqson();
	/* this chain has a RCU read critical section which can be disfunctional
	 * in cpu idle. Copy RCU_NONIDLE code to let RCU know this.
	 */
	ret = __atomic_notifier_call_chain(&emac_ctrl_fe_notifier_chain, event, NULL,
		nr_to_call, nr_calls);
	rcu_irq_exit_irqson();
	return notifier_to_errno(ret);
}

static int __maybe_unused emac_ctl_fe_xmit(
	struct emac_ctrl_fe_virtio_dev *pdev
)
{
	unsigned long flags;
	struct scatterlist sg[1];
	struct emac_ctrl_fe_to_be_virtio_msg *msg = NULL;
	int retval = 0;

	msg = &pdev->tx_msg;
	//msg_hdr = (struct emac_ctrl_fe_virtio_msg *) msg->txmsg;
	//msg->rxbuf = NULL;

	/*lock*/
	EMAC_CTL_FE_INFO("Entry msg len =%d", msg->len);
	sg_init_one(sg, msg, sizeof(*msg));

	spin_lock_irqsave(&pdev->txq_lock, flags);
	/*expose output buffers to other end*/
	retval = virtqueue_add_outbuf(pdev->emac_ctl_txq, sg, 1, msg, GFP_ATOMIC);
	if (retval) {
		spin_unlock_irqrestore(&pdev->txq_lock, flags);
		EMAC_CTL_FE_ERR("fail to add output buffer\n");
		/* who will free buffer*/
		goto out;
	}
	/*update other side after add_buf*/
	virtqueue_kick(pdev->emac_ctl_txq);
	EMAC_CTL_FE_INFO("Kicked Host recieve Q \n");
	/*unlock*/
	spin_unlock_irqrestore(&pdev->txq_lock, flags);
out:
	return retval;
}

static void __maybe_unused emac_ctrl_fe_trigger_notif_work(struct work_struct *work) {

	if (emac_ctrl_fe_ctx->emac_ctrl_fe_state >= EMAC_CTRL_FE_EVENT_LISTN_INIT) {
		/*Register for Events*/
		emac_ctrl_fe_ctx->tx_msg.type = VIRTIO_EMAC_DMA_VIRT_REG_EVENTS;
		emac_ctrl_fe_ctx->tx_msg.len = sizeof(struct emac_ctrl_fe_to_be_virtio_msg);
		emac_ctl_fe_xmit(emac_ctrl_fe_ctx);
		EMAC_CTL_FE_INFO("Sent Register Event Cmd \n");
	}
}

static struct delayed_work emac_ctrl_fe_trigger_notif;

int emac_ctrl_fe_register_notifier(struct notifier_block *nb)
{
	if (emac_ctrl_fe_ctx) {
		/*DMA Driver is now registered*/
		mutex_lock(&emac_ctrl_fe_ctx->emac_ctl_fe_lock);
		emac_ctrl_fe_ctx->emac_dma_drv_state = EMAC_CTRL_FE_DMA_DRV_REG;
		mutex_unlock(&emac_ctrl_fe_ctx->emac_ctl_fe_lock);
		EMAC_CTL_FE_INFO("Register for Event notification \n");
		/*process pending acks*/
		if (emac_ctrl_fe_ctx->emac_ctrl_fe_state >=
			EMAC_CTRL_FE_EVENT_LISTN_INIT) {
			/*process notification sequence so far*/
			EMAC_CTL_FE_INFO("Schedule Event notifications \n");
			schedule_delayed_work(&emac_ctrl_fe_trigger_notif, msecs_to_jiffies(5) );
		}
	}

	return atomic_notifier_chain_register(&emac_ctrl_fe_notifier_chain, nb);
}
EXPORT_SYMBOL_GPL(emac_ctrl_fe_register_notifier);

int emac_ctrl_fe_unregister_notifier(struct notifier_block *nb)
{
	if (emac_ctrl_fe_ctx) {
		mutex_lock(&emac_ctrl_fe_ctx->emac_ctl_fe_lock);
		emac_ctrl_fe_ctx->emac_dma_drv_state = EMAC_CTRL_FE_DMA_DRV_UNREG;
		mutex_unlock(&emac_ctrl_fe_ctx->emac_ctl_fe_lock);

		emac_ctrl_fe_ctx->tx_msg.type = VIRTIO_EMAC_DMA_VIRT_UNREG_EVENTS;
		emac_ctrl_fe_ctx->tx_msg.len = sizeof(struct emac_ctrl_fe_to_be_virtio_msg);
		emac_ctl_fe_xmit(emac_ctrl_fe_ctx);
		EMAC_CTL_FE_INFO("Sent UnRegister Event Cmd \n");
	}

	/*Parse notifier block for suspend state*/
	cancel_delayed_work_sync(&emac_ctrl_fe_trigger_notif);

	return atomic_notifier_chain_unregister(&emac_ctrl_fe_notifier_chain, nb);
}
EXPORT_SYMBOL_GPL(emac_ctrl_fe_unregister_notifier);
/*EXPORT_SYMBOL is also same define #define EXPORT_SYMBOL_GPL(sym) extern typeof(sym) sym*/
/*GPL will show the symbol only in GPL-licensed modules and EXPORT_SYMBOL in modules with any license*/


/**
 * struct emac_ctrl_fe_cb_info is a List of all registrations for an
 * indication of EMAC Ctrl FE driver readiness.
 *
 * We expect only 1 client for near future unless there is ask to support
 * multiple DMA per VM with multiple driver.
*/
struct emac_ctrl_fe_cb_info {
	struct list_head link;
	emac_ctrl_fe_ready_cb ready_cb;
	void *user_data;
};


/*Derived from ipa driver*/
int emac_ctrl_fe_register_ready_cb(void (*emac_ctrl_fe_ready_cb)(void *user_data),
	void *user_data)
{
	int ret = 0;

	if (emac_ctrl_fe_ctx && emac_ctrl_fe_ctx->emac_ctrl_fe_ready) {
		/*call the callback*/
		EMAC_CTL_FE_INFO("Trigger FE Ready CB\n");
		if (emac_ctrl_fe_ready_cb)
			emac_ctrl_fe_ready_cb(user_data);

	}
	else {
		EMAC_CTL_FE_INFO("FE Not Support or Ready Yet\n");
		ret = -EINVAL;
	}
	return ret;
}
EXPORT_SYMBOL(emac_ctrl_fe_register_ready_cb);

/*This is to be called at end of probe once basic init is done */
static inline  void __maybe_unused emac_ctrl_fe_ready_trigger_cb(void)
{
	struct emac_ctrl_fe_cb_info *info;
	struct emac_ctrl_fe_cb_info *next;

	/* Call all the CBs */
	list_for_each_entry_safe(info, next,
		&emac_ctrl_fe_ctx->emac_ctrl_fe_ready_cb_list, link) {
		if (info->ready_cb)
			info->ready_cb(info->user_data);

		list_del(&info->link);
		kfree(info);
	}
}

void __maybe_unused emac_ctrl_fe_gvm_dma_stopped(void){
	EMAC_CTL_FE_INFO("Relay DMA STOP ACK to Host");
	emac_ctrl_fe_ctx->tx_msg.type = VIRTIO_EMAC_DMA_STOP_ACK;
	emac_ctrl_fe_ctx->tx_msg.len = sizeof(struct emac_ctrl_fe_to_be_virtio_msg);
	emac_ctl_fe_xmit(emac_ctrl_fe_ctx);
	//return;
}

/* request filter addition at EMAC HW*/
int __maybe_unused emac_ctrl_fe_filter_add_request(enum emac_ctrl_fe_filter_types filter_type,
	union emac_ctrl_fe_filter *filter) {

	if (!emac_ctrl_fe_ctx)
		return -EINVAL;
	mutex_lock(&emac_ctrl_fe_ctx->emac_ctl_fe_lock);

	EMAC_CTL_FE_INFO("Fe Filter Add Req %d", filter_type);
	switch (filter_type) {
	case UNICAST_FILTER:
		EMAC_CTL_FE_INFO("Fe Unicast Mac Filter Add Req");
		if (!is_valid_ether_addr(filter->unicast_mac.enm_addr)) {
			EMAC_CTL_FE_ERR("invalid ethernet address:\n");
			//return -EINVAL;
		}

		EMAC_CTL_FE_INFO("Requesting Filter for multicast MacAddr: %pM\n", filter->unicast_mac.enm_addr);
		emac_ctrl_fe_ctx->tx_msg.type = VIRTIO_EMAC_DMA_VIRT_UNICAST_MAC_ADD;
		memcpy(&(emac_ctrl_fe_ctx->tx_msg.request_data.unicast_mac),
			&filter->unicast_mac, ETH_ALEN);
		emac_ctrl_fe_ctx->tx_msg.len = sizeof(struct emac_ctrl_fe_to_be_virtio_msg);
		emac_ctl_fe_xmit(emac_ctrl_fe_ctx);
		break;

	case MULTICAST_FILTER:
		EMAC_CTL_FE_INFO("Fe Multicast Mac Filter Add Req");
		if (!is_multicast_ether_addr(filter->multi_mac.enm_addr)) {
			EMAC_CTL_FE_ERR("invalid multicast address:\n");
			//return -EINVAL;
		}

		EMAC_CTL_FE_INFO("Requesting Filter for multicast MacAddr: %pM\n", filter->multi_mac.enm_addr);
		emac_ctrl_fe_ctx->tx_msg.type = VIRTIO_EMAC_DMA_VIRT_MULTICAST_ADD;
		memcpy(emac_ctrl_fe_ctx->tx_msg.request_data.multi_mac.enm_addr,
			filter->multi_mac.enm_addr, ETH_ALEN);
		emac_ctrl_fe_ctx->tx_msg.len = sizeof(struct emac_ctrl_fe_to_be_virtio_msg);
		emac_ctl_fe_xmit(emac_ctrl_fe_ctx);
		break;

	case VLAN_FILTER:
		EMAC_CTL_FE_INFO("Fe Vlan Filter Add Req");
		if (!IS_VLAN_ID_VALID(filter->vlan_id) ) {
			EMAC_CTL_FE_ERR("invalid vlan_id %d:\n", filter->vlan_id);
			//return -EINVAL;
		}

		EMAC_CTL_FE_INFO("Requesting Vlan filter Add: %d\n", filter->vlan_id);
		emac_ctrl_fe_ctx->tx_msg.type = VIRTIO_EMAC_DMA_VIRT_VLAN_FTR_ADD;
		emac_ctrl_fe_ctx->tx_msg.request_data.vlan_id =filter->vlan_id;
		emac_ctrl_fe_ctx->tx_msg.len = sizeof(struct emac_ctrl_fe_to_be_virtio_msg);
		emac_ctl_fe_xmit(emac_ctrl_fe_ctx);
		break;

	default:
		EMAC_CTL_FE_WARN("Unknown Filter Request %d ", filter_type);
		break;
	}

	mutex_unlock(&emac_ctrl_fe_ctx->emac_ctl_fe_lock);
	return 0;
}

/* request filter deletion at EMAC HW*/
int __maybe_unused emac_ctrl_fe_filter_del_request(enum emac_ctrl_fe_filter_types filter_type,
	union emac_ctrl_fe_filter filter) {

	if (!emac_ctrl_fe_ctx)
		return -EINVAL;

	mutex_lock(&emac_ctrl_fe_ctx->emac_ctl_fe_lock);

	EMAC_CTL_FE_INFO("Relay Filter Del req");
	switch (filter_type) {
	case MULTICAST_FILTER:
		EMAC_CTL_FE_INFO("Fe Multicast Mac Filter Add Req");
		if (!is_multicast_ether_addr(filter.multi_mac.enm_addr)) {
			EMAC_CTL_FE_ERR("invalid multicast address:\n");
			//return -EINVAL;
		}

		EMAC_CTL_FE_INFO( "Requesting Filter for multicast MacAddr: %pM\n", filter.multi_mac);
		emac_ctrl_fe_ctx->tx_msg.type = VIRTIO_EMAC_DMA_VIRT_MULTICAST_DEL;
		memcpy(&(emac_ctrl_fe_ctx->tx_msg.request_data.multi_mac),
			&filter.multi_mac, ETH_ALEN);
		emac_ctrl_fe_ctx->tx_msg.len = sizeof(struct emac_ctrl_fe_to_be_virtio_msg);
		emac_ctl_fe_xmit(emac_ctrl_fe_ctx);
		break;
	case UNICAST_FILTER:
		EMAC_CTL_FE_INFO("Fe Unicast Mac Filter Add Req");
		if (!is_valid_ether_addr(filter.unicast_mac.enm_addr)) {
			EMAC_CTL_FE_ERR("invalid ethernet address:\n");
			//return -EINVAL;
		}

		EMAC_CTL_FE_INFO("Requesting Filter for multicast MacAddr: %pM\n",
			filter.unicast_mac.enm_addr);
		emac_ctrl_fe_ctx->tx_msg.type = VIRTIO_EMAC_DMA_VIRT_UNICAST_MAC_DEL;
		memcpy(&emac_ctrl_fe_ctx->tx_msg.request_data.unicast_mac,
			&filter.unicast_mac, ETH_ALEN);
		emac_ctrl_fe_ctx->tx_msg.len = sizeof(struct emac_ctrl_fe_to_be_virtio_msg);
		emac_ctl_fe_xmit(emac_ctrl_fe_ctx);
		break;

	case VLAN_FILTER:
		EMAC_CTL_FE_INFO("Fe Vlan Filter Del Req");
		if (!IS_VLAN_ID_VALID(filter.vlan_id) ) {
			EMAC_CTL_FE_ERR("invalid vlan_id %d:\n", filter.vlan_id);
			//return -EINVAL;
		}

		EMAC_CTL_FE_INFO("Requesting Vlan filter Del: %d\n", filter.vlan_id);
		emac_ctrl_fe_ctx->tx_msg.type = VIRTIO_EMAC_DMA_VIRT_VLAN_FTR_DEL;
		emac_ctrl_fe_ctx->tx_msg.request_data.vlan_id =filter.vlan_id;
		emac_ctrl_fe_ctx->tx_msg.len = sizeof(struct emac_ctrl_fe_to_be_virtio_msg);
		emac_ctl_fe_xmit(emac_ctrl_fe_ctx);
		break;

	default:
		EMAC_CTL_FE_WARN("Unknown Filter Request %d ", filter_type);
		break;
	}

	mutex_unlock(&emac_ctrl_fe_ctx->emac_ctl_fe_lock);
	return 0;
}

/* request mac_addr_chg_req*/
int __maybe_unused emac_ctrl_fe_mac_addr_chg(struct unicast_mac_addr *new_mac_addr) {

	EMAC_CTL_FE_INFO("Relay Fe Mac Addr Req");
	if (!is_valid_ether_addr(new_mac_addr->enm_addr)) {
		EMAC_CTL_FE_ERR("invalid ethernet address:\n");
		//return -EINVAL;
	}

	EMAC_CTL_FE_INFO("Virt Eth Mac Addr Req: %pM\n", new_mac_addr->enm_addr);

	emac_ctrl_fe_ctx->tx_msg.type = VIRTIO_EMAC_DMA_VIRT_UNICAST_MAC_ADD;


	memcpy(&(emac_ctrl_fe_ctx->tx_msg.request_data.unicast_mac),
		new_mac_addr, ETH_ALEN);

	emac_ctrl_fe_ctx->tx_msg.len = sizeof(struct emac_ctrl_fe_to_be_virtio_msg);

	emac_ctl_fe_xmit(emac_ctrl_fe_ctx);
	EMAC_CTL_FE_INFO("Sent MAC Addr Update Event Cmd \n");
	return 0;
}

void emac_ctl_fe_replenish_rxbuf(
	struct emac_ctrl_fe_virtio_dev *pdev,
	struct emac_ctrl_be_to_fe_virtio_msg *msg
)
{
	struct scatterlist sg[1];
	EMAC_CTL_FE_DBG("Entry");
	//spin_lock_irqsave(&pdev->rxq_lock, flags);

	memset(msg, 0x0, sizeof(*msg));
	sg_init_one(sg, msg, sizeof(*msg));
	/*expose input buffers to other end*/
	virtqueue_add_inbuf(pdev->emac_ctl_rxq, sg, 1, msg, GFP_ATOMIC);
	/* instead of doing frequent kicks can kick 1 time in end of outer loop*/
	//virtqueue_kick(pdev->emac_ctl_rxq);
	//spin_unlock_irqrestore(&pdev->rxq_lock, flags);
}

void emac_ctl_fe_process_rxbuf(
	struct emac_ctrl_fe_virtio_dev *pdev,
	struct emac_ctrl_be_to_fe_virtio_msg *msg,
	unsigned int len
)
{
	enum emac_dma_drv_state_type   dma_drv_state = EMAC_CTRL_FE_DMA_DRV_DOWN;
	EMAC_CTL_FE_INFO("Receive len =%u, cmd= %d, msgid =%d, msg len = %d ",
		len, msg->cmd, msg->msgid, msg->len);

	/*todo add lock for state read*/
	mutex_lock(&pdev->emac_ctl_fe_lock);
	dma_drv_state = pdev->emac_dma_drv_state;
	mutex_unlock(&pdev->emac_ctl_fe_lock);
	if (dma_drv_state == EMAC_CTRL_FE_DMA_DRV_REG) {
		switch (msg->cmd) {

		case EMAC_HW_DOWN:
			EMAC_CTL_FE_INFO("Notify EMAC_HW_DOWN");
			emac_ctrl_fe_notify(EMAC_HW_DOWN, -1, NULL);
			pdev->emac_ctrl_fe_state = EMAC_CTRL_FE_HW_DOWN_NOTIFIED;
			break;

		case EMAC_HW_UP:
			EMAC_CTL_FE_INFO("Notify EMAC_HW_UP");
			emac_ctrl_fe_notify(EMAC_HW_UP, -1, NULL);
			pdev->emac_ctrl_fe_state = EMAC_CTRL_FE_HW_UP_NOTIFIED;
			break;

		case EMAC_LINK_DOWN:
			EMAC_CTL_FE_INFO("Notify EMAC_LINK_DOWN");
			emac_ctrl_fe_notify(EMAC_LINK_DOWN, -1, NULL);
			pdev->emac_ctrl_fe_state = EMAC_CTRL_FE_LINK_DOWN_NOTIFIED;
			break;

		case EMAC_LINK_UP:
			EMAC_CTL_FE_INFO("Notify EMAC_LINK_UP");
			emac_ctrl_fe_notify(EMAC_LINK_UP, -1, NULL);
			pdev->emac_ctrl_fe_state = EMAC_CTRL_FE_LINK_UP_NOTIFIED;
			break;

		case EMAC_DMA_INT_STS_AVAIL:
			EMAC_CTL_FE_INFO("Notify EMAC_DMA_INT_STS_AVAIL");
			emac_ctrl_fe_notify(EMAC_DMA_INT_STS_AVAIL, -1, NULL);
			break;

		default:
			EMAC_CTL_FE_WARN("Received cmd %d not recognized ",  msg->cmd);
			break;

		}
	}
	else {
		EMAC_CTL_FE_WARN("Received cmd %d in unregestered State ",  msg->cmd);
	}
}

/* This is similar to RX complete interrupt
   this seems like single kick but need to treat as if_start*/
static void emac_ctl_fe_recv_done(struct virtqueue *rvq)
{
	struct emac_ctrl_fe_virtio_dev *pdev = rvq->vdev->priv;
	struct emac_ctrl_be_to_fe_virtio_msg *msg;
	unsigned long flags;
	unsigned int len;
	EMAC_CTL_FE_DBG("Entry");

	while (1) {
		spin_lock_irqsave(&pdev->rxq_lock, flags);
		EMAC_CTL_FE_DBG("Call Virtqueue_get_buff");
		msg = virtqueue_get_buf(pdev->emac_ctl_rxq, &len);
		if (!msg) {
			spin_unlock_irqrestore(&pdev->rxq_lock, flags);
			EMAC_CTL_FE_ERR("incoming signal, but no used buffer\n");
			break;
		}
		EMAC_CTL_FE_INFO("Got Buffer len %d ", len);
		spin_unlock_irqrestore(&pdev->rxq_lock, flags);
		/*process recieved message  Can Be stubbed out*/
		emac_ctl_fe_process_rxbuf(pdev, msg , len);

		/*Reclaim RX buffer*/
		emac_ctl_fe_replenish_rxbuf(pdev, msg);
		//virtqueue_kick(pdev->emac_ctl_rxq);  try
		/*Signal if using barrier Sync*/
	}/*while*/

	spin_lock_irqsave(&pdev->rxq_lock, flags);
	virtqueue_kick(pdev->emac_ctl_rxq);
	spin_unlock_irqrestore(&pdev->rxq_lock, flags);
}

static int emac_ctrl_fe_init_vqs(struct emac_ctrl_fe_virtio_dev *pdev)
{
	struct virtqueue *vqs[EMAC_CTRL_FE_VIRTQ_NUM];
	static const char *const names[] = { "emac_ctl_tx", "emac_ctl_rx" };
	vq_callback_t *cbs[] = {NULL, emac_ctl_fe_recv_done};
	int ret;

	/* Find VirtQueues and Register callback*/
	ret = virtio_find_vqs(pdev->vdev,
				EMAC_CTRL_FE_VIRTQ_NUM,
				vqs,
				cbs,
				names,
				NULL);
	if (ret) {
		EMAC_CTL_FE_ERR("virtio_find_vqs failed\n");
		return ret;
	}

	EMAC_CTL_FE_INFO("VirtQ Callback Reg Complete \n");

	/*Initialize TX VQ*/
	spin_lock_init(&pdev->txq_lock);
	pdev->emac_ctl_txq = vqs[EMAC_CTRL_FE_TX_VQ];

	/*Initialized RX VQ*/
	spin_lock_init(&pdev->rxq_lock);
	pdev->emac_ctl_rxq = vqs[EMAC_CTRL_FE_RX_VQ];

	EMAC_CTL_FE_INFO("VirtQ Init Complete \n");
	return 0;
}

static void emac_ctrl_fe_allocate_rxbufs(struct emac_ctrl_fe_virtio_dev *pdev)
{
	unsigned long flags;
	int i, size;

	spin_lock_irqsave(&pdev->rxq_lock, flags);
	size = virtqueue_get_vring_size(pdev->emac_ctl_rxq);
	if (size > ARRAY_SIZE(pdev->rx_msg))
		size = ARRAY_SIZE(pdev->rx_msg);
	for (i = 0; i < size; i++)
		emac_ctl_fe_replenish_rxbuf(pdev, &pdev->rx_msg[i]);
	//virtqueue_kick(pdev->emac_ctl_rxq);
	spin_unlock_irqrestore(&pdev->rxq_lock, flags);
}

static int emac_ctrl_fe_probe(struct virtio_device *vdev)
{
	int ret;
	struct emac_ctrl_fe_virtio_dev *pdev;

	EMAC_CTL_FE_DBG("\n");
	EMAC_CTL_FE_INFO("Start Probe allocate devm\n");

	/*Resource Managed kzalloc and memory allocated with this
	function is auto freed on driver detach https://lwn.net/Articles/222860/
	allocations are cleaned up automatically shall the probe itself fail*/
	pdev = devm_kzalloc(&vdev->dev, sizeof(*pdev), GFP_KERNEL);
	if (!pdev) {
		ret = -ENOMEM;
		goto fail;
	}

	mutex_init(&pdev->emac_ctl_fe_lock);

	emac_ctrl_fe_ctx = pdev;
	vdev->priv = pdev;
	pdev->vdev = vdev;
	pdev->name = "emac_ctrl_fe";

	/*Initialize States*/
	pdev->emac_ctrl_fe_state = EMAC_CTRL_FE_DOWN;
	pdev->emac_dma_drv_state = EMAC_CTRL_FE_DMA_DRV_DOWN;


	EMAC_CTL_FE_INFO("Init VQS \n");
	/*Allocate and Initialize RX and TX VirtQueues*/
	ret = emac_ctrl_fe_init_vqs(pdev);

	/*enable vq use in probe function*/
	virtio_device_ready(vdev);

	EMAC_CTL_FE_INFO("Allocate RXBufs \n");
	emac_ctrl_fe_allocate_rxbufs(pdev);

	/* Disable TX Complete ISR*/
	virtqueue_disable_cb(pdev->emac_ctl_txq);

	/*Enable Rx Complete ISR*/
	virtqueue_enable_cb(pdev->emac_ctl_rxq);
	/* Kick Host*/
	virtqueue_kick(pdev->emac_ctl_rxq);
	EMAC_CTL_FE_INFO("Kicked Host VirtQ \n");
	pdev->emac_ctrl_fe_state = EMAC_CTRL_FE_EVENT_LISTN_INIT;
/*
#ifdef EMAC_CTL_FE_DEBUGFS
	pdev->debugfs_root = debugfs_create_dir("emac_ctl_fe", NULL);
	debugfs_create_file("log", 00400 | 00200,
			pdev->debugfs_root, priv,
			&fops_debugfs_log);
	debugfs_create_file("timeout", 00400 | 00200,
			pdev->debugfs_root, NULL,
			&fops_debugfs_timeout);
#endif
*/

	/* Initialize Control Path*/

	/*Signal FE DMA Thin driver EMAC_FE_CTRL_DRV is ready*/
	pdev->emac_ctrl_fe_ready = true;
	INIT_DEFERRABLE_WORK(&emac_ctrl_fe_trigger_notif, emac_ctrl_fe_trigger_notif_work);
#ifdef CONFIG_QGKI_MSM_BOOT_TIME_MARKER
	place_marker("M - DRIVER EMAC_CTRL_FE Ready");
#endif
	return 0;
	/*
alloc_rxbufs_fail:
	//device_destroy(pdev->class, MKDEV(MAJOR(pdev->dev_no), MINOR_NUM_DEV));
device_create_fail:

	if (!IS_ERR_OR_NULL(dev))
		device_destroy(pdev->class, MKDEV(MAJOR(pdev->dev_no),
				MINOR_NUM_DEV));
	class_destroy(pdev->class);

class_create_fail:
	//cdev_del(&pdev->cdev);
free_chrdev:
	//unregister_chrdev_region(pdev->dev_no, DEVICE_NUM);
free_priv:
	//kfree(pdev);//not needed with managed allocation
	*/
fail:
	return ret;

}
/*
static void emac_ctrl_fe_remove(struct virtio_device *vdev)
{
	struct emac_ctrl_fe_virtio_dev *pdev;

	EMAC_CTL_FE_DBG("\n");
	pdev = vdev->priv;

#ifdef EAVB_DEBUGFS
	debugfs_remove_recursive(pdev->debugfs_root);
#endif
	device_destroy(pdev->class, MKDEV(MAJOR(pdev->dev_no), MINOR_NUM_DEV));
	class_destroy(pdev->class);
	cdev_del(&pdev->cdev);
	unregister_chrdev_region(pdev->dev_no, DEVICE_NUM);

	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);
	kfree(pdev->rxbufs[0]);
	kfree(pdev);
}
*/

const static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_EMAC_CTL_FE, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	/*none*/
};

static struct virtio_driver emac_ctrl_fe_virtio_drv = {
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
		//.pm = &emac_ctrl_fe_pm_ops,
	},
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.id_table = id_table,
	.probe = emac_ctrl_fe_probe,
	//.shutdown = ,
	//.remove = emac_ctrl_fe_remove,
	//.freeze = ,
	//.restore = ,
};

static int __init emac_ctrl_fe_init(void)
{
#ifdef CONFIG_QGKI_MSM_BOOT_TIME_MARKER
	place_marker("M - DRIVER EMAC_CTRL_FE Init");
#endif
	pr_err("%s: Module Entry \n", __func__);
	return register_virtio_driver(&emac_ctrl_fe_virtio_drv);

}


static void __exit emac_ctrl_fe_exit(void)
{
	unregister_virtio_driver(&emac_ctrl_fe_virtio_drv);
}

module_init(emac_ctrl_fe_init);
module_exit(emac_ctrl_fe_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Emac Virt DMA Ctrl FE Driver");
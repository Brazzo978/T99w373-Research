/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/debugfs.h>

#include "ioss_i.h"

static struct dentry *root_dir;
static struct dentry *devices_dir;

int ioss_debugfs_init(void)
{
	if (root_dir)
		return 0;

	root_dir = debugfs_create_dir(IOSS_SUBSYS, 0);
	if (IS_ERR_OR_NULL(root_dir)) {
		ioss_log_err(NULL, "Failed to create root debugfs directory for IOSS");
		goto fail;
	}

	devices_dir = debugfs_create_dir("devices", root_dir);
	if (IS_ERR_OR_NULL(devices_dir)) {
		ioss_log_err(NULL, "Failed to create devices debugfs directory for IOSS");
		goto fail;
	}

	return 0;

fail:
	debugfs_remove_recursive(root_dir);
	root_dir = NULL;
	devices_dir = NULL;
	return -EFAULT;
}

void ioss_debugfs_exit(void)
{
	debugfs_remove_recursive(root_dir);
	root_dir = NULL;
	devices_dir = NULL;
}

static int get_idev_statistics(struct ioss_device *idev,
		struct ioss_device_stats *stats)
{
	int rc;
	struct ioss_channel *ch;
	struct ioss_interface *iface = &idev->interface;
	struct rtnl_link_stats64 netdev_stats;

	memset(stats, 0, sizeof(struct ioss_device_stats));

	/* Fetch EMAC level statistics */
	rc = ioss_dev_op(idev, get_device_statistics, idev, stats);
	if (rc) {
		ioss_dev_err(idev, "Failed to get device statistics");
		return -EFAULT;
	}

	/* Aggregate channel level stats */
	ioss_for_each_channel(ch, iface) {
		struct ioss_channel_stats ch_stats;

		memset(&ch_stats, 0, sizeof(struct ioss_channel_stats));

		if (ioss_dev_op(idev, get_channel_statistics, ch, &ch_stats)) {
			ioss_dev_err(idev, "Failed to get channel statistics");
			return -EFAULT;
		}

		if (ch->direction == IOSS_CH_DIR_RX) {
			stats->hwp_rx_errors += ch_stats.overflow_error +
					ch_stats.underflow_error;
		}

		if (ch->direction == IOSS_CH_DIR_TX) {
			stats->hwp_tx_errors += ch_stats.overflow_error +
					ch_stats.underflow_error;
		}
	}

	/* Fetch Linux netdev stats */
	memset(&netdev_stats, 0, sizeof(struct rtnl_link_stats64));
	dev_get_stats(ioss_iface_to_netdev(iface), &netdev_stats);

	stats->exp_rx_packets += iface->exception_stats.rx_packets;
	stats->exp_rx_bytes += iface->exception_stats.rx_bytes;

	if (stats->emac_rx_packets)
		stats->hwp_rx_packets = stats->emac_rx_packets +
				stats->exp_rx_packets - netdev_stats.rx_packets;
	if (stats->emac_tx_packets)
		stats->hwp_tx_packets = stats->emac_tx_packets +
				stats->exp_tx_packets - netdev_stats.tx_packets;
	if (stats->emac_rx_bytes)
		stats->hwp_rx_bytes = stats->emac_rx_bytes +
				stats->exp_rx_bytes - netdev_stats.rx_bytes;
	if (stats->emac_tx_bytes)
		stats->hwp_tx_bytes = stats->emac_tx_bytes +
				stats->exp_tx_bytes - netdev_stats.tx_bytes;
	stats->hwp_rx_drops = stats->emac_rx_drops;

	return 0;
}

static ssize_t read_idev_statistics(struct file *file, char __user *user_buf,
		size_t size, loff_t *ppos)
{
	char *buf;
	size_t len = 0;
	const size_t BUF_LEN = 3000;
	ssize_t ret_cnt = 0;
	struct ioss_device *idev = file->private_data;
	struct ioss_device_stats dev_stats;

	if (get_idev_statistics(idev, &dev_stats)) {
		ioss_dev_err(idev, "Failed to get idev statistics");
		return -EFAULT;
	}

	buf = kmalloc(BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "hwp_rx_packets",
			  dev_stats.hwp_rx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "hwp_tx_packets",
			 dev_stats.hwp_tx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "hwp_rx_bytes",
			 dev_stats.hwp_rx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "hwp_tx_bytes",
			 dev_stats.hwp_tx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "hwp_rx_errors",
			 dev_stats.hwp_rx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "hwp_tx_errors",
			 dev_stats.hwp_tx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "hwp_rx_drops",
			 dev_stats.hwp_rx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "hwp_tx_drops",
			 dev_stats.hwp_tx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "exp_rx_packets",
			 dev_stats.exp_rx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "exp_tx_packets",
			 dev_stats.exp_tx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "exp_rx_bytes",
			 dev_stats.exp_rx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "exp_tx_bytes",
			 dev_stats.exp_tx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "exp_rx_errors",
			 dev_stats.exp_rx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "exp_tx_errors",
			 dev_stats.exp_tx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "exp_rx_drops",
			 dev_stats.exp_rx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "exp_tx_drops",
			 dev_stats.exp_tx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_rx_packets",
			 dev_stats.emac_rx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_tx_packets",
			 dev_stats.emac_tx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_rx_bytes",
			 dev_stats.emac_rx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_tx_bytes",
			 dev_stats.emac_tx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_rx_errors",
			 dev_stats.emac_rx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_tx_errors",
			 dev_stats.emac_tx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_rx_drops",
			 dev_stats.emac_rx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_tx_drops",
			 dev_stats.emac_tx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_rx_pause_frames",
			 dev_stats.emac_rx_pause_frames);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "emac_tx_pause_frames",
			 dev_stats.emac_tx_pause_frames);

	ret_cnt = simple_read_from_buffer(user_buf, size, ppos, buf, len);
	kfree(buf);

	return ret_cnt;
}

static ssize_t read_idev_stats(struct file *file, char __user *user_buf, size_t size, loff_t *ppos)
{
	char *buf;
	size_t len = 0;
	const size_t BUF_LEN = 3000;
	ssize_t ret_cnt = 0;
	struct ioss_device *idev = file->private_data;
	struct ioss_device_stats dev_stats;

	if (get_idev_statistics(idev, &dev_stats)) {
		ioss_dev_err(idev, "Failed to get idev statistics");
		return -EFAULT;
	}

	buf = kmalloc(BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.hwp_rx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.hwp_tx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.hwp_rx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.hwp_tx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.hwp_rx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.hwp_tx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.hwp_rx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.hwp_tx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.exp_rx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.exp_tx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.exp_rx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.exp_tx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.exp_rx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.exp_tx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.exp_rx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.exp_tx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_rx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_tx_packets);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_rx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_tx_bytes);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_rx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_tx_errors);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_rx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_tx_drops);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", dev_stats.emac_rx_pause_frames);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu", dev_stats.emac_tx_pause_frames);
	len += scnprintf(buf + len, BUF_LEN - len, "\n");

	ret_cnt = simple_read_from_buffer(user_buf, size, ppos, buf, len);
	kfree(buf);

	return ret_cnt;
}

static ssize_t read_ch_statistics(struct file *file, char __user *user_buf,
		size_t size, loff_t *ppos)
{
	char *buf;
	size_t len = 0;
	const size_t BUF_LEN = 3000;
	ssize_t ret_cnt = 0;
	struct ioss_channel *ch = file->private_data;
	struct ioss_device *idev = ioss_ch_dev(ch);
	struct ioss_channel_stats ch_stats;

	memset(&ch_stats, 0, sizeof(struct ioss_channel_stats));

	if (ioss_dev_op(idev, get_channel_statistics, ch, &ch_stats)) {
		ioss_dev_err(idev, "Failed to get channel statistics");
		return -EFAULT;
	}

	buf = kmalloc(BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "overflow_errors",
			 ch_stats.overflow_error);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "underflow_errors",
			 ch_stats.underflow_error);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "desc_unavail",
			 ch_stats.desc_unavail);

	ret_cnt = simple_read_from_buffer(user_buf, size, ppos, buf, len);
	kfree(buf);

	return ret_cnt;
}

static ssize_t read_ch_stats(struct file *file, char __user *user_buf, size_t size, loff_t *ppos)
{
	char *buf;
	size_t len = 0;
	const size_t BUF_LEN = 3000;
	ssize_t ret_cnt = 0;
	struct ioss_channel *ch = file->private_data;
	struct ioss_channel_stats ch_stats;
	struct ioss_device *idev = ioss_ch_dev(ch);

	memset(&ch_stats, 0, sizeof(struct ioss_channel_stats));

	if (ioss_dev_op(idev, get_channel_statistics, ch, &ch_stats)) {
		ioss_dev_err(idev, "Failed to get channel stats");
		return -EFAULT;
	}

	buf = kmalloc(BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", ch_stats.overflow_error);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", ch_stats.underflow_error);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", ch_stats.desc_unavail);
	len += scnprintf(buf + len, BUF_LEN - len, "\n");

	ret_cnt = simple_read_from_buffer(user_buf, size, ppos, buf, len);
	kfree(buf);

	return ret_cnt;
}

static ssize_t read_ch_status(struct file *file, char __user *user_buf, size_t size, loff_t *ppos)
{
	char *buf;
	size_t len = 0;
	const size_t BUF_LEN = 3000;
	ssize_t ret_cnt = 0;
	struct ioss_channel *ch = file->private_data;
	struct ioss_device *idev = ioss_ch_dev(ch);
	struct ioss_channel_status ch_status;

	memset(&ch_status, 0, sizeof(struct ioss_channel_status));

	if (ioss_dev_op(idev, get_channel_status, ch, &ch_status)) {
		ioss_dev_err(idev, "Failed to get channel status");
		return -EFAULT;
	}

	buf = kmalloc(BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, BUF_LEN - len, "%s: %d\n", "enabled", ch_status.enabled);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "size", ch_status.ring_size);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "interrupt_modc",
			 ch_status.interrupt_modc);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu ns\n", "interrupt_modt",
			 ch_status.interrupt_modt);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "head_ptr", ch_status.head_ptr);
	len += scnprintf(buf + len, BUF_LEN - len, "%s: %llu\n", "tail_ptr", ch_status.tail_ptr);

	ret_cnt = simple_read_from_buffer(user_buf, size, ppos, buf, len);
	kfree(buf);

	return ret_cnt;
}

static ssize_t read_ch_stat(struct file *file, char __user *user_buf, size_t size, loff_t *ppos)
{
	char *buf;
	size_t len = 0;
	const size_t BUF_LEN = 3000;
	ssize_t ret_cnt = 0;
	struct ioss_channel *ch = file->private_data;
	struct ioss_device *idev = ioss_ch_dev(ch);
	struct ioss_channel_status ch_status;

	memset(&ch_status, 0, sizeof(struct ioss_channel_status));

	if (ioss_dev_op(idev, get_channel_status, ch, &ch_status)) {
		ioss_dev_err(idev, "Failed to get channel status");
		return -EFAULT;
	}

	buf = kmalloc(BUF_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	len += scnprintf(buf + len, BUF_LEN - len, "%d ", ch_status.enabled);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", ch_status.ring_size);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", ch_status.interrupt_modc);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", ch_status.interrupt_modt);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu ", ch_status.head_ptr);
	len += scnprintf(buf + len, BUF_LEN - len, "%llu", ch_status.tail_ptr);
	len += scnprintf(buf + len, BUF_LEN - len, "\n");

	ret_cnt = simple_read_from_buffer(user_buf, size, ppos, buf, len);
	kfree(buf);

	return ret_cnt;
}


static const struct file_operations fops_idev_statistics = {
	.read = read_idev_statistics,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static const struct file_operations fops_idev_stats = {
	.read = read_idev_stats,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static const struct file_operations fops_ch_statistics = {
	.read = read_ch_statistics,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static const struct file_operations fops_ch_stats = {
	.read = read_ch_stats,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static const struct file_operations fops_ch_status = {
	.read = read_ch_status,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

static const struct file_operations fops_ch_stat = {
	.read = read_ch_stat,
	.open = simple_open,
	.owner = THIS_MODULE,
	.llseek = default_llseek,
};

int ioss_debugfs_add_idev(struct ioss_device *idev)
{
	struct dentry *statistics;
	struct dentry *stats;

	idev->debugfs = debugfs_create_dir(idev->net_dev->name, devices_dir);
	if (IS_ERR_OR_NULL(idev->debugfs)) {
		ioss_dev_err(idev, "Failed to create %s debugfs directory", idev->net_dev->name);
		goto err_debugfs;
	}

	statistics = debugfs_create_file("statistics", 0444, idev->debugfs, idev,
		     &fops_idev_statistics);
	if (IS_ERR_OR_NULL(statistics)) {
		ioss_dev_err(idev, "Failed to create debugfs file for %s", idev->net_dev->name);
		goto err_debugfs;
	}

	stats = debugfs_create_file("stats", 0444, idev->debugfs, idev, &fops_idev_stats);
	if (IS_ERR_OR_NULL(stats)) {
		ioss_dev_err(idev, "Failed to create debugfs file for %s", idev->net_dev->name);
		goto err_debugfs;
	}

	return 0;

err_debugfs:
	debugfs_remove_recursive(idev->debugfs);
	idev->debugfs = NULL;
	return -EFAULT;
}

void ioss_debugfs_remove_idev(struct ioss_device *idev)
{
	debugfs_remove_recursive(idev->debugfs);
	idev->debugfs = NULL;
}

int ioss_debugfs_add_channel(struct ioss_channel *ch)
{
	struct dentry *statistics;
	struct dentry *stats;
	struct dentry *status;
	struct dentry *stat;

	char dir_name[32];
	struct ioss_device *idev = ioss_ch_dev(ch);

	snprintf(dir_name, sizeof(dir_name), "%s-%d",
		 ((ch->direction == IOSS_CH_DIR_RX) ? "rx" : "tx"), ch->id);

	ch->debugfs = debugfs_create_dir(dir_name, idev->debugfs);
	if (IS_ERR_OR_NULL(ch->debugfs)) {
		ioss_dev_err(idev, "Failed to create %s debugfs directory", dir_name);
		goto err_debugfs;
	}

	statistics = debugfs_create_file("statistics", 0444, ch->debugfs, ch, &fops_ch_statistics);
	if (IS_ERR_OR_NULL(statistics)) {
		ioss_dev_err(idev, "Failed to create statistics debugfs file for %s", dir_name);
		goto err_debugfs;
	}

	stats = debugfs_create_file("stats", 0444, ch->debugfs, ch, &fops_ch_stats);
	if (IS_ERR_OR_NULL(stats)) {
		ioss_dev_err(idev, "Failed to create stats debugfs file for %s", dir_name);
		goto err_debugfs;
	}

	status = debugfs_create_file("status", 0444, ch->debugfs, ch, &fops_ch_status);
	if (IS_ERR_OR_NULL(status)) {
		ioss_dev_err(idev, "Failed to create status debugfs file for %s", dir_name);
		goto err_debugfs;
	}

	stat = debugfs_create_file("stat", 0444, ch->debugfs, ch, &fops_ch_stat);
	if (IS_ERR_OR_NULL(stat)) {
		ioss_dev_err(idev, "Failed to create stat debugfs file for %s", dir_name);
		goto err_debugfs;
	}

	return 0;

err_debugfs:
	debugfs_remove_recursive(ch->debugfs);
	ch->debugfs = NULL;
	return -EFAULT;
}

void ioss_debugfs_remove_channel(struct ioss_channel *ch)
{
	debugfs_remove_recursive(ch->debugfs);
	ch->debugfs = NULL;
}


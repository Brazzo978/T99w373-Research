#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/msm_ipa.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* rmnet_data0 sane destination pipe seen in working route logs. */
#define RMNET_DST_PIPE_FALLBACK 33
#define DST_PIPE_SANITY_MAX 1024
#define MAX_TX_PROPS_SANITY 16

static int (*real_ioctl_fn)(int, unsigned long, void *);

static void log_fix(const char *ifname, uint32_t idx, uint32_t old_dst, uint32_t new_dst)
{
	int fd = open("/usrdata/ipacm_fix.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd < 0)
		return;
	dprintf(fd, "[ipacm_fix] if=%s tx[%u] dst_pipe %u -> %u\n", ifname, idx, old_dst, new_dst);
	close(fd);
}

static bool is_target_iface(const char *name)
{
	if (!name)
		return false;
	if (strncmp(name, "rmnet_data0", IPA_RESOURCE_NAME_MAX) == 0)
		return true;
	if (strncmp(name, "eth0", IPA_RESOURCE_NAME_MAX) == 0)
		return true;
	return false;
}

static uint32_t pick_fallback_dst(const struct ipa_ioc_query_intf_tx_props *q)
{
	if (!q)
		return RMNET_DST_PIPE_FALLBACK;
	if (strncmp(q->name, "rmnet_data0", IPA_RESOURCE_NAME_MAX) == 0)
		return RMNET_DST_PIPE_FALLBACK;
	/* For eth0, prefer tx[0] if already sane, otherwise reuse rmnet fallback. */
	if (q->num_tx_props > 0) {
		uint32_t d = (uint32_t)q->tx[0].dst_pipe;
		if (d > 0 && d < DST_PIPE_SANITY_MAX)
			return d;
	}
	return RMNET_DST_PIPE_FALLBACK;
}

static void sanitize_query_tx_props(struct ipa_ioc_query_intf_tx_props *q)
{
	uint32_t i;
	uint32_t fallback;

	if (!q)
		return;
	if (!is_target_iface(q->name))
		return;
	if (q->num_tx_props == 0 || q->num_tx_props > MAX_TX_PROPS_SANITY)
		return;

	fallback = pick_fallback_dst(q);

	for (i = 0; i < q->num_tx_props; i++) {
		uint32_t dst = (uint32_t)q->tx[i].dst_pipe;
		if (dst == 0 || dst >= DST_PIPE_SANITY_MAX) {
			q->tx[i].dst_pipe = (enum ipa_client_type)fallback;
			log_fix(q->name, i, dst, fallback);
		}
		if ((uint32_t)q->tx[i].alt_dst_pipe >= DST_PIPE_SANITY_MAX)
			q->tx[i].alt_dst_pipe = 0;
		if ((uint32_t)q->tx[i].ip > IPA_IP_MAX)
			q->tx[i].ip = (i == 0) ? IPA_IP_v4 : IPA_IP_v6;
	}

	/* Preserve old dual-prop behavior expected by ipacm (v4 + v6). */
	if (q->num_tx_props >= 2 && (uint32_t)q->tx[1].dst_pipe == 0) {
		q->tx[1] = q->tx[0];
		q->tx[1].ip = IPA_IP_v6;
		log_fix(q->name, 1, 0, (uint32_t)q->tx[1].dst_pipe);
	}
}

int ioctl(int fd, unsigned long request, ...)
{
	va_list ap;
	void *arg;
	int ret;

	if (!real_ioctl_fn)
		real_ioctl_fn = (int (*)(int, unsigned long, void *))dlsym(RTLD_NEXT, "ioctl");

	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);

	ret = real_ioctl_fn(fd, request, arg);
	if (ret >= 0 && request == IPA_IOC_QUERY_INTF_TX_PROPS)
		sanitize_query_tx_props((struct ipa_ioc_query_intf_tx_props *)arg);

	return ret;
}

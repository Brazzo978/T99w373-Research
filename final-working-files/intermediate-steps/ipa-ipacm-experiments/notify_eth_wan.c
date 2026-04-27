#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

/* From msm_ipa.h uapi */
#define IPA_IOC_MAGIC              0xCF
#define IPA_RESOURCE_NAME_MAX      32
#define IPA_WAN_MSG_IPv6_ADDR_GW_LEN 4
#define IPA_IOCTL_NOTIFY_WAN_UPSTREAM_ROUTE_ADD 37

enum ipa_ip_type { IPA_IP_v4 = 0, IPA_IP_v6 = 1, IPA_IP_MAX };

struct ipa_wan_msg {
	char     upstream_ifname[IPA_RESOURCE_NAME_MAX];
	char     tethered_ifname[IPA_RESOURCE_NAME_MAX];
	enum ipa_ip_type ip;
	uint32_t ipv4_addr_gw;
	uint32_t ipv6_addr_gw[IPA_WAN_MSG_IPv6_ADDR_GW_LEN];
};

#define IPA_IOC_NOTIFY_WAN_UPSTREAM_ROUTE_ADD \
	_IOWR(IPA_IOC_MAGIC, IPA_IOCTL_NOTIFY_WAN_UPSTREAM_ROUTE_ADD, \
	      struct ipa_wan_msg *)

static uint32_t bswap32_local(uint32_t x)
{
	return ((x & 0x000000FFU) << 24) | ((x & 0x0000FF00U) << 8) |
	       ((x & 0x00FF0000U) >> 8)  | ((x & 0xFF000000U) >> 24);
}

static int find_default_gw_be(const char *ifname, uint32_t *gw_be_out)
{
	FILE *f;
	char line[256];
	int line_no = 0;

	f = fopen("/proc/net/route", "r");
	if (!f) { perror("fopen /proc/net/route"); return 0; }

	while (fgets(line, sizeof(line), f)) {
		char iface[IFNAMSIZ];
		uint32_t dst, gw_le, flags;
		line_no++;
		if (line_no == 1) continue;
		if (sscanf(line, "%15s %x %x %x", iface, &dst, &gw_le, &flags) < 4)
			continue;
		if (dst == 0 && strncmp(iface, ifname, IFNAMSIZ) == 0) {
			*gw_be_out = bswap32_local(gw_le);
			fclose(f);
			return 1;
		}
	}
	fclose(f);
	return 0;
}

int main(int argc, char *argv[])
{
	const char *upstream = (argc > 1) ? argv[1] : "rmnet_data0";
	const char *tethered = (argc > 2) ? argv[2] : "eth0";
	struct ipa_wan_msg msg;
	uint32_t gw_be = 0;
	int fd, ret;

	printf("notify_eth_wan: upstream=%s tethered=%s\n", upstream, tethered);

	if (!find_default_gw_be(upstream, &gw_be)) {
		fprintf(stderr, "No default route on %s\n", upstream);
		return 1;
	}
	printf("  GW (BE): 0x%08x\n", gw_be);

	fd = open("/dev/ipa", O_RDWR);
	if (fd < 0) { perror("open /dev/ipa"); return 1; }

	memset(&msg, 0, sizeof(msg));
	strncpy(msg.upstream_ifname, upstream, IPA_RESOURCE_NAME_MAX - 1);
	strncpy(msg.tethered_ifname, tethered, IPA_RESOURCE_NAME_MAX - 1);
	msg.ip = IPA_IP_v4;
	msg.ipv4_addr_gw = gw_be;

	ret = ioctl(fd, IPA_IOC_NOTIFY_WAN_UPSTREAM_ROUTE_ADD, &msg);
	if (ret < 0) perror("IPA_IOC_NOTIFY_WAN_UPSTREAM_ROUTE_ADD");
	else printf("  OK (ret=%d)\n", ret);

	close(fd);
	return (ret < 0) ? 1 : 0;
}

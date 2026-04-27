#include <asm/unistd.h>
#include <linux/msm_ipa.h>
#include <stdint.h>

static inline long sys_open(const char *path, int flags, int mode)
{
	register long r7 __asm__("r7") = __NR_open;
	register long r0 __asm__("r0") = (long)path;
	register long r1 __asm__("r1") = flags;
	register long r2 __asm__("r2") = mode;
	__asm__ __volatile__("svc 0" : "=r"(r0) : "r"(r7), "0"(r0), "r"(r1), "r"(r2) : "r3", "lr", "memory");
	return r0;
}

static inline long sys_read(int fd, void *buf, unsigned long count)
{
	register long r7 __asm__("r7") = __NR_read;
	register long r0 __asm__("r0") = fd;
	register long r1 __asm__("r1") = (long)buf;
	register long r2 __asm__("r2") = count;
	__asm__ __volatile__("svc 0" : "=r"(r0) : "r"(r7), "0"(r0), "r"(r1), "r"(r2) : "r3", "lr", "memory");
	return r0;
}

static inline long sys_write(int fd, const void *buf, unsigned long count)
{
	register long r7 __asm__("r7") = __NR_write;
	register long r0 __asm__("r0") = fd;
	register long r1 __asm__("r1") = (long)buf;
	register long r2 __asm__("r2") = count;
	__asm__ __volatile__("svc 0" : "=r"(r0) : "r"(r7), "0"(r0), "r"(r1), "r"(r2) : "r3", "lr", "memory");
	return r0;
}

static inline long sys_close(int fd)
{
	register long r7 __asm__("r7") = __NR_close;
	register long r0 __asm__("r0") = fd;
	__asm__ __volatile__("svc 0" : "=r"(r0) : "r"(r7), "0"(r0) : "r1", "r2", "r3", "lr", "memory");
	return r0;
}

static inline long sys_ioctl(int fd, unsigned long req, void *arg)
{
	register long r7 __asm__("r7") = __NR_ioctl;
	register long r0 __asm__("r0") = fd;
	register long r1 __asm__("r1") = (long)req;
	register long r2 __asm__("r2") = (long)arg;
	__asm__ __volatile__("svc 0" : "=r"(r0) : "r"(r7), "0"(r0), "r"(r1), "r"(r2) : "r3", "lr", "memory");
	return r0;
}

static inline void sys_exit(int code)
{
	register long r7 __asm__("r7") = __NR_exit;
	register long r0 __asm__("r0") = code;
	__asm__ __volatile__("svc 0" : : "r"(r7), "r"(r0) : "memory");
	for (;;)
		;
}

static int str_eq(const char *a, const char *b, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		if (a[i] != b[i])
			return 0;
		if (a[i] == '\0')
			return 1;
	}
	return 1;
}

static int hex_nibble(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int parse_hex32_token(const char *s, int *advance, uint32_t *out)
{
	int i = 0;
	int n;
	int digits = 0;
	uint32_t v = 0;
	while (s[i] == ' ' || s[i] == '\t') i++;
	for (; i < 64; i++) {
		n = hex_nibble(s[i]);
		if (n < 0) break;
		v = (v << 4) | (uint32_t)n;
		digits++;
	}
	if (digits == 0) return 0;
	*advance = i;
	*out = v;
	return 1;
}

static uint32_t bswap32_local(uint32_t x)
{
	return ((x & 0x000000FFU) << 24) |
	       ((x & 0x0000FF00U) << 8) |
	       ((x & 0x00FF0000U) >> 8) |
	       ((x & 0xFF000000U) >> 24);
}

static int find_default_gw_be(const char *ifname, uint32_t *gw_be_out)
{
	char buf[4096];
	int fd;
	long nread;
	int i = 0;
	int line_no = 0;

	fd = (int)sys_open("/proc/net/route", 0, 0);
	if (fd < 0) return 0;
	nread = sys_read(fd, buf, sizeof(buf) - 1);
	sys_close(fd);
	if (nread <= 0) return 0;
	buf[nread] = '\0';

	while (i < nread) {
		int line_start = i;
		int line_end = i;
		char iface[16];
		int k = 0;
		int pos;
		uint32_t dst = 0;
		uint32_t gw_le = 0;

		while (line_end < nread && buf[line_end] != '\n') line_end++;
		i = line_end + 1;
		line_no++;
		if (line_no == 1) continue;

		while (line_start < line_end && buf[line_start] != ' ' && buf[line_start] != '\t' && k < 15)
			iface[k++] = buf[line_start++];
		iface[k] = '\0';
		if (k == 0) continue;

		pos = line_start;
		if (!parse_hex32_token(&buf[pos], &k, &dst)) continue;
		pos += k;
		if (!parse_hex32_token(&buf[pos], &k, &gw_le)) continue;

		if (dst == 0 && str_eq(iface, ifname, 16)) {
			*gw_be_out = bswap32_local(gw_le);
			return 1;
		}
	}
	return 0;
}

static void copy_name32(char dst[IPA_RESOURCE_NAME_MAX], const char *src)
{
	int i;
	for (i = 0; i < IPA_RESOURCE_NAME_MAX; i++) {
		char c = src[i];
		dst[i] = c;
		if (c == '\0') break;
	}
	for (; i < IPA_RESOURCE_NAME_MAX; i++) dst[i] = '\0';
}

void _start(void)
{
	struct ipa_wan_msg msg;
	uint8_t *p = (uint8_t *)&msg;
	uint32_t gw = 0;
	int i;
	int fd;
	long ret;
	const char ok[] = "wan_up_add:OK\n";
	const char no[] = "wan_up_add:FAIL\n";

	for (i = 0; i < (int)sizeof(msg); i++) p[i] = 0;
	if (!find_default_gw_be("rmnet_data0", &gw)) {
		sys_write(1, no, sizeof(no) - 1);
		sys_exit(2);
	}

	copy_name32(msg.upstream_ifname, "rmnet_data0");
	copy_name32(msg.tethered_ifname, "eth0");
	msg.ip = IPA_IP_v4;
	msg.ipv4_addr_gw = gw;

	fd = (int)sys_open("/dev/ipa", 2, 0);
	if (fd < 0) {
		sys_write(1, no, sizeof(no) - 1);
		sys_exit(3);
	}

	ret = sys_ioctl(fd, IPA_IOC_NOTIFY_WAN_UPSTREAM_ROUTE_ADD, &msg);
	sys_close(fd);
	if (ret >= 0) {
		sys_write(1, ok, sizeof(ok) - 1);
		sys_exit(0);
	}
	sys_write(1, no, sizeof(no) - 1);
	sys_exit(4);
}

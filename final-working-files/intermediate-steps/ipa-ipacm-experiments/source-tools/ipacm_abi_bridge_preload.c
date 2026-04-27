#define _GNU_SOURCE
#include <asm/unistd.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <linux/msm_ipa.h>

#define LOG_PATH "/usrdata/ipa_abi_bridge.log"
#define OLD_TX_SIZE 200U
#define OLD_RX_SIZE 164U
#define OLD_ATTR_SIZE 152U
#define QUERY_HDR_SIZE 36U
#define MAX_PROPS 16U

static inline long sys_ioctl(int fd, unsigned long req, void *arg)
{
	register long r7 __asm__("r7") = __NR_ioctl;
	register long r0 __asm__("r0") = fd;
	register long r1 __asm__("r1") = (long)req;
	register long r2 __asm__("r2") = (long)arg;
	__asm__ __volatile__("svc 0" : "=r"(r0) : "r"(r7), "0"(r0), "r"(r1), "r"(r2) : "r3", "lr", "memory");
	return r0;
}

static inline long sys_open(const char *path, int flags, int mode)
{
	register long r7 __asm__("r7") = __NR_open;
	register long r0 __asm__("r0") = (long)path;
	register long r1 __asm__("r1") = flags;
	register long r2 __asm__("r2") = mode;
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

static inline long sys_readlink(const char *path, char *buf, unsigned long count)
{
	register long r7 __asm__("r7") = __NR_readlink;
	register long r0 __asm__("r0") = (long)path;
	register long r1 __asm__("r1") = (long)buf;
	register long r2 __asm__("r2") = count;
	__asm__ __volatile__("svc 0" : "=r"(r0) : "r"(r7), "0"(r0), "r"(r1), "r"(r2) : "r3", "lr", "memory");
	return r0;
}

static void mem_zero(uint8_t *p, unsigned int n)
{
	unsigned int i;
	for (i = 0; i < n; i++) p[i] = 0;
}

static void mem_copy(uint8_t *d, const uint8_t *s, unsigned int n)
{
	unsigned int i;
	for (i = 0; i < n; i++) d[i] = s[i];
}

static int str_eq_32(const char *a, const char *b)
{
	int i;
	for (i = 0; i < IPA_RESOURCE_NAME_MAX; i++) {
		char ca = a[i];
		char cb = b[i];
		if (ca != cb) return 0;
		if (ca == '\0') return 1;
	}
	return 1;
}

static int str_eq_n(const char *a, const char *b, int max_n)
{
	int i;
	for (i = 0; i < max_n; i++) {
		char ca = a[i];
		char cb = b[i];
		if (ca != cb) return 0;
		if (ca == '\0') return 1;
	}
	return 1;
}

static int target_if(const char *name)
{
	return str_eq_32(name, "eth0") || str_eq_32(name, "rmnet_data0") || str_eq_32(name, "rmnet_data1");
}

static int build_fd_link_path(int fd, char out[32])
{
	char rev[12];
	unsigned int n = 0, i, v;
	static const char pre[] = "/proc/self/fd/";
	for (i = 0; i < sizeof(pre) - 1; i++) out[i] = pre[i];
	v = (unsigned int)fd;
	do {
		if (n >= sizeof(rev)) return 0;
		rev[n++] = (char)('0' + (v % 10U));
		v /= 10U;
	} while (v);
	for (i = 0; i < n; i++) out[sizeof(pre) - 1 + i] = rev[n - 1 - i];
	out[sizeof(pre) - 1 + n] = '\0';
	return 1;
}

static int fd_is_ipa_dev(int fd)
{
	char path[32];
	char link[64];
	long n;
	static int cached_fd = -1;
	static int cached_is_ipa;
	if (fd == cached_fd) return cached_is_ipa;
	cached_fd = fd;
	cached_is_ipa = 0;
	if (!build_fd_link_path(fd, path)) return 0;
	n = sys_readlink(path, link, sizeof(link) - 1);
	if (n <= 0) return 0;
	link[n] = '\0';
	if (str_eq_n(link, "/dev/ipa", 8)) cached_is_ipa = 1;
	return cached_is_ipa;
}

static void log_line(const char *tag, const char *name, uint32_t n)
{
	int fd, i;
	static const char hx[] = "0123456789abcdef";
	char num[10];
	num[0] = '0'; num[1] = 'x';
	for (i = 0; i < 8; i++) num[2 + i] = hx[(n >> (28 - i * 4)) & 0xf];
	fd = (int)sys_open(LOG_PATH, 0x441, 0600);
	if (fd < 0) return;
	for (i = 0; tag[i]; i++); sys_write(fd, tag, i);
	sys_write(fd, "[", 1);
	if (name) { for (i = 0; i < IPA_RESOURCE_NAME_MAX && name[i]; i++); sys_write(fd, name, i); }
	sys_write(fd, "]", 1);
	sys_write(fd, num, 10);
	sys_write(fd, "\n", 1);
	sys_close(fd);
}

static void translate_tx_02300_to_new(struct ipa_ioc_query_intf_tx_props *q)
{
	uint32_t n, idx;
	uint8_t tmp[OLD_TX_SIZE];
	uint8_t *base;
	uint32_t new_tx_size = (uint32_t)sizeof(struct ipa_ioc_tx_intf_prop);
	uint32_t new_attr_size = (uint32_t)offsetof(struct ipa_ioc_tx_intf_prop, dst_pipe) - 4U;

	if (!q || !target_if(q->name)) return;
	n = q->num_tx_props;
	if (n == 0 || n > MAX_PROPS) return;
	if (new_tx_size == OLD_TX_SIZE) return;
	base = (uint8_t *)&q->tx[0];
	for (idx = n; idx > 0; idx--) {
		uint32_t i = idx - 1;
		uint8_t *oldp = base + i * OLD_TX_SIZE;
		uint8_t *newp = base + i * new_tx_size;
		mem_copy(tmp, oldp, OLD_TX_SIZE);
		mem_zero(newp, new_tx_size);
		mem_copy(newp + offsetof(struct ipa_ioc_tx_intf_prop, ip), tmp + 0, 4);
		mem_copy(newp + offsetof(struct ipa_ioc_tx_intf_prop, attrib), tmp + 4, OLD_ATTR_SIZE < new_attr_size ? OLD_ATTR_SIZE : new_attr_size);
		mem_copy(newp + offsetof(struct ipa_ioc_tx_intf_prop, dst_pipe), tmp + 156, 4);
		mem_copy(newp + offsetof(struct ipa_ioc_tx_intf_prop, alt_dst_pipe), tmp + 160, 4);
		mem_copy(newp + offsetof(struct ipa_ioc_tx_intf_prop, hdr_name), tmp + 164, IPA_RESOURCE_NAME_MAX);
		mem_copy(newp + offsetof(struct ipa_ioc_tx_intf_prop, hdr_l2_type), tmp + 196, 4);
	}
	log_line("TX_XLAT", q->name, n);
}

static void translate_rx_02300_to_new(struct ipa_ioc_query_intf_rx_props *q)
{
	uint32_t n, idx;
	uint8_t tmp[OLD_RX_SIZE];
	uint8_t *base;
	uint32_t new_rx_size = (uint32_t)sizeof(struct ipa_ioc_rx_intf_prop);
	uint32_t new_attr_size = (uint32_t)offsetof(struct ipa_ioc_rx_intf_prop, src_pipe) - 4U;

	if (!q || !target_if(q->name)) return;
	n = q->num_rx_props;
	if (n == 0 || n > MAX_PROPS) return;
	if (new_rx_size == OLD_RX_SIZE) return;
	base = (uint8_t *)&q->rx[0];
	for (idx = n; idx > 0; idx--) {
		uint32_t i = idx - 1;
		uint8_t *oldp = base + i * OLD_RX_SIZE;
		uint8_t *newp = base + i * new_rx_size;
		mem_copy(tmp, oldp, OLD_RX_SIZE);
		mem_zero(newp, new_rx_size);
		mem_copy(newp + offsetof(struct ipa_ioc_rx_intf_prop, ip), tmp + 0, 4);
		mem_copy(newp + offsetof(struct ipa_ioc_rx_intf_prop, attrib), tmp + 4, OLD_ATTR_SIZE < new_attr_size ? OLD_ATTR_SIZE : new_attr_size);
		mem_copy(newp + offsetof(struct ipa_ioc_rx_intf_prop, src_pipe), tmp + 156, 4);
		mem_copy(newp + offsetof(struct ipa_ioc_rx_intf_prop, hdr_l2_type), tmp + 160, 4);
	}
	log_line("RX_XLAT", q->name, n);
}

int ioctl(int fd, unsigned long request, ...)
{
	va_list ap;
	void *arg;
	long ret;
	unsigned long ioc_type;
	unsigned long ioc_nr;

	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);
	ioc_type = (request >> _IOC_TYPESHIFT) & _IOC_TYPEMASK;
	ioc_nr = (request >> _IOC_NRSHIFT) & _IOC_NRMASK;
	if (ioc_type == IPA_IOC_MAGIC && !fd_is_ipa_dev(fd))
		return (int)sys_ioctl(fd, request, arg);
	ret = sys_ioctl(fd, request, arg);
	if (ret >= 0 && ioc_type == IPA_IOC_MAGIC && arg) {
		if (ioc_nr == IPA_IOCTL_QUERY_INTF_TX_PROPS)
			translate_tx_02300_to_new((struct ipa_ioc_query_intf_tx_props *)arg);
		else if (ioc_nr == IPA_IOCTL_QUERY_INTF_RX_PROPS)
			translate_rx_02300_to_new((struct ipa_ioc_query_intf_rx_props *)arg);
	}
	return (int)ret;
}

#define _GNU_SOURCE
#include <asm/unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <linux/msm_ipa.h>

#define MAX_TX 8

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
static inline long sys_close(int fd)
{
	register long r7 __asm__("r7") = __NR_close;
	register long r0 __asm__("r0") = fd;
	__asm__ __volatile__("svc 0" : "=r"(r0) : "r"(r7), "0"(r0) : "r1", "r2", "r3", "lr", "memory");
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
static inline void sys_exit(int code)
{
	register long r7 __asm__("r7") = __NR_exit;
	register long r0 __asm__("r0") = code;
	__asm__ __volatile__("svc 0" : : "r"(r7), "r"(r0) : "r1", "r2", "r3", "lr", "memory");
	for (;;)
		;
}

static void zero_mem(void *p, unsigned int n)
{
	unsigned int i;
	uint8_t *b = (uint8_t *)p;
	for (i = 0; i < n; i++) b[i] = 0;
}
static void copy_name_32(char dst[IPA_RESOURCE_NAME_MAX], const char *src)
{
	int i;
	for (i = 0; i < IPA_RESOURCE_NAME_MAX; i++) {
		char c = src[i];
		dst[i] = c;
		if (c == '\0') break;
	}
	for (; i < IPA_RESOURCE_NAME_MAX; i++) dst[i] = '\0';
}
static void put_str(const char *s)
{
	int n = 0; while (s[n]) n++; sys_write(1, s, (unsigned long)n);
}
static void put_hex32(uint32_t v)
{
	static const char hx[] = "0123456789abcdef";
	char out[10]; int i;
	out[0]='0'; out[1]='x';
	for (i=0;i<8;i++) out[2+i]=hx[(v>>(28-i*4))&0xF];
	sys_write(1,out,10);
}
static void put_byte_hex(uint8_t v)
{
	static const char hx[] = "0123456789abcdef";
	char out[2]; out[0]=hx[(v>>4)&0xF]; out[1]=hx[v&0xF];
	sys_write(1,out,2);
}
static void kv(const char *k, uint32_t v)
{
	put_str(k); put_str("="); put_hex32(v); put_str("\n");
}

struct query_tx_req {
	struct ipa_ioc_query_intf_tx_props q;
	struct ipa_ioc_tx_intf_prop tx[MAX_TX];
};

static int run_probe(void)
{
	struct query_tx_req req;
	long ret;
	int fd;
	unsigned int i, j;

	fd = (int)sys_open("/dev/ipa", 2, 0);
	if (fd < 0) {
		kv("open_ret", (uint32_t)(unsigned int)fd);
		return 2;
	}

	zero_mem(&req, sizeof(req));
	copy_name_32(req.q.name, "rmnet_data0");
	req.q.num_tx_props = MAX_TX;

	ret = sys_ioctl(fd, IPA_IOC_QUERY_INTF_TX_PROPS, &req.q);
	kv("ioctl_ret", (uint32_t)(unsigned int)ret);
	put_str("if="); put_str(req.q.name); put_str("\n");
	kv("num_tx", req.q.num_tx_props);

	for (i = 0; i < req.q.num_tx_props && i < MAX_TX; i++) {
		kv("idx", i);
		kv("dst", (uint32_t)req.tx[i].dst_pipe);
		kv("alt", (uint32_t)req.tx[i].alt_dst_pipe);
		kv("l2", (uint32_t)req.tx[i].hdr_l2_type);
		kv("ip", (uint32_t)req.tx[i].ip);
		put_str("hdr_name_str="); put_str(req.tx[i].hdr_name); put_str("\n");
		put_str("hdr_name_raw=");
		for (j = 0; j < IPA_RESOURCE_NAME_MAX; j++)
			put_byte_hex((uint8_t)req.tx[i].hdr_name[j]);
		put_str("\n");
	}

	sys_close(fd);
	return 0;
}

void _start(void)
{
	int rc = run_probe();
	sys_exit(rc);
}

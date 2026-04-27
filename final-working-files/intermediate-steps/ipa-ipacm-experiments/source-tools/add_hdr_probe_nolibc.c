#define _GNU_SOURCE
#include <asm/unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <linux/msm_ipa.h>

#define INVALID_HDR_HDL 0xffffffffU

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
	for (i = 0; i < n; i++)
		b[i] = 0;
}

static void copy_name_32(char dst[IPA_RESOURCE_NAME_MAX], const char *src)
{
	int i;
	for (i = 0; i < IPA_RESOURCE_NAME_MAX; i++) {
		char c = src[i];
		dst[i] = c;
		if (c == '\0')
			break;
	}
	for (; i < IPA_RESOURCE_NAME_MAX; i++)
		dst[i] = '\0';
}

static void put_str(const char *s)
{
	int n = 0;
	while (s[n])
		n++;
	sys_write(1, s, (unsigned long)n);
}

static void put_hex32(uint32_t v)
{
	static const char hx[] = "0123456789abcdef";
	char out[10];
	int i;
	out[0] = '0'; out[1] = 'x';
	for (i = 0; i < 8; i++)
		out[2 + i] = hx[(v >> (28 - i * 4)) & 0xF];
	sys_write(1, out, 10);
}

static void put_kv_hex(const char *k, uint32_t v)
{
	put_str(k);
	put_str("=");
	put_hex32(v);
	put_str("\n");
}

struct add_hdr_req_one {
	struct ipa_ioc_add_hdr req;
	struct ipa_hdr_add hdr;
};

struct del_hdr_req_one {
	struct ipa_ioc_del_hdr req;
	struct ipa_hdr_del h;
};

static int run_probe(void)
{
	struct add_hdr_req_one add;
	struct ipa_ioc_get_hdr get;
	struct del_hdr_req_one del;
	const char *name = "probe_addhdr_v4";
	long ret;
	int fd;
	uint32_t del_hdl;

	fd = (int)sys_open("/dev/ipa", 2, 0);
	if (fd < 0) {
		put_kv_hex("open_ret", (uint32_t)(unsigned int)fd);
		return 2;
	}

	zero_mem(&add, sizeof(add));
	add.req.commit = 1;
	add.req.num_hdrs = 1;
	copy_name_32(add.hdr.name, name);
	add.hdr.hdr_len = 14;
	add.hdr.type = IPA_HDR_L2_ETHERNET_II;
	add.hdr.is_partial = 1;
	add.hdr.is_eth2_ofst_valid = 0;
	add.hdr.eth2_ofst = 0;
	add.hdr.hdr[12] = 0x08;
	add.hdr.hdr[13] = 0x00;
	add.hdr.hdr_hdl = INVALID_HDR_HDL;
	add.hdr.status = -777;

	ret = sys_ioctl(fd, IPA_IOC_ADD_HDR, &add.req);
	put_kv_hex("add_ioctl_ret", (uint32_t)(unsigned int)ret);
	put_kv_hex("add_hdr_status", (uint32_t)(unsigned int)add.hdr.status);
	put_kv_hex("add_hdr_hdl", add.hdr.hdr_hdl);

	zero_mem(&get, sizeof(get));
	copy_name_32(get.name, name);
	ret = sys_ioctl(fd, IPA_IOC_GET_HDR, &get);
	put_kv_hex("get_ioctl_ret", (uint32_t)(unsigned int)ret);
	put_kv_hex("get_hdr_hdl", get.hdl);

	del_hdl = (get.hdl != 0) ? get.hdl : add.hdr.hdr_hdl;
	if (del_hdl != 0 && del_hdl != INVALID_HDR_HDL) {
		zero_mem(&del, sizeof(del));
		del.req.commit = 1;
		del.req.num_hdls = 1;
		del.h.hdl = del_hdl;
		del.h.status = -333;
		ret = sys_ioctl(fd, IPA_IOC_DEL_HDR, &del.req);
		put_kv_hex("del_ioctl_ret", (uint32_t)(unsigned int)ret);
		put_kv_hex("del_status", (uint32_t)(unsigned int)del.h.status);
	}

	sys_close(fd);
	return 0;
}

void _start(void)
{
	int rc = run_probe();
	sys_exit(rc);
}

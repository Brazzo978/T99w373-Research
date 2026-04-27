#define _GNU_SOURCE
#include <asm/unistd.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <linux/msm_ipa.h>

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

#define WAN_IOC_MAGIC 0x69
#define WAN_IOCTL_NOTIFY_WAN_STATE 11

#define RMNET_DST_PIPE_FALLBACK 35
#define RMNET_SRC_PIPE_FALLBACK 2
#define ETH_SRC_PIPE_FALLBACK 108
#define ETH_ALT_DST_PIPE_FALLBACK 52
#define DST_PIPE_SANITY_MAX 1024U
#define MAX_TX_PROPS_SANITY 16U
#define SYNTH_COPY_HDR_LEN 8U
#define ENABLE_UL_EP_REWRITE 0
#define INVALID_HDR_HDL 0xffffffffU
#define FORCE_FLT_ROUTE_OVERRIDE 1
#define FORCE_RT_DST_33_TO_35 1
#define ENABLE_RT_IOC_TOUCH 0
#define ENABLE_QUERY_IOC_SANITIZE 1
#define MAX_FLT_RULES_TRANSLATE 16U
#define MAX_RT_RULES_TRANSLATE 16U

struct wan_ioctl_notify_wan_state_local {
	uint8_t up;
	char upstreamIface[IFNAMSIZ];
};

struct ipa_flt_rule_legacy {
	uint8_t raw[360];
};

struct ipa_flt_rule_v2_legacy {
	uint8_t raw[364];
};

struct ipa_flt_rule_add_legacy {
	struct ipa_flt_rule_legacy rule;
	uint8_t at_rear;
	uint32_t flt_rule_hdl;
	int status;
};

struct ipa_flt_rule_add_v2_legacy {
	uint8_t at_rear;
	uint32_t flt_rule_hdl;
	int status;
	struct ipa_flt_rule_v2_legacy rule;
};

struct ipa_ioc_add_flt_rule_legacy_flat {
	uint8_t commit;
	enum ipa_ip_type ip;
	enum ipa_client_type ep;
	uint8_t global;
	uint8_t num_rules;
	struct ipa_flt_rule_add_legacy rules[MAX_FLT_RULES_TRANSLATE];
};

struct ipa_ioc_add_flt_rule_after_legacy_flat {
	uint8_t commit;
	enum ipa_ip_type ip;
	enum ipa_client_type ep;
	uint8_t num_rules;
	uint32_t add_after_hdl;
	struct ipa_flt_rule_add_legacy rules[MAX_FLT_RULES_TRANSLATE];
};

struct ipa_ioc_add_flt_rule_v2_legacy_ptr {
	uint8_t commit;
	enum ipa_ip_type ip;
	enum ipa_client_type ep;
	uint8_t global;
	uint8_t num_rules;
	uint32_t flt_rule_size;
	uint32_t reserved1;
	uint16_t reserved2;
	uint8_t reserved3;
	uint64_t rules;
};

struct ipa_rt_rule_legacy {
	uint8_t raw[168];
};

struct ipa_rt_rule_add_legacy {
	struct ipa_rt_rule_legacy rule;
	uint8_t at_rear;
	uint32_t rt_rule_hdl;
	int status;
};

struct ipa_ioc_add_rt_rule_legacy_flat {
	uint8_t commit;
	enum ipa_ip_type ip;
	char rt_tbl_name[IPA_RESOURCE_NAME_MAX];
	uint8_t num_rules;
	struct ipa_rt_rule_add_legacy rules[MAX_RT_RULES_TRANSLATE];
};

struct ipa_ioc_add_rt_rule_after_legacy_flat {
	uint8_t commit;
	enum ipa_ip_type ip;
	char rt_tbl_name[IPA_RESOURCE_NAME_MAX];
	uint8_t num_rules;
	uint32_t add_after_hdl;
	struct ipa_rt_rule_add_legacy rules[MAX_RT_RULES_TRANSLATE];
};

static void force_notify_wan_upstream(const char *up_ifname);
static int g_wan_force_done;
static uint32_t g_cached_v4_hdr_hdl;
static uint32_t g_cached_v6_hdr_hdl;
static uint32_t g_cached_v4_rt_tbl_hdl;
static uint32_t g_cached_v6_rt_tbl_hdl;
static int hex_nibble(char c);

static inline long sys_ioctl(int fd, unsigned long req, void *arg)
{
	register long r7 __asm__("r7") = __NR_ioctl;
	register long r0 __asm__("r0") = fd;
	register long r1 __asm__("r1") = (long)req;
	register long r2 __asm__("r2") = (long)arg;

	__asm__ __volatile__(
		"svc 0"
		: "=r"(r0)
		: "r"(r7), "0"(r0), "r"(r1), "r"(r2)
		: "r3", "lr", "memory");

	return r0;
}

static inline long sys_open(const char *path, int flags, int mode)
{
	register long r7 __asm__("r7") = __NR_open;
	register long r0 __asm__("r0") = (long)path;
	register long r1 __asm__("r1") = flags;
	register long r2 __asm__("r2") = mode;

	__asm__ __volatile__(
		"svc 0"
		: "=r"(r0)
		: "r"(r7), "0"(r0), "r"(r1), "r"(r2)
		: "r3", "lr", "memory");

	return r0;
}

static inline long sys_read(int fd, void *buf, unsigned long count)
{
	register long r7 __asm__("r7") = __NR_read;
	register long r0 __asm__("r0") = fd;
	register long r1 __asm__("r1") = (long)buf;
	register long r2 __asm__("r2") = count;

	__asm__ __volatile__(
		"svc 0"
		: "=r"(r0)
		: "r"(r7), "0"(r0), "r"(r1), "r"(r2)
		: "r3", "lr", "memory");

	return r0;
}

static inline long sys_readlink(const char *path, char *buf, unsigned long count)
{
	register long r7 __asm__("r7") = __NR_readlink;
	register long r0 __asm__("r0") = (long)path;
	register long r1 __asm__("r1") = (long)buf;
	register long r2 __asm__("r2") = count;

	__asm__ __volatile__(
		"svc 0"
		: "=r"(r0)
		: "r"(r7), "0"(r0), "r"(r1), "r"(r2)
		: "r3", "lr", "memory");

	return r0;
}

static inline long sys_close(int fd)
{
	register long r7 __asm__("r7") = __NR_close;
	register long r0 __asm__("r0") = fd;

	__asm__ __volatile__(
		"svc 0"
		: "=r"(r0)
		: "r"(r7), "0"(r0)
		: "r1", "r2", "r3", "lr", "memory");

	return r0;
}

static inline long sys_write(int fd, const void *buf, unsigned long count)
{
	register long r7 __asm__("r7") = __NR_write;
	register long r0 __asm__("r0") = fd;
	register long r1 __asm__("r1") = (long)buf;
	register long r2 __asm__("r2") = count;

	__asm__ __volatile__(
		"svc 0"
		: "=r"(r0)
		: "r"(r7), "0"(r0), "r"(r1), "r"(r2)
		: "r3", "lr", "memory");

	return r0;
}

#define LOG_PATH "/usrdata/ipa_fix_log.txt"

static void log_cstr(const char *tag, const char *s)
{
	int fd = (int)sys_open(LOG_PATH, 0x441, 0600); /* O_WRONLY|O_CREAT|O_APPEND */
	int i;
	if (fd < 0)
		return;
	for (i = 0; tag[i]; i++)
		;
	sys_write(fd, tag, (unsigned long)i);
	sys_write(fd, "[", 1);
	for (i = 0; i < IPA_RESOURCE_NAME_MAX && s[i]; i++)
		;
	sys_write(fd, s, (unsigned long)i);
	sys_write(fd, "]\n", 2);
	sys_close(fd);
}

static void log_u32hex(const char *tag, uint32_t val)
{
	static const char hx[] = "0123456789abcdef";
	char nbuf[10]; /* "0x" + 8 hex digits */
	int fd, tlen, i;

	nbuf[0] = '0'; nbuf[1] = 'x';
	for (i = 0; i < 8; i++)
		nbuf[2 + i] = hx[(val >> (28 - i * 4)) & 0xF];

	fd = (int)sys_open(LOG_PATH, 0x441, 0600);
	if (fd < 0)
		return;
	tlen = 0;
	while (tag[tlen])
		tlen++;
	sys_write(fd, tag, (unsigned long)tlen);
	sys_write(fd, "[", 1);
	sys_write(fd, nbuf, 10);
	sys_write(fd, "]\n", 2);
	sys_close(fd);
}

static void log_hex_bytes(const char *tag, const uint8_t *buf, unsigned int len)
{
	static const char hx[] = "0123456789abcdef";
	char hbuf[IPA_RESOURCE_NAME_MAX * 2];
	int fd, tlen, i;

	if (!buf)
		return;
	if (len > IPA_RESOURCE_NAME_MAX)
		len = IPA_RESOURCE_NAME_MAX;

	for (i = 0; i < (int)len; i++) {
		hbuf[i * 2] = hx[(buf[i] >> 4) & 0xF];
		hbuf[i * 2 + 1] = hx[buf[i] & 0xF];
	}

	fd = (int)sys_open(LOG_PATH, 0x441, 0600);
	if (fd < 0)
		return;
	tlen = 0;
	while (tag[tlen])
		tlen++;
	sys_write(fd, tag, (unsigned long)tlen);
	sys_write(fd, "[", 1);
	sys_write(fd, hbuf, (unsigned long)(len * 2));
	sys_write(fd, "]\n", 2);
	sys_close(fd);
}

static int str_eq_32(const char *a, const char *b)
{
	int i;
	for (i = 0; i < IPA_RESOURCE_NAME_MAX; i++) {
		char ca = a[i];
		char cb = b[i];
		if (ca != cb)
			return 0;
		if (ca == '\0')
			return 1;
	}
	return 1;
}

static int is_target_iface(const char *name)
{
	if (!name)
		return 0;
	if (str_eq_32(name, "rmnet_data0"))
		return 1;
	if (str_eq_32(name, "eth0"))
		return 1;
	return 0;
}

static int is_rmnet_iface(const char *name)
{
	return name && str_eq_32(name, "rmnet_data0");
}

static int is_eth_iface(const char *name)
{
	return name && str_eq_32(name, "eth0");
}

static int str_eq_n(const char *a, const char *b, int max_n)
{
	int i;
	for (i = 0; i < max_n; i++) {
		char ca = a[i];
		char cb = b[i];
		if (ca != cb)
			return 0;
		if (ca == '\0')
			return 1;
	}
	return 1;
}

static int build_fd_link_path(int fd, char out[32])
{
	char rev[12];
	unsigned int n = 0;
	unsigned int i = 0;
	unsigned int v;
	static const char pre[] = "/proc/self/fd/";
	unsigned int prelen = sizeof(pre) - 1;

	if (!out || fd < 0)
		return 0;

	for (i = 0; i < prelen; i++)
		out[i] = pre[i];
	v = (unsigned int)fd;
	do {
		if (n >= sizeof(rev))
			return 0;
		rev[n++] = (char)('0' + (v % 10U));
		v /= 10U;
	} while (v);
	for (i = 0; i < n; i++)
		out[prelen + i] = rev[n - 1 - i];
	out[prelen + n] = '\0';
	return 1;
}

static int fd_is_ipa_dev(int fd)
{
	char path[32];
	char link[64];
	long n;
	static int cached_fd = -1;
	static int cached_is_ipa;

	if (fd == cached_fd)
		return cached_is_ipa;
	cached_fd = fd;
	cached_is_ipa = 0;

	if (!build_fd_link_path(fd, path))
		return 0;
	n = sys_readlink(path, link, sizeof(link) - 1);
	if (n <= 0)
		return 0;
	link[n] = '\0';

	if (str_eq_n(link, "/dev/ipa", 8))
		cached_is_ipa = 1;
	return cached_is_ipa;
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

static void set_hdr_name_1(char hdr_name[IPA_RESOURCE_NAME_MAX])
{
	int i;
	hdr_name[0] = '1';
	for (i = 1; i < IPA_RESOURCE_NAME_MAX; i++)
		hdr_name[i] = '\0';
}

static void set_hdr_name_eth0_v4(char hdr_name[IPA_RESOURCE_NAME_MAX])
{
	copy_name_32(hdr_name, "eth0_ipv4");
}

static void set_hdr_name_eth0_v6(char hdr_name[IPA_RESOURCE_NAME_MAX])
{
	copy_name_32(hdr_name, "eth0_ipv6");
}

static int is_blank_name_32(const char name[IPA_RESOURCE_NAME_MAX])
{
	int i;
	for (i = 0; i < IPA_RESOURCE_NAME_MAX; i++) {
		char c = name[i];
		if (c == '\0')
			return 1;
		if (c != ' ' && c != '\t')
			return 0;
	}
	return 1;
}

static int read_eth0_mac(uint8_t mac[6])
{
	char buf[32];
	int fd;
	long nread;
	int i = 0;
	int m = 0;

	fd = (int)sys_open("/sys/class/net/eth0/address", 0, 0);
	if (fd < 0)
		return 0;
	nread = sys_read(fd, buf, sizeof(buf) - 1);
	sys_close(fd);
	if (nread <= 0)
		return 0;
	buf[nread] = '\0';

	while (m < 6 && buf[i]) {
		int hi, lo;
		while (buf[i] == ':' || buf[i] == ' ' || buf[i] == '\n' || buf[i] == '\t')
			i++;
		if (!buf[i] || !buf[i + 1])
			break;
		hi = hex_nibble(buf[i]);
		lo = hex_nibble(buf[i + 1]);
		if (hi < 0 || lo < 0)
			break;
		mac[m++] = (uint8_t)((hi << 4) | lo);
		i += 2;
	}
	return (m == 6);
}

static void synthesize_copy_hdr(struct ipa_ioc_copy_hdr *ch)
{
	unsigned int j;

	if (!ch)
		return;

	for (j = 0; j < IPA_HDR_MAX_SIZE; j++)
		ch->hdr[j] = 0;
	ch->hdr_len = (uint8_t)SYNTH_COPY_HDR_LEN;
	ch->type = (enum ipa_hdr_l2_type)0; /* IPA_HDR_L2_NONE */
	ch->is_partial = 1;
	ch->is_eth2_ofst_valid = 0;
	ch->eth2_ofst = 0;
}

static void synthesize_copy_hdr_eth(struct ipa_ioc_copy_hdr *ch, int ipv6)
{
	uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
	unsigned int j;

	if (!ch)
		return;

	(void)read_eth0_mac(mac);
	if (ipv6)
		set_hdr_name_eth0_v6(ch->name);
	else
		set_hdr_name_eth0_v4(ch->name);

	for (j = 0; j < IPA_HDR_MAX_SIZE; j++)
		ch->hdr[j] = 0;
	for (j = 0; j < 6; j++)
		ch->hdr[6 + j] = mac[j];
	ch->hdr[12] = ipv6 ? 0x86 : 0x08;
	ch->hdr[13] = ipv6 ? 0xdd : 0x00;

	ch->hdr_len = 14;
	ch->type = IPA_HDR_L2_ETHERNET_II;
	ch->is_partial = 1;
	ch->is_eth2_ofst_valid = 0;
	ch->eth2_ofst = 0;
}

static uint32_t pick_fallback_dst(const struct ipa_ioc_query_intf_tx_props *q)
{
	uint32_t d;
	if (!q)
		return RMNET_DST_PIPE_FALLBACK;
	if (is_rmnet_iface(q->name))
		return RMNET_DST_PIPE_FALLBACK;
	if (q->num_tx_props > 0) {
		d = (uint32_t)q->tx[0].dst_pipe;
		if (d > 0 && d < DST_PIPE_SANITY_MAX)
			return d;
	}
	return RMNET_DST_PIPE_FALLBACK;
}

static void log_query_tx_props_raw(struct ipa_ioc_query_intf_tx_props *q)
{
	unsigned int i;

	if (!q)
		return;
	if (!is_target_iface(q->name))
		return;
	if (q->num_tx_props > MAX_TX_PROPS_SANITY)
		return;

	log_cstr("QTX_if", q->name);
	log_u32hex("QTX_n", q->num_tx_props);
	for (i = 0; i < q->num_tx_props; i++) {
		log_u32hex("QTX_i", i);
		log_cstr("QTX_name", q->tx[i].hdr_name);
		log_hex_bytes("QTX_name_raw",
			(const uint8_t *)q->tx[i].hdr_name,
			IPA_RESOURCE_NAME_MAX);
		log_u32hex("QTX_dst", (uint32_t)q->tx[i].dst_pipe);
		log_u32hex("QTX_alt", (uint32_t)q->tx[i].alt_dst_pipe);
		log_u32hex("QTX_l2", (uint32_t)q->tx[i].hdr_l2_type);
		log_u32hex("QTX_ip", (uint32_t)q->tx[i].ip);
	}
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
		if (dst == 0 || dst >= DST_PIPE_SANITY_MAX)
			q->tx[i].dst_pipe = (enum ipa_client_type)fallback;

		if ((uint32_t)q->tx[i].alt_dst_pipe >= DST_PIPE_SANITY_MAX)
			q->tx[i].alt_dst_pipe = 0;

		q->tx[i].ip = (i == 0) ? IPA_IP_v4 : IPA_IP_v6;
			if (is_eth_iface(q->name)) {
				if (i == 0)
					set_hdr_name_eth0_v4(q->tx[i].hdr_name);
				else if (i == 1)
					set_hdr_name_eth0_v6(q->tx[i].hdr_name);
				else if (q->tx[i].hdr_name[0] == '\0' || q->tx[i].hdr_name[0] == ' ')
					set_hdr_name_eth0_v4(q->tx[i].hdr_name);
			}
		}

		if (q->num_tx_props >= 2 && (uint32_t)q->tx[1].dst_pipe == 0) {
			q->tx[1].dst_pipe = q->tx[0].dst_pipe;
			q->tx[1].alt_dst_pipe = q->tx[0].alt_dst_pipe;
			q->tx[1].ip = IPA_IP_v6;
			if (is_eth_iface(q->name))
				set_hdr_name_eth0_v6(q->tx[1].hdr_name);
		}

	/* Force a synthetic WAN_UPSTREAM_ROUTE_ADD using current default GW. */
	if (is_rmnet_iface(q->name))
		force_notify_wan_upstream(q->name);
}

static void sanitize_query_rx_props(struct ipa_ioc_query_intf_rx_props *q)
{
	uint32_t i;
	uint32_t fallback;

	if (!q)
		return;
	if (!is_target_iface(q->name))
		return;
	if (q->num_rx_props == 0 || q->num_rx_props > MAX_TX_PROPS_SANITY)
		return;

	fallback = is_rmnet_iface(q->name) ? RMNET_SRC_PIPE_FALLBACK : ETH_SRC_PIPE_FALLBACK;

	for (i = 0; i < q->num_rx_props; i++) {
		uint32_t src = (uint32_t)q->rx[i].src_pipe;
		if (src == 0 || src >= DST_PIPE_SANITY_MAX)
			q->rx[i].src_pipe = (enum ipa_client_type)fallback;
		q->rx[i].ip = (i == 0) ? IPA_IP_v4 : IPA_IP_v6;
	}
}

static void force_query_eth0_tx_props(struct ipa_ioc_query_intf_tx_props *q)
{
	unsigned int j;
	unsigned int i;

	if (!q || !is_eth_iface(q->name))
		return;

	q->num_tx_props = 2;
	for (i = 0; i < 2; i++) {
		uint8_t *p = (uint8_t *)&q->tx[i];
		for (j = 0; j < sizeof(q->tx[i]); j++)
			p[j] = 0;
	}

	q->tx[0].ip = IPA_IP_v4;
	q->tx[0].dst_pipe = (enum ipa_client_type)RMNET_DST_PIPE_FALLBACK;
	q->tx[0].alt_dst_pipe = (enum ipa_client_type)ETH_ALT_DST_PIPE_FALLBACK;
	q->tx[0].hdr_l2_type = IPA_HDR_L2_ETHERNET_II;
	set_hdr_name_eth0_v4(q->tx[0].hdr_name);

	q->tx[1].ip = IPA_IP_v6;
	q->tx[1].dst_pipe = (enum ipa_client_type)RMNET_DST_PIPE_FALLBACK;
	q->tx[1].alt_dst_pipe = 0;
	q->tx[1].hdr_l2_type = IPA_HDR_L2_ETHERNET_II;
	set_hdr_name_eth0_v6(q->tx[1].hdr_name);
}

static void force_query_eth0_rx_props(struct ipa_ioc_query_intf_rx_props *q)
{
	unsigned int j;
	unsigned int i;

	if (!q || !is_eth_iface(q->name))
		return;

	q->num_rx_props = 2;
	for (i = 0; i < 2; i++) {
		uint8_t *p = (uint8_t *)&q->rx[i];
		for (j = 0; j < sizeof(q->rx[i]); j++)
			p[j] = 0;
	}

	q->rx[0].ip = IPA_IP_v4;
	q->rx[0].src_pipe = (enum ipa_client_type)ETH_SRC_PIPE_FALLBACK;
	q->rx[1].ip = IPA_IP_v6;
	q->rx[1].src_pipe = (enum ipa_client_type)ETH_SRC_PIPE_FALLBACK;
}

static int hex_nibble(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int parse_hex32_token(const char *s, int *advance, uint32_t *out)
{
	int i = 0;
	int n;
	int digits = 0;
	uint32_t v = 0;

	while (s[i] == ' ' || s[i] == '\t')
		i++;
	for (; i < 64; i++) {
		n = hex_nibble(s[i]);
		if (n < 0)
			break;
		v = (v << 4) | (uint32_t)n;
		digits++;
	}
	if (digits == 0)
		return 0;
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
	if (fd < 0)
		return 0;
	nread = sys_read(fd, buf, sizeof(buf) - 1);
	sys_close(fd);
	if (nread <= 0)
		return 0;
	buf[nread] = '\0';

	while (i < nread) {
		int line_start = i;
		int line_end = i;
		char iface[IFNAMSIZ];
		int k = 0;
		int pos;
		uint32_t dst = 0;
		uint32_t gw_le = 0;

		while (line_end < nread && buf[line_end] != '\n')
			line_end++;
		i = line_end + 1;
		line_no++;
		if (line_no == 1) /* header */
			continue;

		/* iface */
		while (line_start < line_end &&
		       buf[line_start] != ' ' &&
		       buf[line_start] != '\t' &&
		       k < IFNAMSIZ - 1) {
			iface[k++] = buf[line_start++];
		}
		iface[k] = '\0';
		if (k == 0)
			continue;

		pos = line_start;
		if (!parse_hex32_token(&buf[pos], &k, &dst))
			continue;
		pos += k;
		if (!parse_hex32_token(&buf[pos], &k, &gw_le))
			continue;

		if (dst == 0 && str_eq_n(iface, ifname, IFNAMSIZ)) {
			*gw_be_out = bswap32_local(gw_le);
			return 1;
		}
	}
	return 0;
}

static int lookup_hdr_handle_by_name(int fd, const char *name, uint32_t *hdl_out)
{
	struct ipa_ioc_get_hdr gh;
	long ret;

	if (!name || !hdl_out)
		return 0;

	{
		unsigned int j;
		uint8_t *p = (uint8_t *)&gh;
		for (j = 0; j < sizeof(gh); j++)
			p[j] = 0;
	}
	copy_name_32(gh.name, name);
	ret = sys_ioctl(fd, IPA_IOC_GET_HDR, &gh);
	if (ret < 0)
		return 0;

	*hdl_out = gh.hdl;
	return 1;
}

static long get_hdr_handle_raw_ioctl(int fd, const char *name, uint32_t *hdl_out)
{
	struct ipa_ioc_get_hdr gh;
	unsigned int j;
	long ret;

	if (!name)
		return -1;

	{
		uint8_t *p = (uint8_t *)&gh;
		for (j = 0; j < sizeof(gh); j++)
			p[j] = 0;
	}
	copy_name_32(gh.name, name);
	ret = sys_ioctl(fd, IPA_IOC_GET_HDR, &gh);
	if (ret >= 0 && hdl_out)
		*hdl_out = gh.hdl;
	return ret;
}

static int remap_copy_hdr_name_if_known(int fd, struct ipa_ioc_copy_hdr *ch)
{
	const char *cand0 = 0;
	const char *cand1 = 0;
	uint32_t h = 0;

	if (!ch)
		return 0;

	if (str_eq_32(ch->name, "eth0_ipv4")) {
		cand0 = "35_IPACM_ODU_v4";
		cand1 = "35_IPACM_ETH_v4_0";
	} else if (str_eq_32(ch->name, "eth0_ipv6")) {
		cand0 = "35_IPACM_ODU_v6";
		cand1 = "35_IPACM_ETH_v6_0";
	} else {
		return 0;
	}

	if (lookup_hdr_handle_by_name(fd, cand0, &h)) {
		copy_name_32(ch->name, cand0);
		log_cstr("COPY_HDR_remap", cand0);
		log_u32hex("COPY_HDR_remap_hdl", h);
		return 1;
	}
	if (lookup_hdr_handle_by_name(fd, cand1, &h)) {
		copy_name_32(ch->name, cand1);
		log_cstr("COPY_HDR_remap", cand1);
		log_u32hex("COPY_HDR_remap_hdl", h);
		return 1;
	}

	log_u32hex("COPY_HDR_remap_miss", 1);
	return -1;
}

static int is_copy_hdr_v4_name(const char *name)
{
	if (!name)
		return 0;
	return str_eq_32(name, "eth0_ipv4") ||
	       str_eq_32(name, "35_IPACM_ODU_v4") ||
	       str_eq_32(name, "35_IPACM_ETH_v4_0");
}

static int is_copy_hdr_v6_name(const char *name)
{
	if (!name)
		return 0;
	return str_eq_32(name, "eth0_ipv6") ||
	       str_eq_32(name, "35_IPACM_ODU_v6") ||
	       str_eq_32(name, "35_IPACM_ETH_v6_0");
}

static int ensure_fallback_hdr_handle(int fd, enum ipa_ip_type ip, uint32_t *hdl_out)
{
	uint32_t h = 0;

	if (!hdl_out)
		return 0;

	if (ip == IPA_IP_v4 && g_cached_v4_hdr_hdl != 0) {
		*hdl_out = g_cached_v4_hdr_hdl;
		return 1;
	}
	if (ip == IPA_IP_v6 && g_cached_v6_hdr_hdl != 0) {
		*hdl_out = g_cached_v6_hdr_hdl;
		return 1;
	}

	if (ip == IPA_IP_v4) {
		if (!lookup_hdr_handle_by_name(fd, "35_IPACM_ETH_v4_0", &h) &&
		    !lookup_hdr_handle_by_name(fd, "35_IPACM_ODU_v4", &h))
			return 0;
		g_cached_v4_hdr_hdl = h;
	} else if (ip == IPA_IP_v6) {
		if (!lookup_hdr_handle_by_name(fd, "35_IPACM_ETH_v6_0", &h) &&
		    !lookup_hdr_handle_by_name(fd, "35_IPACM_ODU_v6", &h))
			return 0;
		g_cached_v6_hdr_hdl = h;
	} else {
		return 0;
	}

	*hdl_out = h;
	return 1;
}

static void backfill_add_hdr_handles(int fd, struct ipa_ioc_add_hdr *ah)
{
	unsigned int i;

	if (!ah || ah->num_hdrs == 0)
		return;

	for (i = 0; i < (unsigned int)ah->num_hdrs && i < 16; i++) {
		uint32_t h = 0;
		if (ah->hdr[i].hdr_hdl != 0 && ah->hdr[i].hdr_hdl != INVALID_HDR_HDL &&
		    ah->hdr[i].status == 0)
			continue;
		if (!lookup_hdr_handle_by_name(fd, ah->hdr[i].name, &h))
			continue;
		if (h == 0 || h == INVALID_HDR_HDL)
			continue;
		ah->hdr[i].hdr_hdl = h;
		ah->hdr[i].status = 0;
		log_cstr("HDR_backfill_name", ah->hdr[i].name);
		log_u32hex("HDR_backfill_hdl", h);
	}
}

static int lookup_rt_tbl_handle(int fd, enum ipa_ip_type ip, const char *name, uint32_t *hdl_out)
{
	struct ipa_ioc_get_rt_tbl rt;
	long ret;

	if (!name || !hdl_out)
		return 0;

	{
		unsigned int j;
		uint8_t *p = (uint8_t *)&rt;
		for (j = 0; j < sizeof(rt); j++)
			p[j] = 0;
	}
	rt.ip = ip;
	copy_name_32(rt.name, name);
	ret = sys_ioctl(fd, IPA_IOC_GET_RT_TBL, &rt);
	if (ret < 0)
		return 0;

	*hdl_out = rt.hdl;
	return 1;
}

static int ensure_wan_rt_tbl_handle(int fd, enum ipa_ip_type ip, uint32_t *hdl_out)
{
	uint32_t h = 0;

	if (!hdl_out)
		return 0;

	if (ip == IPA_IP_v4 && g_cached_v4_rt_tbl_hdl != 0) {
		*hdl_out = g_cached_v4_rt_tbl_hdl;
		return 1;
	}
	if (ip == IPA_IP_v6 && g_cached_v6_rt_tbl_hdl != 0) {
		*hdl_out = g_cached_v6_rt_tbl_hdl;
		return 1;
	}

	if (ip == IPA_IP_v4) {
		if (!lookup_rt_tbl_handle(fd, ip, "COMRTBLLANv4", &h) &&
		    !lookup_rt_tbl_handle(fd, ip, "WANRTBLv4", &h) &&
		    !lookup_rt_tbl_handle(fd, ip, "ipa_dflt_wan_rt", &h) &&
		    !lookup_rt_tbl_handle(fd, ip, "ipa_dflt_rt", &h))
			return 0;
	} else if (ip == IPA_IP_v6) {
		if (!lookup_rt_tbl_handle(fd, ip, "WANRTBLv6", &h) &&
		    !lookup_rt_tbl_handle(fd, ip, "COMRTBLv6", &h) &&
		    !lookup_rt_tbl_handle(fd, ip, "ipa_dflt_wan_rt", &h) &&
		    !lookup_rt_tbl_handle(fd, ip, "ipa_dflt_rt", &h))
			return 0;
	} else {
		return 0;
	}

	if (ip == IPA_IP_v4)
		g_cached_v4_rt_tbl_hdl = h;
	else if (ip == IPA_IP_v6)
		g_cached_v6_rt_tbl_hdl = h;
	else
		return 0;

	log_u32hex("RT_tbl_pick", h);
	*hdl_out = h;
	return 1;
}

static long add_flt_rule_via_legacy_layout(int fd, struct ipa_ioc_add_flt_rule *fr)
{
	struct ipa_ioc_add_flt_rule_legacy_flat lf;
	unsigned int i, j;
	long ret;

	if (!fr)
		return -1;
	if (fr->num_rules == 0 || fr->num_rules > MAX_FLT_RULES_TRANSLATE)
		return sys_ioctl(fd, IPA_IOC_ADD_FLT_RULE, fr);

	{
		uint8_t *p = (uint8_t *)&lf;
		for (j = 0; j < sizeof(lf); j++)
			p[j] = 0;
	}

	lf.commit = fr->commit;
	lf.ip = fr->ip;
	lf.ep = fr->ep;
	lf.global = fr->global;
	lf.num_rules = fr->num_rules;

	for (i = 0; i < (unsigned int)fr->num_rules; i++) {
		uint8_t *src = (uint8_t *)&fr->rules[i].rule;
		for (j = 0; j < sizeof(lf.rules[i].rule.raw); j++)
			lf.rules[i].rule.raw[j] = src[j];
		lf.rules[i].at_rear = fr->rules[i].at_rear;
		lf.rules[i].flt_rule_hdl = fr->rules[i].flt_rule_hdl;
		lf.rules[i].status = fr->rules[i].status;
	}

	ret = sys_ioctl(fd, IPA_IOC_ADD_FLT_RULE, &lf);

	for (i = 0; i < (unsigned int)fr->num_rules; i++) {
		fr->rules[i].flt_rule_hdl = lf.rules[i].flt_rule_hdl;
		fr->rules[i].status = lf.rules[i].status;
	}
	return ret;
}

static long add_flt_rule_after_via_legacy_layout(int fd,
	struct ipa_ioc_add_flt_rule_after *fr)
{
	struct ipa_ioc_add_flt_rule_after_legacy_flat lf;
	unsigned int i, j;
	long ret;

	if (!fr)
		return -1;
	if (fr->num_rules == 0 || fr->num_rules > MAX_FLT_RULES_TRANSLATE)
		return sys_ioctl(fd, IPA_IOC_ADD_FLT_RULE_AFTER, fr);

	{
		uint8_t *p = (uint8_t *)&lf;
		for (j = 0; j < sizeof(lf); j++)
			p[j] = 0;
	}

	lf.commit = fr->commit;
	lf.ip = fr->ip;
	lf.ep = fr->ep;
	lf.num_rules = fr->num_rules;
	lf.add_after_hdl = fr->add_after_hdl;

	for (i = 0; i < (unsigned int)fr->num_rules; i++) {
		uint8_t *src = (uint8_t *)&fr->rules[i].rule;
		for (j = 0; j < sizeof(lf.rules[i].rule.raw); j++)
			lf.rules[i].rule.raw[j] = src[j];
		lf.rules[i].at_rear = fr->rules[i].at_rear;
		lf.rules[i].flt_rule_hdl = fr->rules[i].flt_rule_hdl;
		lf.rules[i].status = fr->rules[i].status;
	}

	ret = sys_ioctl(fd, IPA_IOC_ADD_FLT_RULE_AFTER, &lf);

	for (i = 0; i < (unsigned int)fr->num_rules; i++) {
		fr->rules[i].flt_rule_hdl = lf.rules[i].flt_rule_hdl;
		fr->rules[i].status = lf.rules[i].status;
	}
	return ret;
}

static long add_flt_rule_v2_via_legacy_layout(int fd,
	struct ipa_ioc_add_flt_rule_v2 *fr)
{
	struct ipa_ioc_add_flt_rule_v2_legacy_ptr lf;
	struct ipa_flt_rule_add_v2_legacy lr[MAX_FLT_RULES_TRANSLATE];
	struct ipa_flt_rule_add_v2 *src;
	unsigned int i, j;
	long ret;
	uint32_t rt_fix = 0;

	if (!fr)
		return -1;
	if (fr->num_rules == 0 || fr->num_rules > MAX_FLT_RULES_TRANSLATE)
		return sys_ioctl(fd, IPA_IOC_ADD_FLT_RULE_V2, fr);

	src = (struct ipa_flt_rule_add_v2 *)(uintptr_t)fr->rules;
	if (!src)
		return sys_ioctl(fd, IPA_IOC_ADD_FLT_RULE_V2, fr);

	{
		uint8_t *p = (uint8_t *)&lf;
		for (j = 0; j < sizeof(lf); j++)
			p[j] = 0;
	}
	{
		uint8_t *p = (uint8_t *)&lr[0];
		for (j = 0; j < sizeof(lr); j++)
			p[j] = 0;
	}

	lf.commit = fr->commit;
	lf.ip = fr->ip;
	lf.ep = fr->ep;
	lf.global = fr->global;
	lf.num_rules = fr->num_rules;
	lf.flt_rule_size = sizeof(struct ipa_flt_rule_add_v2_legacy);
	lf.rules = (uint64_t)(uintptr_t)&lr[0];

	for (i = 0; i < (unsigned int)fr->num_rules; i++) {
		uint32_t act = (uint32_t)src[i].rule.action;
		uint32_t rth = (uint32_t)src[i].rule.rt_tbl_hdl;

		if (FORCE_FLT_ROUTE_OVERRIDE &&
		    (uint32_t)fr->ep == (uint32_t)IPA_CLIENT_RTK_ETHERNET_PROD &&
		    ensure_wan_rt_tbl_handle(fd, fr->ip, &rt_fix)) {
			src[i].rule.action = IPA_PASS_TO_ROUTING;
			src[i].rule.rt_tbl_hdl = rt_fix;
			log_u32hex("FLTv2_act_fix", (uint32_t)src[i].rule.action);
			log_u32hex("FLTv2_rth_fix", rt_fix);
		} else if (act == (uint32_t)IPA_PASS_TO_ROUTING &&
			   (rth == 0 || rth == INVALID_HDR_HDL) &&
			   ensure_wan_rt_tbl_handle(fd, fr->ip, &rt_fix)) {
			src[i].rule.rt_tbl_hdl = rt_fix;
			log_u32hex("FLTv2_rth_fix", rt_fix);
		}

		lr[i].at_rear = src[i].at_rear;
		lr[i].flt_rule_hdl = src[i].flt_rule_hdl;
		lr[i].status = src[i].status;
		{
			uint8_t *raw = (uint8_t *)&src[i].rule;
			for (j = 0; j < sizeof(lr[i].rule.raw); j++)
				lr[i].rule.raw[j] = raw[j];
		}
	}

	ret = sys_ioctl(fd, IPA_IOC_ADD_FLT_RULE_V2, &lf);

	for (i = 0; i < (unsigned int)fr->num_rules; i++) {
		src[i].flt_rule_hdl = lr[i].flt_rule_hdl;
		src[i].status = lr[i].status;
	}
	return ret;
}

static long add_rt_rule_via_legacy_layout(int fd, struct ipa_ioc_add_rt_rule *rr)
{
	struct ipa_ioc_add_rt_rule_legacy_flat lr;
	unsigned int i, j;
	long ret;

	if (!rr)
		return -1;
	if (rr->num_rules == 0 || rr->num_rules > MAX_RT_RULES_TRANSLATE)
		return sys_ioctl(fd, IPA_IOC_ADD_RT_RULE, rr);

	{
		uint8_t *p = (uint8_t *)&lr;
		for (j = 0; j < sizeof(lr); j++)
			p[j] = 0;
	}

	lr.commit = rr->commit;
	lr.ip = rr->ip;
	copy_name_32(lr.rt_tbl_name, rr->rt_tbl_name);
	lr.num_rules = rr->num_rules;

	for (i = 0; i < (unsigned int)rr->num_rules; i++) {
		uint8_t *src = (uint8_t *)&rr->rules[i].rule;
		for (j = 0; j < sizeof(lr.rules[i].rule.raw); j++)
			lr.rules[i].rule.raw[j] = src[j];
		lr.rules[i].at_rear = rr->rules[i].at_rear;
		lr.rules[i].rt_rule_hdl = rr->rules[i].rt_rule_hdl;
		lr.rules[i].status = rr->rules[i].status;
	}

	ret = sys_ioctl(fd, IPA_IOC_ADD_RT_RULE, &lr);

	for (i = 0; i < (unsigned int)rr->num_rules; i++) {
		rr->rules[i].rt_rule_hdl = lr.rules[i].rt_rule_hdl;
		rr->rules[i].status = lr.rules[i].status;
	}
	return ret;
}

static long add_rt_rule_after_via_legacy_layout(int fd,
	struct ipa_ioc_add_rt_rule_after *rr)
{
	struct ipa_ioc_add_rt_rule_after_legacy_flat lr;
	unsigned int i, j;
	long ret;

	if (!rr)
		return -1;
	if (rr->num_rules == 0 || rr->num_rules > MAX_RT_RULES_TRANSLATE)
		return sys_ioctl(fd, IPA_IOC_ADD_RT_RULE_AFTER, rr);

	{
		uint8_t *p = (uint8_t *)&lr;
		for (j = 0; j < sizeof(lr); j++)
			p[j] = 0;
	}

	lr.commit = rr->commit;
	lr.ip = rr->ip;
	copy_name_32(lr.rt_tbl_name, rr->rt_tbl_name);
	lr.num_rules = rr->num_rules;
	lr.add_after_hdl = rr->add_after_hdl;

	for (i = 0; i < (unsigned int)rr->num_rules; i++) {
		uint8_t *src = (uint8_t *)&rr->rules[i].rule;
		for (j = 0; j < sizeof(lr.rules[i].rule.raw); j++)
			lr.rules[i].rule.raw[j] = src[j];
		lr.rules[i].at_rear = rr->rules[i].at_rear;
		lr.rules[i].rt_rule_hdl = rr->rules[i].rt_rule_hdl;
		lr.rules[i].status = rr->rules[i].status;
	}

	ret = sys_ioctl(fd, IPA_IOC_ADD_RT_RULE_AFTER, &lr);

	for (i = 0; i < (unsigned int)rr->num_rules; i++) {
		rr->rules[i].rt_rule_hdl = lr.rules[i].rt_rule_hdl;
		rr->rules[i].status = lr.rules[i].status;
	}
	return ret;
}

static void force_notify_wan_upstream(const char *up_ifname)
{
	struct ipa_wan_msg msg;
	uint32_t gw_be = 0;
	int fd;
	long ret;

	if (g_wan_force_done)
		return;
	if (!up_ifname || up_ifname[0] == '\0')
		return;
	if (!find_default_gw_be(up_ifname, &gw_be))
		return;

	fd = (int)sys_open("/dev/ipa", 2, 0); /* O_RDWR */
	if (fd < 0)
		return;

	/* Zero-init without libc. */
	{
		unsigned int j;
		uint8_t *p = (uint8_t *)&msg;
		for (j = 0; j < sizeof(msg); j++)
			p[j] = 0;
	}
	copy_name_32(msg.upstream_ifname, up_ifname);
	copy_name_32(msg.tethered_ifname, "eth0");
	msg.ip = IPA_IP_v4;
	msg.ipv4_addr_gw = gw_be;

	ret = sys_ioctl(fd, IPA_IOC_NOTIFY_WAN_UPSTREAM_ROUTE_ADD, &msg);
	if (ret >= 0)
		g_wan_force_done = 1;
	sys_close(fd);
}

int ioctl(int fd, unsigned long request, ...)
{
	va_list ap;
	void *arg;
	long ret;
	unsigned long ioc_type;
	unsigned long ioc_nr;
	int force_wan_up = 0;
	char wan_ifname[IFNAMSIZ];
	int wi;

	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);

	ioc_type = (request >> _IOC_TYPESHIFT) & _IOC_TYPEMASK;
	ioc_nr = (request >> _IOC_NRSHIFT) & _IOC_NRMASK;
	if (ioc_type == WAN_IOC_MAGIC &&
	    ioc_nr == WAN_IOCTL_NOTIFY_WAN_STATE &&
	    arg) {
		struct wan_ioctl_notify_wan_state_local *st =
			(struct wan_ioctl_notify_wan_state_local *)arg;
		if (st->up) {
			force_wan_up = 1;
			for (wi = 0; wi < IFNAMSIZ; wi++)
				wan_ifname[wi] = st->upstreamIface[wi];
			wan_ifname[IFNAMSIZ - 1] = '\0';
		}
	}
	if (ioc_type == IPA_IOC_MAGIC && !fd_is_ipa_dev(fd))
		return (int)sys_ioctl(fd, request, arg);

	/* Log and intercept COPY_HDR before calling kernel */
	if (ioc_type == IPA_IOC_MAGIC && ioc_nr == IPA_IOCTL_COPY_HDR && arg) {
		struct ipa_ioc_copy_hdr *ch = (struct ipa_ioc_copy_hdr *)arg;
		uint32_t gh = 0;
		long gh_ret = get_hdr_handle_raw_ioctl(fd, ch->name, &gh);
		log_cstr("COPY_HDR_req", ch->name);
		log_u32hex("COPY_GETH_ret", (uint32_t)(unsigned int)gh_ret);
		if (gh_ret >= 0)
			log_u32hex("COPY_GETH_hdl", gh);
		if (is_copy_hdr_v4_name(ch->name) || is_copy_hdr_v6_name(ch->name))
			(void)remap_copy_hdr_name_if_known(fd, ch);
		if (is_blank_name_32(ch->name)) {
			log_u32hex("COPY_HDR_blank", 1);
		}
	}

	/* Log and fix ADD_FLT_RULE ep before calling kernel.
	 * ipacm installs rules with ep=APPS_WAN_CONS(35) derived from
	 * QUERY_INTF_RX_PROPS for eth0; the correct UL pipe is
	 * IPA_CLIENT_RTK_ETHERNET_PROD(108). */
	if (ioc_type == IPA_IOC_MAGIC && ioc_nr == IPA_IOCTL_ADD_FLT_RULE && arg) {
		struct ipa_ioc_add_flt_rule *fr = (struct ipa_ioc_add_flt_rule *)arg;
		unsigned int fi;
		uint32_t rt_fix = 0;
		if (ENABLE_UL_EP_REWRITE &&
		    !fr->global &&
		    (uint32_t)fr->ep == (uint32_t)IPA_CLIENT_APPS_WAN_CONS)
			fr->ep = (enum ipa_client_type)IPA_CLIENT_RTK_ETHERNET_PROD;
		for (fi = 0; fi < (unsigned int)fr->num_rules && fi < 8; fi++) {
			uint32_t act = (uint32_t)fr->rules[fi].rule.action;
			uint32_t rth = (uint32_t)fr->rules[fi].rule.rt_tbl_hdl;
			log_u32hex("FLT_rule_act", act);
			log_u32hex("FLT_rule_rth", rth);
			if (FORCE_FLT_ROUTE_OVERRIDE &&
			    (uint32_t)fr->ep == (uint32_t)IPA_CLIENT_RTK_ETHERNET_PROD &&
			    ensure_wan_rt_tbl_handle(fd, fr->ip, &rt_fix)) {
				fr->rules[fi].rule.action = IPA_PASS_TO_ROUTING;
				fr->rules[fi].rule.rt_tbl_hdl = rt_fix;
				log_u32hex("FLT_act_fix", (uint32_t)fr->rules[fi].rule.action);
				log_u32hex("FLT_rth_fix", rt_fix);
			} else if (act == (uint32_t)IPA_PASS_TO_ROUTING &&
				   (rth == 0 || rth == INVALID_HDR_HDL) &&
				   ensure_wan_rt_tbl_handle(fd, fr->ip, &rt_fix)) {
				fr->rules[fi].rule.rt_tbl_hdl = rt_fix;
				log_u32hex("FLT_rth_fix", rt_fix);
			}
		}
		log_u32hex("FLT_ep", (uint32_t)fr->ep);
		log_u32hex("FLT_ip", (uint32_t)fr->ip);
		log_u32hex("FLT_global", (uint32_t)fr->global);
		log_u32hex("FLT_nrules", (uint32_t)fr->num_rules);
	}

	if (ioc_type == IPA_IOC_MAGIC && ioc_nr == IPA_IOCTL_ADD_FLT_RULE_V2 && arg) {
		struct ipa_ioc_add_flt_rule_v2 *fr = (struct ipa_ioc_add_flt_rule_v2 *)arg;
		log_u32hex("FLTv2_ep", (uint32_t)fr->ep);
		log_u32hex("FLTv2_ip", (uint32_t)fr->ip);
		log_u32hex("FLTv2_global", (uint32_t)fr->global);
		log_u32hex("FLTv2_nrules", (uint32_t)fr->num_rules);
		log_u32hex("FLTv2_rule_sz", (uint32_t)fr->flt_rule_size);
	}

#if ENABLE_RT_IOC_TOUCH
	/* Fix invalid header handles in routing rules before kernel validation.
	 * ipacm may carry 0xffffffff when ADD_HDR status is misreported. */
	if (ioc_type == IPA_IOC_MAGIC && ioc_nr == IPA_IOCTL_ADD_RT_RULE && arg) {
		struct ipa_ioc_add_rt_rule *rr = (struct ipa_ioc_add_rt_rule *)arg;
		unsigned int ri;
		uint32_t repl = 0;

		log_cstr("RT_tbl", rr->rt_tbl_name);
		log_u32hex("RT_ip", (uint32_t)rr->ip);
		log_u32hex("RT_nrules", (uint32_t)rr->num_rules);

			for (ri = 0; ri < (unsigned int)rr->num_rules && ri < 8; ri++) {
				uint32_t old = rr->rules[ri].rule.hdr_hdl;
#if FORCE_RT_DST_33_TO_35
				if ((uint32_t)rr->rules[ri].rule.dst ==
				    (uint32_t)IPA_CLIENT_APPS_LAN_CONS) {
					rr->rules[ri].rule.dst =
						(enum ipa_client_type)IPA_CLIENT_APPS_WAN_CONS;
					log_u32hex("RT_dst_fix", (uint32_t)rr->rules[ri].rule.dst);
				}
#endif
				if (old == INVALID_HDR_HDL &&
				    ensure_fallback_hdr_handle(fd, rr->ip, &repl)) {
					rr->rules[ri].rule.hdr_hdl = repl;
				log_u32hex("RT_hdr_fix_old", old);
				log_u32hex("RT_hdr_fix_new", repl);
			}
			log_u32hex("RT_dst", (uint32_t)rr->rules[ri].rule.dst);
			log_u32hex("RT_hdr", (uint32_t)rr->rules[ri].rule.hdr_hdl);
		}
	}
#endif

	if (ioc_type == IPA_IOC_MAGIC &&
		   ioc_nr == IPA_IOCTL_ADD_FLT_RULE_V2 && arg) {
		ret = add_flt_rule_v2_via_legacy_layout(fd,
			(struct ipa_ioc_add_flt_rule_v2 *)arg);
	} else if (ioc_type == IPA_IOC_MAGIC &&
		   ioc_nr == IPA_IOCTL_ADD_FLT_RULE && arg) {
		ret = add_flt_rule_via_legacy_layout(fd, (struct ipa_ioc_add_flt_rule *)arg);
	} else if (ioc_type == IPA_IOC_MAGIC &&
		   ioc_nr == IPA_IOCTL_ADD_FLT_RULE_AFTER && arg) {
		ret = add_flt_rule_after_via_legacy_layout(fd,
			(struct ipa_ioc_add_flt_rule_after *)arg);
	} else {
		ret = sys_ioctl(fd, request, arg);
	}
	if (ret < 0 && ioc_type == IPA_IOC_MAGIC &&
	    ioc_nr == IPA_IOCTL_QUERY_INTF_TX_PROPS && arg) {
		struct ipa_ioc_query_intf_tx_props *q =
			(struct ipa_ioc_query_intf_tx_props *)arg;
		log_cstr("QTX_fail_if", q->name);
		log_u32hex("QTX_fail_ret", (uint32_t)(unsigned int)ret);
		force_query_eth0_tx_props(q);
		if (is_eth_iface(q->name)) {
			ret = 0;
			log_u32hex("QTX_force_eth0", 1);
		}
	}
	if (ret < 0 && ioc_type == IPA_IOC_MAGIC &&
	    ioc_nr == IPA_IOCTL_QUERY_INTF_RX_PROPS && arg) {
		struct ipa_ioc_query_intf_rx_props *q =
			(struct ipa_ioc_query_intf_rx_props *)arg;
		log_cstr("QRX_fail_if", q->name);
		log_u32hex("QRX_fail_ret", (uint32_t)(unsigned int)ret);
		force_query_eth0_rx_props(q);
		if (is_eth_iface(q->name)) {
			ret = 0;
			log_u32hex("QRX_force_eth0", 1);
		}
	}
	if (ioc_type == IPA_IOC_MAGIC && ioc_nr == IPA_IOCTL_ADD_HDR && arg)
		backfill_add_hdr_handles(fd, (struct ipa_ioc_add_hdr *)arg);
	force_notify_wan_upstream("rmnet_data0");
	if (force_wan_up)
		force_notify_wan_upstream(wan_ifname);
	if (ioc_type == IPA_IOC_MAGIC && ioc_nr == IPA_IOCTL_COPY_HDR)
		log_u32hex("COPY_HDR_ret", (uint32_t)(unsigned int)ret);

	if (ioc_type == IPA_IOC_MAGIC && ioc_nr == IPA_IOCTL_ADD_HDR && arg) {
		struct ipa_ioc_add_hdr *ah = (struct ipa_ioc_add_hdr *)arg;
		unsigned int hi;
		log_u32hex("ADD_HDR_ret", (uint32_t)(unsigned int)ret);
		log_u32hex("ADD_HDR_n", (uint32_t)ah->num_hdrs);
		for (hi = 0; hi < (unsigned int)ah->num_hdrs && hi < 8; hi++) {
			log_cstr("ADD_HDR_name", ah->hdr[hi].name);
			log_u32hex("ADD_HDR_hdl", ah->hdr[hi].hdr_hdl);
			log_u32hex("ADD_HDR_st", (uint32_t)(unsigned int)ah->hdr[hi].status);
		}
	}

	/* Log ADD_FLT_RULE per-rule status after kernel */
	if (ioc_type == IPA_IOC_MAGIC && ioc_nr == IPA_IOCTL_ADD_FLT_RULE && arg) {
		struct ipa_ioc_add_flt_rule *fr = (struct ipa_ioc_add_flt_rule *)arg;
		unsigned int ri;
		log_u32hex("FLT_ret", (uint32_t)(unsigned int)ret);
		for (ri = 0; ri < (unsigned int)fr->num_rules && ri < 8; ri++)
			log_u32hex("FLT_status", (uint32_t)(unsigned int)fr->rules[ri].status);
	}

	if (ioc_type == IPA_IOC_MAGIC && ioc_nr == IPA_IOCTL_ADD_FLT_RULE_V2 && arg) {
		struct ipa_ioc_add_flt_rule_v2 *fr = (struct ipa_ioc_add_flt_rule_v2 *)arg;
		struct ipa_flt_rule_add_v2 *rules =
			(struct ipa_flt_rule_add_v2 *)(uintptr_t)fr->rules;
		unsigned int ri;
		log_u32hex("FLTv2_ret", (uint32_t)(unsigned int)ret);
		if (rules) {
			for (ri = 0; ri < (unsigned int)fr->num_rules && ri < 8; ri++)
				log_u32hex("FLTv2_status",
					(uint32_t)(unsigned int)rules[ri].status);
		}
	}

#if ENABLE_RT_IOC_TOUCH
	if (ioc_type == IPA_IOC_MAGIC && ioc_nr == IPA_IOCTL_ADD_RT_RULE && arg) {
		struct ipa_ioc_add_rt_rule *rr = (struct ipa_ioc_add_rt_rule *)arg;
		unsigned int ri;
		log_u32hex("RT_ret", (uint32_t)(unsigned int)ret);
		for (ri = 0; ri < (unsigned int)rr->num_rules && ri < 8; ri++) {
			log_u32hex("RT_status", (uint32_t)(unsigned int)rr->rules[ri].status);
			log_u32hex("RT_hdl", (uint32_t)rr->rules[ri].rt_rule_hdl);
		}
	}
#endif

	/* If COPY_HDR failed (header not in IPA table), fake success with zeroed
	 * header data so ipacm can continue to ADD_FLT_RULE for ETH UL path. */
	if (ret < 0 && ioc_type == IPA_IOC_MAGIC &&
	    ioc_nr == IPA_IOCTL_COPY_HDR && arg) {
		struct ipa_ioc_copy_hdr *ch = (struct ipa_ioc_copy_hdr *)arg;
		int is_v4 = is_copy_hdr_v4_name(ch->name);
		int is_v6 = is_copy_hdr_v6_name(ch->name);
		log_u32hex("COPY_HDR_fake_from", (uint32_t)(unsigned int)ret);
		if ((is_v4 || is_v6) && remap_copy_hdr_name_if_known(fd, ch) > 0) {
			ret = sys_ioctl(fd, request, arg);
			log_u32hex("COPY_HDR_retry_ret", (uint32_t)(unsigned int)ret);
		}
		if (ret < 0) {
			if (is_v4) {
				synthesize_copy_hdr_eth(ch, 0);
			} else if (is_v6) {
				synthesize_copy_hdr_eth(ch, 1);
			} else {
				synthesize_copy_hdr(ch);
			}
			log_cstr("COPY_HDR_synth", ch->name);
			log_u32hex("COPY_HDR_synth_len", ch->hdr_len);
			ret = 0;
		}
		ret = 0;
	}

	if (ret >= 0 && ENABLE_QUERY_IOC_SANITIZE && ioc_type == IPA_IOC_MAGIC) {
		if (ioc_nr == IPA_IOCTL_QUERY_INTF_TX_PROPS) {
			log_query_tx_props_raw((struct ipa_ioc_query_intf_tx_props *)arg);
			sanitize_query_tx_props((struct ipa_ioc_query_intf_tx_props *)arg);
		} else if (ioc_nr == IPA_IOCTL_QUERY_INTF_RX_PROPS)
			sanitize_query_rx_props((struct ipa_ioc_query_intf_rx_props *)arg);
	}

	return (int)ret;
}

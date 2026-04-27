#define _GNU_SOURCE
#include <asm/unistd.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <linux/msm_ipa.h>

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

#define DEFAULT_LOG_PATH "/usrdata/ipacm_abi_bridge_v4.log"
#define ENV_PATH "/proc/self/environ"
#define MAX_ENV_BUF 4096U
#define MAX_CFG_STR 256U
#define MAX_LOG_PATH 160U
#define MAX_PROPS 16U
#define OLD_ATTR_SIZE 152U
#define OLD_TX_SIZE 200U
#define OLD_RX_SIZE 164U
#define OLD_TX_DST 156U
#define OLD_TX_ALT 160U
#define OLD_TX_HDR 164U
#define OLD_TX_L2 196U
#define OLD_RX_SRC 156U
#define OLD_RX_L2 160U
#define QUERY_PREFIX_SIZE 36U
#define PIPE_MAX 1024U
#define RT_FLT_MAX_LOG_RULES 3U
#define RT_FLT_MAX_TRANSLATE_RULES 8U
#define ATTR_COMMON_SIZE 148U
#define OLD_RT_RULE_SIZE 168U
#define OLD_RT_ADD_SIZE 180U
#define OLD_RT_ADD_AT_REAR 168U
#define OLD_RT_ADD_HDL 172U
#define OLD_RT_ADD_STATUS 176U
#define OLD_FLT_RULE_SIZE 360U
#define OLD_FLT_ADD_SIZE 372U
#define OLD_FLT_ADD_AT_REAR 360U
#define OLD_FLT_ADD_HDL 364U
#define OLD_FLT_ADD_STATUS 368U
#define OLD_GEN_EQ_SIZE 340U
#define OLD_GEN_EQ_ATTR 4U
#define OLD_GEN_EQ_EQ 156U

struct bridge_cfg {
	int loaded;
	int mode_translate;
	int tx;
	int rx;
	int ext;
	int rt_flt_log;
	int rt_flt_translate;
	int fail_closed;
	int hexdump_once;
	char allow[MAX_CFG_STR];
	char translate[MAX_CFG_STR];
	char log_path[MAX_LOG_PATH];
};

static struct bridge_cfg g_cfg;
static uint32_t g_hexdump_mask;

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

static uint32_t rd32(const uint8_t *p)
{
	return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int cstr_len(const char *s, int max)
{
	int i;
	if (!s) return 0;
	for (i = 0; i < max && s[i]; i++) ;
	return i;
}

static int str_eq(const char *a, const char *b)
{
	int i;
	for (i = 0; ; i++) {
		if (a[i] != b[i]) return 0;
		if (a[i] == '\0') return 1;
	}
}

static int str_eq_n(const char *a, const char *b, int max_n)
{
	int i;
	for (i = 0; i < max_n; i++) {
		if (a[i] != b[i]) return 0;
		if (a[i] == '\0') return 1;
	}
	return 1;
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

static void copy_cstr(char *dst, unsigned int max, const char *src)
{
	unsigned int i;
	if (!dst || max == 0) return;
	for (i = 0; i + 1 < max && src && src[i]; i++) dst[i] = src[i];
	dst[i] = '\0';
}

static int env_get(const char *name, char *out, unsigned int out_sz)
{
	static char env_buf[MAX_ENV_BUF];
	static int env_loaded;
	static long env_len;
	int fd;
	unsigned int name_len, pos;

	if (!env_loaded) {
		fd = (int)sys_open(ENV_PATH, 0, 0);
		if (fd >= 0) {
			env_len = sys_read(fd, env_buf, sizeof(env_buf) - 1);
			sys_close(fd);
			if (env_len < 0) env_len = 0;
			env_buf[env_len] = '\0';
		}
		env_loaded = 1;
	}
	if (!out || out_sz == 0) return 0;
	out[0] = '\0';
	name_len = (unsigned int)cstr_len(name, 128);
	pos = 0;
	while (pos < (unsigned int)env_len) {
		unsigned int start = pos;
		unsigned int i = 0;
		while (pos < (unsigned int)env_len && env_buf[pos]) pos++;
		if (pos > start + name_len && env_buf[start + name_len] == '=') {
			for (i = 0; i < name_len; i++) if (env_buf[start + i] != name[i]) break;
			if (i == name_len) {
				unsigned int v = start + name_len + 1;
				unsigned int j = 0;
				while (v < pos && j + 1 < out_sz) out[j++] = env_buf[v++];
				out[j] = '\0';
				return 1;
			}
		}
		pos++;
	}
	return 0;
}

static int parse_bool_env(const char *name, int def)
{
	char v[16];
	if (!env_get(name, v, sizeof(v))) return def;
	if (str_eq(v, "1") || str_eq(v, "yes") || str_eq(v, "true") || str_eq(v, "on")) return 1;
	if (str_eq(v, "0") || str_eq(v, "no") || str_eq(v, "false") || str_eq(v, "off")) return 0;
	return def;
}

static void load_cfg(void)
{
	char v[MAX_CFG_STR];
	if (g_cfg.loaded) return;
	g_cfg.loaded = 1;
	g_cfg.mode_translate = 0;
	g_cfg.tx = parse_bool_env("IPA_ABI_BRIDGE_TX", 1);
	g_cfg.rx = parse_bool_env("IPA_ABI_BRIDGE_RX", 1);
	g_cfg.ext = parse_bool_env("IPA_ABI_BRIDGE_EXT", 1);
	g_cfg.rt_flt_log = parse_bool_env("IPA_ABI_BRIDGE_RT_FLT_LOG", 1);
	g_cfg.rt_flt_translate = parse_bool_env("IPA_ABI_BRIDGE_RT_FLT_TRANSLATE", 0);
	g_cfg.fail_closed = parse_bool_env("IPA_ABI_BRIDGE_FAIL_CLOSED", 1);
	g_cfg.hexdump_once = parse_bool_env("IPA_ABI_BRIDGE_HEXDUMP_ONCE", 1);
	copy_cstr(g_cfg.allow, sizeof(g_cfg.allow), "eth0");
	copy_cstr(g_cfg.translate, sizeof(g_cfg.translate), "");
	copy_cstr(g_cfg.log_path, sizeof(g_cfg.log_path), DEFAULT_LOG_PATH);
	if (env_get("IPA_ABI_BRIDGE_MODE", v, sizeof(v)) && str_eq(v, "translate")) g_cfg.mode_translate = 1;
	if (env_get("IPA_ABI_BRIDGE_ALLOW_IFACES", v, sizeof(v))) copy_cstr(g_cfg.allow, sizeof(g_cfg.allow), v);
	if (env_get("IPA_ABI_BRIDGE_TRANSLATE_IFACES", v, sizeof(v))) copy_cstr(g_cfg.translate, sizeof(g_cfg.translate), v);
	if (env_get("IPA_ABI_BRIDGE_LOG", v, sizeof(v))) copy_cstr(g_cfg.log_path, sizeof(g_cfg.log_path), v);
}

static int csv_has(const char *csv, const char *name)
{
	unsigned int pos = 0;
	int nlen = cstr_len(name, IPA_RESOURCE_NAME_MAX);
	if (!csv || !csv[0] || !name) return 0;
	if (csv[0] == '*' && csv[1] == '\0') return 1;
	while (csv[pos]) {
		unsigned int start, len = 0, i;
		while (csv[pos] == ' ' || csv[pos] == ',') pos++;
		start = pos;
		while (csv[pos] && csv[pos] != ',') { pos++; len++; }
		while (len > 0 && csv[start + len - 1] == ' ') len--;
		if ((int)len == nlen) {
			for (i = 0; i < len; i++) if (csv[start + i] != name[i]) break;
			if (i == len) return 1;
		}
	}
	return 0;
}

static int iface_allowed(const char *name)
{
	load_cfg();
	return csv_has(g_cfg.allow, name);
}

static int iface_translate_allowed(const char *name)
{
	load_cfg();
	return g_cfg.mode_translate && csv_has(g_cfg.translate, name);
}

static int fd_is_ipa_dev(int fd)
{
	char path[32];
	char link[64];
	long n;
	static int cached_fd = -1;
	static int cached_is_ipa;
	unsigned int i, nrev = 0, v;
	char rev[12];
	static const char pre[] = "/proc/self/fd/";
	if (fd == cached_fd) return cached_is_ipa;
	cached_fd = fd;
	cached_is_ipa = 0;
	for (i = 0; i < sizeof(pre) - 1; i++) path[i] = pre[i];
	v = (unsigned int)fd;
	do { if (nrev >= sizeof(rev)) return 0; rev[nrev++] = (char)('0' + (v % 10U)); v /= 10U; } while (v);
	for (i = 0; i < nrev; i++) path[sizeof(pre) - 1 + i] = rev[nrev - 1 - i];
	path[sizeof(pre) - 1 + nrev] = '\0';
	n = sys_readlink(path, link, sizeof(link) - 1);
	if (n <= 0) return 0;
	link[n] = '\0';
	if (str_eq_n(link, "/dev/ipa", 8)) cached_is_ipa = 1;
	return cached_is_ipa;
}

static int log_open(void)
{
	load_cfg();
	return (int)sys_open(g_cfg.log_path, 0x441, 0600); /* O_WRONLY|O_CREAT|O_APPEND */
}

static void log_s(int fd, const char *s)
{
	sys_write(fd, s, (unsigned long)cstr_len(s, 512));
}

static void log_name(int fd, const char *s)
{
	sys_write(fd, s, (unsigned long)cstr_len(s, IPA_RESOURCE_NAME_MAX));
}

static void log_hex32_fd(int fd, uint32_t val)
{
	static const char hx[] = "0123456789abcdef";
	char b[10];
	int i;
	b[0] = '0'; b[1] = 'x';
	for (i = 0; i < 8; i++) b[2 + i] = hx[(val >> (28 - i * 4)) & 0xf];
	sys_write(fd, b, 10);
}

static void log_kv4(const char *tag, const char *ifname, uint32_t a, uint32_t b, uint32_t c, uint32_t d, const char *action)
{
	int fd = log_open();
	if (fd < 0) return;
	log_s(fd, "[ABI] "); log_s(fd, tag); log_s(fd, " if="); log_name(fd, ifname);
	log_s(fd, " a="); log_hex32_fd(fd, a); log_s(fd, " b="); log_hex32_fd(fd, b);
	log_s(fd, " c="); log_hex32_fd(fd, c); log_s(fd, " d="); log_hex32_fd(fd, d);
	if (action) { log_s(fd, " action="); log_s(fd, action); }
	log_s(fd, "\n");
	sys_close(fd);
}

static void log_prop_tx(const char *tag, const char *ifname, uint32_t idx, uint32_t ip, uint32_t dst, uint32_t alt, uint32_t l2, const char *hdr)
{
	int fd = log_open();
	if (fd < 0) return;
	log_s(fd, "[ABI] "); log_s(fd, tag); log_s(fd, " if="); log_name(fd, ifname);
	log_s(fd, " idx="); log_hex32_fd(fd, idx); log_s(fd, " ip="); log_hex32_fd(fd, ip);
	log_s(fd, " dst="); log_hex32_fd(fd, dst); log_s(fd, " alt="); log_hex32_fd(fd, alt);
	log_s(fd, " l2="); log_hex32_fd(fd, l2); log_s(fd, " hdr="); log_name(fd, hdr);
	log_s(fd, "\n");
	sys_close(fd);
}

static void log_prop_rx(const char *tag, const char *ifname, uint32_t idx, uint32_t ip, uint32_t src, uint32_t l2)
{
	int fd = log_open();
	if (fd < 0) return;
	log_s(fd, "[ABI] "); log_s(fd, tag); log_s(fd, " if="); log_name(fd, ifname);
	log_s(fd, " idx="); log_hex32_fd(fd, idx); log_s(fd, " ip="); log_hex32_fd(fd, ip);
	log_s(fd, " src="); log_hex32_fd(fd, src); log_s(fd, " l2="); log_hex32_fd(fd, l2);
	log_s(fd, "\n");
	sys_close(fd);
}

static int printable_name(const char *s)
{
	int i, seen = 0;
	if (!s) return 0;
	for (i = 0; i < IPA_RESOURCE_NAME_MAX; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c == 0) break;
		if (c < 32 || c > 126) return 0;
		seen = 1;
	}
	return seen;
}

static int tx_score_layout(const uint8_t *base, uint32_t n, uint32_t stride, uint32_t off_dst, uint32_t off_alt, uint32_t off_hdr, uint32_t off_l2)
{
	uint32_t i;
	int score = 0;
	if (n == 0 || n > MAX_PROPS) return 0;
	for (i = 0; i < n; i++) {
		const uint8_t *p = base + i * stride;
		uint32_t ip = rd32(p + 0);
		uint32_t dst = rd32(p + off_dst);
		uint32_t alt = rd32(p + off_alt);
		uint32_t l2 = rd32(p + off_l2);
		if (ip <= 1) score++;
		if (dst > 0 && dst < PIPE_MAX) score++;
		if (alt < PIPE_MAX) score++;
		if (l2 <= 4) score++;
		if (printable_name((const char *)(p + off_hdr))) score++;
	}
	return score;
}

static int rx_score_layout(const uint8_t *base, uint32_t n, uint32_t stride, uint32_t off_src, uint32_t off_l2)
{
	uint32_t i;
	int score = 0;
	if (n == 0 || n > MAX_PROPS) return 0;
	for (i = 0; i < n; i++) {
		const uint8_t *p = base + i * stride;
		uint32_t ip = rd32(p + 0);
		uint32_t src = rd32(p + off_src);
		uint32_t l2 = rd32(p + off_l2);
		if (ip <= 1) score++;
		if (src > 0 && src < PIPE_MAX) score++;
		if (l2 <= 4) score++;
	}
	return score;
}

static int iface_id(const char *name)
{
	if (str_eq_32(name, "eth0")) return 0;
	if (str_eq_32(name, "rmnet_data0")) return 1;
	if (str_eq_32(name, "rmnet_data1")) return 2;
	if (str_eq_32(name, "bridge0")) return 3;
	return 4;
}

static void log_hexdump_once(const char *tag, const char *ifname, const uint8_t *buf, uint32_t len, int type)
{
	static const char hx[] = "0123456789abcdef";
	char out[2 * 96];
	uint32_t i, max;
	int fd, bit;
	load_cfg();
	if (!g_cfg.hexdump_once || !buf) return;
	bit = type * 8 + iface_id(ifname);
	if (bit >= 31) return;
	if (g_hexdump_mask & (1U << bit)) return;
	g_hexdump_mask |= (1U << bit);
	max = len > 96 ? 96 : len;
	for (i = 0; i < max; i++) { out[i * 2] = hx[(buf[i] >> 4) & 0xf]; out[i * 2 + 1] = hx[buf[i] & 0xf]; }
	fd = log_open();
	if (fd < 0) return;
	log_s(fd, "[ABI] "); log_s(fd, tag); log_s(fd, " if="); log_name(fd, ifname); log_s(fd, " hex=");
	sys_write(fd, out, max * 2);
	log_s(fd, "\n");
	sys_close(fd);
}

static void translate_tx_02300_to_new(struct ipa_ioc_query_intf_tx_props *q)
{
	uint32_t n, idx;
	uint8_t tmp[OLD_TX_SIZE];
	uint8_t *base;
	uint32_t new_tx_size = (uint32_t)sizeof(struct ipa_ioc_tx_intf_prop);
	uint32_t new_attr_size = (uint32_t)offsetof(struct ipa_ioc_tx_intf_prop, dst_pipe) - 4U;
	n = q->num_tx_props;
	base = (uint8_t *)&q->tx[0];
	for (idx = n; idx > 0; idx--) {
		uint32_t i = idx - 1;
		uint8_t *oldp = base + i * OLD_TX_SIZE;
		uint8_t *newp = base + i * new_tx_size;
		mem_copy(tmp, oldp, OLD_TX_SIZE);
		mem_zero(newp, new_tx_size);
		mem_copy(newp + offsetof(struct ipa_ioc_tx_intf_prop, ip), tmp, 4);
		mem_copy(newp + offsetof(struct ipa_ioc_tx_intf_prop, attrib), tmp + 4, OLD_ATTR_SIZE < new_attr_size ? OLD_ATTR_SIZE : new_attr_size);
		mem_copy(newp + offsetof(struct ipa_ioc_tx_intf_prop, dst_pipe), tmp + OLD_TX_DST, 4);
		mem_copy(newp + offsetof(struct ipa_ioc_tx_intf_prop, alt_dst_pipe), tmp + OLD_TX_ALT, 4);
		mem_copy(newp + offsetof(struct ipa_ioc_tx_intf_prop, hdr_name), tmp + OLD_TX_HDR, IPA_RESOURCE_NAME_MAX);
		mem_copy(newp + offsetof(struct ipa_ioc_tx_intf_prop, hdr_l2_type), tmp + OLD_TX_L2, 4);
	}
}

static void translate_rx_02300_to_new(struct ipa_ioc_query_intf_rx_props *q)
{
	uint32_t n, idx;
	uint8_t tmp[OLD_RX_SIZE];
	uint8_t *base;
	uint32_t new_rx_size = (uint32_t)sizeof(struct ipa_ioc_rx_intf_prop);
	uint32_t new_attr_size = (uint32_t)offsetof(struct ipa_ioc_rx_intf_prop, src_pipe) - 4U;
	n = q->num_rx_props;
	base = (uint8_t *)&q->rx[0];
	for (idx = n; idx > 0; idx--) {
		uint32_t i = idx - 1;
		uint8_t *oldp = base + i * OLD_RX_SIZE;
		uint8_t *newp = base + i * new_rx_size;
		mem_copy(tmp, oldp, OLD_RX_SIZE);
		mem_zero(newp, new_rx_size);
		mem_copy(newp + offsetof(struct ipa_ioc_rx_intf_prop, ip), tmp, 4);
		mem_copy(newp + offsetof(struct ipa_ioc_rx_intf_prop, attrib), tmp + 4, OLD_ATTR_SIZE < new_attr_size ? OLD_ATTR_SIZE : new_attr_size);
		mem_copy(newp + offsetof(struct ipa_ioc_rx_intf_prop, src_pipe), tmp + OLD_RX_SRC, 4);
		mem_copy(newp + offsetof(struct ipa_ioc_rx_intf_prop, hdr_l2_type), tmp + OLD_RX_L2, 4);
	}
}

static void log_tx_props(const char *tag, const char *ifname, const uint8_t *base, uint32_t n, uint32_t stride, uint32_t off_dst, uint32_t off_alt, uint32_t off_hdr, uint32_t off_l2)
{
	uint32_t i;
	if (n > MAX_PROPS) n = MAX_PROPS;
	for (i = 0; i < n && i < 3; i++) {
		const uint8_t *p = base + i * stride;
		log_prop_tx(tag, ifname, i, rd32(p), rd32(p + off_dst), rd32(p + off_alt), rd32(p + off_l2), (const char *)(p + off_hdr));
	}
}

static void log_rx_props(const char *tag, const char *ifname, const uint8_t *base, uint32_t n, uint32_t stride, uint32_t off_src, uint32_t off_l2)
{
	uint32_t i;
	if (n > MAX_PROPS) n = MAX_PROPS;
	for (i = 0; i < n && i < 3; i++) {
		const uint8_t *p = base + i * stride;
		log_prop_rx(tag, ifname, i, rd32(p), rd32(p + off_src), rd32(p + off_l2));
	}
}

static void handle_query_tx(struct ipa_ioc_query_intf_tx_props *q)
{
	uint32_t n, new_threshold, old_threshold;
	int old_score, new_score;
	const char *action = "none";
	uint8_t *base;
	load_cfg();
	if (!q || !g_cfg.tx || !iface_allowed(q->name)) return;
	n = q->num_tx_props;
	if (n == 0 || n > MAX_PROPS) { log_kv4("QUERY_TX_SKIP", q->name, n, 0, 0, 0, "bad_n"); return; }
	base = (uint8_t *)&q->tx[0];
	log_hexdump_once("QUERY_TX_RAW", q->name, (const uint8_t *)q, QUERY_PREFIX_SIZE + n * OLD_TX_SIZE, 0);
	old_score = tx_score_layout(base, n, OLD_TX_SIZE, OLD_TX_DST, OLD_TX_ALT, OLD_TX_HDR, OLD_TX_L2);
	new_score = tx_score_layout(base, n, (uint32_t)sizeof(struct ipa_ioc_tx_intf_prop), (uint32_t)offsetof(struct ipa_ioc_tx_intf_prop, dst_pipe), (uint32_t)offsetof(struct ipa_ioc_tx_intf_prop, alt_dst_pipe), (uint32_t)offsetof(struct ipa_ioc_tx_intf_prop, hdr_name), (uint32_t)offsetof(struct ipa_ioc_tx_intf_prop, hdr_l2_type));
	old_threshold = n * 4U;
	new_threshold = n * 4U;
	log_tx_props("TX_OLD", q->name, base, n, OLD_TX_SIZE, OLD_TX_DST, OLD_TX_ALT, OLD_TX_HDR, OLD_TX_L2);
	if (iface_translate_allowed(q->name)) {
		if ((uint32_t)old_score >= old_threshold && (uint32_t)new_score < new_threshold) {
			translate_tx_02300_to_new(q);
			action = "translate_old_to_new";
		} else if ((uint32_t)old_score >= old_threshold && (uint32_t)new_score >= new_threshold) {
			action = g_cfg.fail_closed ? "ambiguous_fail_closed" : "ambiguous_noop";
		} else {
			action = "noop_score";
		}
	} else {
		action = "log_only";
	}
	log_kv4("QUERY_TX", q->name, n, (uint32_t)old_score, (uint32_t)new_score, old_threshold, action);
	log_tx_props("TX_NEW", q->name, (const uint8_t *)&q->tx[0], q->num_tx_props, (uint32_t)sizeof(struct ipa_ioc_tx_intf_prop), (uint32_t)offsetof(struct ipa_ioc_tx_intf_prop, dst_pipe), (uint32_t)offsetof(struct ipa_ioc_tx_intf_prop, alt_dst_pipe), (uint32_t)offsetof(struct ipa_ioc_tx_intf_prop, hdr_name), (uint32_t)offsetof(struct ipa_ioc_tx_intf_prop, hdr_l2_type));
}

static void handle_query_rx(struct ipa_ioc_query_intf_rx_props *q)
{
	uint32_t n, threshold;
	int old_score, new_score;
	const char *action = "none";
	uint8_t *base;
	load_cfg();
	if (!q || !g_cfg.rx || !iface_allowed(q->name)) return;
	n = q->num_rx_props;
	if (n == 0 || n > MAX_PROPS) { log_kv4("QUERY_RX_SKIP", q->name, n, 0, 0, 0, "bad_n"); return; }
	base = (uint8_t *)&q->rx[0];
	log_hexdump_once("QUERY_RX_RAW", q->name, (const uint8_t *)q, QUERY_PREFIX_SIZE + n * OLD_RX_SIZE, 1);
	old_score = rx_score_layout(base, n, OLD_RX_SIZE, OLD_RX_SRC, OLD_RX_L2);
	new_score = rx_score_layout(base, n, (uint32_t)sizeof(struct ipa_ioc_rx_intf_prop), (uint32_t)offsetof(struct ipa_ioc_rx_intf_prop, src_pipe), (uint32_t)offsetof(struct ipa_ioc_rx_intf_prop, hdr_l2_type));
	threshold = n * 3U;
	log_rx_props("RX_OLD", q->name, base, n, OLD_RX_SIZE, OLD_RX_SRC, OLD_RX_L2);
	if (iface_translate_allowed(q->name)) {
		if ((uint32_t)old_score >= threshold && (uint32_t)new_score < threshold) { translate_rx_02300_to_new(q); action = "translate_old_to_new"; }
		else if ((uint32_t)old_score >= threshold && (uint32_t)new_score >= threshold) action = g_cfg.fail_closed ? "ambiguous_fail_closed" : "ambiguous_noop";
		else action = "noop_score";
	} else action = "log_only";
	log_kv4("QUERY_RX", q->name, n, (uint32_t)old_score, (uint32_t)new_score, threshold, action);
	log_rx_props("RX_NEW", q->name, (const uint8_t *)&q->rx[0], q->num_rx_props, (uint32_t)sizeof(struct ipa_ioc_rx_intf_prop), (uint32_t)offsetof(struct ipa_ioc_rx_intf_prop, src_pipe), (uint32_t)offsetof(struct ipa_ioc_rx_intf_prop, hdr_l2_type));
}

static void handle_query_ext(struct ipa_ioc_query_intf_ext_props *q)
{
	uint32_t i, n;
	int fd;
	load_cfg();
	if (!q || !g_cfg.ext || !iface_allowed(q->name)) return;
	n = q->num_ext_props;
	log_hexdump_once("QUERY_EXT_RAW", q->name, (const uint8_t *)q, QUERY_PREFIX_SIZE + (n > MAX_PROPS ? MAX_PROPS : n) * (uint32_t)sizeof(struct ipa_ioc_ext_intf_prop), 2);
	fd = log_open();
	if (fd < 0) return;
	log_s(fd, "[ABI] QUERY_EXT if="); log_name(fd, q->name); log_s(fd, " n="); log_hex32_fd(fd, n); log_s(fd, " action=log_only_layout_same\n");
	for (i = 0; i < n && i < 4; i++) {
		log_s(fd, "[ABI] EXT if="); log_name(fd, q->name); log_s(fd, " idx="); log_hex32_fd(fd, i);
		log_s(fd, " ip="); log_hex32_fd(fd, (uint32_t)q->ext[i].ip);
		log_s(fd, " action="); log_hex32_fd(fd, (uint32_t)q->ext[i].action);
		log_s(fd, " rtidx="); log_hex32_fd(fd, q->ext[i].rt_tbl_idx);
		log_s(fd, " mux="); log_hex32_fd(fd, (uint32_t)q->ext[i].mux_id);
		log_s(fd, " xlat="); log_hex32_fd(fd, (uint32_t)q->ext[i].is_xlat_rule);
		log_s(fd, "\n");
	}
	sys_close(fd);
}

static void attr_new_to_old(uint8_t *oldp, const uint8_t *newp)
{
	mem_zero(oldp, OLD_ATTR_SIZE);
	mem_copy(oldp, newp, ATTR_COMMON_SIZE);
}

static void rt_rule_new_to_old(uint8_t *oldp, const uint8_t *newp)
{
	mem_zero(oldp, OLD_RT_RULE_SIZE);
	mem_copy(oldp, newp, 12U);
	attr_new_to_old(oldp + 12U, newp + 12U);
	mem_copy(oldp + 164U, newp + offsetof(struct ipa_rt_rule, max_prio), 4U);
}

static void flt_rule_new_to_old(uint8_t *oldp, const uint8_t *newp)
{
	mem_zero(oldp, OLD_FLT_RULE_SIZE);
	mem_copy(oldp, newp, 12U);
	attr_new_to_old(oldp + 12U, newp + 12U);
	mem_copy(oldp + 164U, newp + offsetof(struct ipa_flt_rule, eq_attrib), 184U);
	mem_copy(oldp + 348U, newp + offsetof(struct ipa_flt_rule, rt_tbl_idx), 12U);
}

static void log_translate_ioctl(const char *tag, uint32_t n, long ret)
{
	int fd = log_open();
	if (fd < 0) return;
	log_s(fd, "[ABI] "); log_s(fd, tag); log_s(fd, " translated n=");
	log_hex32_fd(fd, n); log_s(fd, " ret="); log_hex32_fd(fd, (uint32_t)(unsigned long)ret);
	log_s(fd, "\n");
	sys_close(fd);
}

static int translate_add_rt_ioctl(int fd, unsigned long request, struct ipa_ioc_add_rt_rule *arg, long *retp)
{
	uint8_t buf[44U + RT_FLT_MAX_TRANSLATE_RULES * OLD_RT_ADD_SIZE];
	uint32_t i, n;
	uint8_t *old_rules;
	uint8_t *new_rules;
	long ret;
	if (!arg || !retp) return 0;
	n = (uint32_t)arg->num_rules;
	if (n == 0 || n > RT_FLT_MAX_TRANSLATE_RULES) return 0;
	mem_zero(buf, sizeof(buf));
	mem_copy(buf, (const uint8_t *)arg, 44U);
	old_rules = buf + 44U;
	new_rules = (uint8_t *)&arg->rules[0];
	for (i = 0; i < n; i++) {
		uint8_t *oldp = old_rules + i * OLD_RT_ADD_SIZE;
		uint8_t *newp = new_rules + i * sizeof(struct ipa_rt_rule_add);
		rt_rule_new_to_old(oldp, newp);
		mem_copy(oldp + OLD_RT_ADD_AT_REAR, newp + offsetof(struct ipa_rt_rule_add, at_rear), 1U);
	}
	ret = sys_ioctl(fd, request, buf);
	for (i = 0; i < n; i++) {
		uint8_t *oldp = old_rules + i * OLD_RT_ADD_SIZE;
		uint8_t *newp = new_rules + i * sizeof(struct ipa_rt_rule_add);
		mem_copy(newp + offsetof(struct ipa_rt_rule_add, rt_rule_hdl), oldp + OLD_RT_ADD_HDL, 4U);
		mem_copy(newp + offsetof(struct ipa_rt_rule_add, status), oldp + OLD_RT_ADD_STATUS, 4U);
	}
	*retp = ret;
	log_translate_ioctl("ADD_RT", n, ret);
	return 1;
}

static int translate_add_flt_ioctl(int fd, unsigned long request, struct ipa_ioc_add_flt_rule *arg, long *retp)
{
	uint8_t buf[16U + RT_FLT_MAX_TRANSLATE_RULES * OLD_FLT_ADD_SIZE];
	uint32_t i, n;
	uint8_t *old_rules;
	uint8_t *new_rules;
	long ret;
	if (!arg || !retp) return 0;
	n = (uint32_t)arg->num_rules;
	if (n == 0 || n > RT_FLT_MAX_TRANSLATE_RULES) return 0;
	mem_zero(buf, sizeof(buf));
	mem_copy(buf, (const uint8_t *)arg, 16U);
	old_rules = buf + 16U;
	new_rules = (uint8_t *)&arg->rules[0];
	for (i = 0; i < n; i++) {
		uint8_t *oldp = old_rules + i * OLD_FLT_ADD_SIZE;
		uint8_t *newp = new_rules + i * sizeof(struct ipa_flt_rule_add);
		flt_rule_new_to_old(oldp, newp);
		mem_copy(oldp + OLD_FLT_ADD_AT_REAR, newp + offsetof(struct ipa_flt_rule_add, at_rear), 1U);
	}
	ret = sys_ioctl(fd, request, buf);
	for (i = 0; i < n; i++) {
		uint8_t *oldp = old_rules + i * OLD_FLT_ADD_SIZE;
		uint8_t *newp = new_rules + i * sizeof(struct ipa_flt_rule_add);
		mem_copy(newp + offsetof(struct ipa_flt_rule_add, flt_rule_hdl), oldp + OLD_FLT_ADD_HDL, 4U);
		mem_copy(newp + offsetof(struct ipa_flt_rule_add, status), oldp + OLD_FLT_ADD_STATUS, 4U);
	}
	*retp = ret;
	log_translate_ioctl("ADD_FLT", n, ret);
	return 1;
}

static int translate_add_rt_after_ioctl(int fd, unsigned long request, struct ipa_ioc_add_rt_rule_after *arg, long *retp)
{
	uint8_t buf[48U + RT_FLT_MAX_TRANSLATE_RULES * OLD_RT_ADD_SIZE];
	uint32_t i, n;
	uint8_t *old_rules;
	uint8_t *new_rules;
	long ret;
	if (!arg || !retp) return 0;
	n = (uint32_t)arg->num_rules;
	if (n == 0 || n > RT_FLT_MAX_TRANSLATE_RULES) return 0;
	mem_zero(buf, sizeof(buf));
	mem_copy(buf, (const uint8_t *)arg, 48U);
	old_rules = buf + 48U;
	new_rules = (uint8_t *)&arg->rules[0];
	for (i = 0; i < n; i++) {
		uint8_t *oldp = old_rules + i * OLD_RT_ADD_SIZE;
		uint8_t *newp = new_rules + i * sizeof(struct ipa_rt_rule_add);
		rt_rule_new_to_old(oldp, newp);
		mem_copy(oldp + OLD_RT_ADD_AT_REAR, newp + offsetof(struct ipa_rt_rule_add, at_rear), 1U);
	}
	ret = sys_ioctl(fd, request, buf);
	for (i = 0; i < n; i++) {
		uint8_t *oldp = old_rules + i * OLD_RT_ADD_SIZE;
		uint8_t *newp = new_rules + i * sizeof(struct ipa_rt_rule_add);
		mem_copy(newp + offsetof(struct ipa_rt_rule_add, rt_rule_hdl), oldp + OLD_RT_ADD_HDL, 4U);
		mem_copy(newp + offsetof(struct ipa_rt_rule_add, status), oldp + OLD_RT_ADD_STATUS, 4U);
	}
	*retp = ret;
	log_translate_ioctl("ADD_RT_AFTER", n, ret);
	return 1;
}

static int translate_add_flt_after_ioctl(int fd, unsigned long request, struct ipa_ioc_add_flt_rule_after *arg, long *retp)
{
	uint8_t buf[20U + RT_FLT_MAX_TRANSLATE_RULES * OLD_FLT_ADD_SIZE];
	uint32_t i, n;
	uint8_t *old_rules;
	uint8_t *new_rules;
	long ret;
	if (!arg || !retp) return 0;
	n = (uint32_t)arg->num_rules;
	if (n == 0 || n > RT_FLT_MAX_TRANSLATE_RULES) return 0;
	mem_zero(buf, sizeof(buf));
	mem_copy(buf, (const uint8_t *)arg, 20U);
	old_rules = buf + 20U;
	new_rules = (uint8_t *)&arg->rules[0];
	for (i = 0; i < n; i++) {
		uint8_t *oldp = old_rules + i * OLD_FLT_ADD_SIZE;
		uint8_t *newp = new_rules + i * sizeof(struct ipa_flt_rule_add);
		flt_rule_new_to_old(oldp, newp);
		mem_copy(oldp + OLD_FLT_ADD_AT_REAR, newp + offsetof(struct ipa_flt_rule_add, at_rear), 1U);
	}
	ret = sys_ioctl(fd, request, buf);
	for (i = 0; i < n; i++) {
		uint8_t *oldp = old_rules + i * OLD_FLT_ADD_SIZE;
		uint8_t *newp = new_rules + i * sizeof(struct ipa_flt_rule_add);
		mem_copy(newp + offsetof(struct ipa_flt_rule_add, flt_rule_hdl), oldp + OLD_FLT_ADD_HDL, 4U);
		mem_copy(newp + offsetof(struct ipa_flt_rule_add, status), oldp + OLD_FLT_ADD_STATUS, 4U);
	}
	*retp = ret;
	log_translate_ioctl("ADD_FLT_AFTER", n, ret);
	return 1;
}

static int translate_generate_flt_eq_ioctl(int fd, unsigned long request, struct ipa_ioc_generate_flt_eq *arg, long *retp)
{
	uint8_t buf[OLD_GEN_EQ_SIZE];
	long ret;
	if (!arg || !retp) return 0;
	mem_zero(buf, sizeof(buf));
	mem_copy(buf, (const uint8_t *)arg, 4U);
	attr_new_to_old(buf + OLD_GEN_EQ_ATTR, ((const uint8_t *)arg) + offsetof(struct ipa_ioc_generate_flt_eq, attrib));
	ret = sys_ioctl(fd, request, buf);
	if (ret >= 0) mem_copy(((uint8_t *)arg) + offsetof(struct ipa_ioc_generate_flt_eq, eq_attrib), buf + OLD_GEN_EQ_EQ, 184U);
	*retp = ret;
	log_translate_ioctl("GENERATE_FLT_EQ", 1U, ret);
	return 1;
}

static int maybe_translate_rt_flt_ioctl(int fd, unsigned long request, unsigned long nr, void *arg, long *retp)
{
	load_cfg();
	if (!g_cfg.rt_flt_translate || !arg || !retp) return 0;
	if (nr == IPA_IOCTL_ADD_RT_RULE) return translate_add_rt_ioctl(fd, request, (struct ipa_ioc_add_rt_rule *)arg, retp);
	if (nr == IPA_IOCTL_ADD_FLT_RULE) return translate_add_flt_ioctl(fd, request, (struct ipa_ioc_add_flt_rule *)arg, retp);
	if (nr == IPA_IOCTL_ADD_RT_RULE_AFTER) return translate_add_rt_after_ioctl(fd, request, (struct ipa_ioc_add_rt_rule_after *)arg, retp);
	if (nr == IPA_IOCTL_ADD_FLT_RULE_AFTER) return translate_add_flt_after_ioctl(fd, request, (struct ipa_ioc_add_flt_rule_after *)arg, retp);
	if (nr == IPA_IOCTL_GENERATE_FLT_EQ) return translate_generate_flt_eq_ioctl(fd, request, (struct ipa_ioc_generate_flt_eq *)arg, retp);
	return 0;
}

static void log_rt_flt(unsigned long nr, void *arg, long ret)
{
	int fd;
	load_cfg();
	if (!g_cfg.rt_flt_log || !arg) return;
	fd = log_open();
	if (fd < 0) return;
	log_s(fd, "[ABI] IOC nr="); log_hex32_fd(fd, (uint32_t)nr); log_s(fd, " ret="); log_hex32_fd(fd, (uint32_t)(unsigned long)ret);
	if (nr == IPA_IOCTL_ADD_RT_RULE) {
		struct ipa_ioc_add_rt_rule *r = (struct ipa_ioc_add_rt_rule *)arg;
		log_s(fd, " ADD_RT ip="); log_hex32_fd(fd, (uint32_t)r->ip); log_s(fd, " tbl="); log_name(fd, r->rt_tbl_name); log_s(fd, " n="); log_hex32_fd(fd, (uint32_t)r->num_rules);
	} else if (nr == IPA_IOCTL_ADD_RT_RULE_V2) {
		struct ipa_ioc_add_rt_rule_v2 *r = (struct ipa_ioc_add_rt_rule_v2 *)arg;
		log_s(fd, " ADD_RT_V2 ip="); log_hex32_fd(fd, (uint32_t)r->ip); log_s(fd, " tbl="); log_name(fd, r->rt_tbl_name); log_s(fd, " n="); log_hex32_fd(fd, (uint32_t)r->num_rules); log_s(fd, " sz="); log_hex32_fd(fd, r->rule_add_size);
	} else if (nr == IPA_IOCTL_ADD_RT_RULE_AFTER) {
		struct ipa_ioc_add_rt_rule_after *r = (struct ipa_ioc_add_rt_rule_after *)arg;
		log_s(fd, " ADD_RT_AFTER ip="); log_hex32_fd(fd, (uint32_t)r->ip); log_s(fd, " tbl="); log_name(fd, r->rt_tbl_name); log_s(fd, " n="); log_hex32_fd(fd, (uint32_t)r->num_rules); log_s(fd, " after="); log_hex32_fd(fd, r->add_after_hdl);
	} else if (nr == IPA_IOCTL_ADD_RT_RULE_AFTER_V2) {
		struct ipa_ioc_add_rt_rule_after_v2 *r = (struct ipa_ioc_add_rt_rule_after_v2 *)arg;
		log_s(fd, " ADD_RT_AFTER_V2 ip="); log_hex32_fd(fd, (uint32_t)r->ip); log_s(fd, " tbl="); log_name(fd, r->rt_tbl_name); log_s(fd, " n="); log_hex32_fd(fd, (uint32_t)r->num_rules); log_s(fd, " after="); log_hex32_fd(fd, r->add_after_hdl); log_s(fd, " sz="); log_hex32_fd(fd, r->rule_add_size);
	} else if (nr == IPA_IOCTL_MDFY_RT_RULE) {
		struct ipa_ioc_mdfy_rt_rule *r = (struct ipa_ioc_mdfy_rt_rule *)arg;
		log_s(fd, " MDFY_RT ip="); log_hex32_fd(fd, (uint32_t)r->ip); log_s(fd, " n="); log_hex32_fd(fd, (uint32_t)r->num_rules);
	} else if (nr == IPA_IOCTL_MDFY_RT_RULE_V2) {
		struct ipa_ioc_mdfy_rt_rule_v2 *r = (struct ipa_ioc_mdfy_rt_rule_v2 *)arg;
		log_s(fd, " MDFY_RT_V2 ip="); log_hex32_fd(fd, (uint32_t)r->ip); log_s(fd, " n="); log_hex32_fd(fd, (uint32_t)r->num_rules); log_s(fd, " sz="); log_hex32_fd(fd, r->rule_mdfy_size);
	} else if (nr == IPA_IOCTL_DEL_RT_RULE) {
		struct ipa_ioc_del_rt_rule *r = (struct ipa_ioc_del_rt_rule *)arg;
		log_s(fd, " DEL_RT ip="); log_hex32_fd(fd, (uint32_t)r->ip); log_s(fd, " n="); log_hex32_fd(fd, (uint32_t)r->num_hdls);
	} else if (nr == IPA_IOCTL_ADD_FLT_RULE) {
		struct ipa_ioc_add_flt_rule *f = (struct ipa_ioc_add_flt_rule *)arg;
		log_s(fd, " ADD_FLT ip="); log_hex32_fd(fd, (uint32_t)f->ip); log_s(fd, " ep="); log_hex32_fd(fd, (uint32_t)f->ep); log_s(fd, " global="); log_hex32_fd(fd, (uint32_t)f->global); log_s(fd, " n="); log_hex32_fd(fd, (uint32_t)f->num_rules);
	} else if (nr == IPA_IOCTL_ADD_FLT_RULE_V2) {
		struct ipa_ioc_add_flt_rule_v2 *f = (struct ipa_ioc_add_flt_rule_v2 *)arg;
		log_s(fd, " ADD_FLT_V2 ip="); log_hex32_fd(fd, (uint32_t)f->ip); log_s(fd, " ep="); log_hex32_fd(fd, (uint32_t)f->ep); log_s(fd, " global="); log_hex32_fd(fd, (uint32_t)f->global); log_s(fd, " n="); log_hex32_fd(fd, (uint32_t)f->num_rules); log_s(fd, " sz="); log_hex32_fd(fd, f->flt_rule_size);
	} else if (nr == IPA_IOCTL_ADD_FLT_RULE_AFTER) {
		struct ipa_ioc_add_flt_rule_after *f = (struct ipa_ioc_add_flt_rule_after *)arg;
		log_s(fd, " ADD_FLT_AFTER ip="); log_hex32_fd(fd, (uint32_t)f->ip); log_s(fd, " ep="); log_hex32_fd(fd, (uint32_t)f->ep); log_s(fd, " n="); log_hex32_fd(fd, (uint32_t)f->num_rules); log_s(fd, " after="); log_hex32_fd(fd, f->add_after_hdl);
	} else if (nr == IPA_IOCTL_ADD_FLT_RULE_AFTER_V2) {
		struct ipa_ioc_add_flt_rule_after_v2 *f = (struct ipa_ioc_add_flt_rule_after_v2 *)arg;
		log_s(fd, " ADD_FLT_AFTER_V2 ip="); log_hex32_fd(fd, (uint32_t)f->ip); log_s(fd, " ep="); log_hex32_fd(fd, (uint32_t)f->ep); log_s(fd, " n="); log_hex32_fd(fd, (uint32_t)f->num_rules); log_s(fd, " after="); log_hex32_fd(fd, f->add_after_hdl); log_s(fd, " sz="); log_hex32_fd(fd, f->flt_rule_size);
	} else if (nr == IPA_IOCTL_MDFY_FLT_RULE) {
		struct ipa_ioc_mdfy_flt_rule *f = (struct ipa_ioc_mdfy_flt_rule *)arg;
		log_s(fd, " MDFY_FLT ip="); log_hex32_fd(fd, (uint32_t)f->ip); log_s(fd, " n="); log_hex32_fd(fd, (uint32_t)f->num_rules);
	} else if (nr == IPA_IOCTL_MDFY_FLT_RULE_V2) {
		struct ipa_ioc_mdfy_flt_rule_v2 *f = (struct ipa_ioc_mdfy_flt_rule_v2 *)arg;
		log_s(fd, " MDFY_FLT_V2 ip="); log_hex32_fd(fd, (uint32_t)f->ip); log_s(fd, " n="); log_hex32_fd(fd, (uint32_t)f->num_rules); log_s(fd, " sz="); log_hex32_fd(fd, f->rule_mdfy_size);
	} else if (nr == IPA_IOCTL_DEL_FLT_RULE) {
		struct ipa_ioc_del_flt_rule *f = (struct ipa_ioc_del_flt_rule *)arg;
		log_s(fd, " DEL_FLT ip="); log_hex32_fd(fd, (uint32_t)f->ip); log_s(fd, " n="); log_hex32_fd(fd, (uint32_t)f->num_hdls);
	} else if (nr == IPA_IOCTL_GENERATE_FLT_EQ) {
		log_s(fd, " GENERATE_FLT_EQ");
	}
	log_s(fd, "\n");
	sys_close(fd);
}

int ioctl(int fd, unsigned long request, ...)
{
	va_list ap;
	void *arg;
	long ret;
	unsigned long type;
	unsigned long nr;
	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);
	type = (request >> _IOC_TYPESHIFT) & _IOC_TYPEMASK;
	nr = (request >> _IOC_NRSHIFT) & _IOC_NRMASK;
	if (type == IPA_IOC_MAGIC && !fd_is_ipa_dev(fd)) return (int)sys_ioctl(fd, request, arg);
	if (type == IPA_IOC_MAGIC && maybe_translate_rt_flt_ioctl(fd, request, nr, arg, &ret)) {
		log_rt_flt(nr, arg, ret);
		return (int)ret;
	}
	ret = sys_ioctl(fd, request, arg);
	if (ret >= 0 && type == IPA_IOC_MAGIC && arg) {
		if (nr == IPA_IOCTL_QUERY_INTF_TX_PROPS) handle_query_tx((struct ipa_ioc_query_intf_tx_props *)arg);
		else if (nr == IPA_IOCTL_QUERY_INTF_RX_PROPS) handle_query_rx((struct ipa_ioc_query_intf_rx_props *)arg);
		else if (nr == IPA_IOCTL_QUERY_INTF_EXT_PROPS) handle_query_ext((struct ipa_ioc_query_intf_ext_props *)arg);
	}
	if (type == IPA_IOC_MAGIC && arg &&
	    (nr == IPA_IOCTL_ADD_RT_RULE || nr == IPA_IOCTL_ADD_RT_RULE_V2 ||
	     nr == IPA_IOCTL_ADD_RT_RULE_AFTER || nr == IPA_IOCTL_ADD_RT_RULE_AFTER_V2 ||
	     nr == IPA_IOCTL_MDFY_RT_RULE || nr == IPA_IOCTL_MDFY_RT_RULE_V2 ||
	     nr == IPA_IOCTL_DEL_RT_RULE ||
	     nr == IPA_IOCTL_ADD_FLT_RULE || nr == IPA_IOCTL_ADD_FLT_RULE_V2 ||
	     nr == IPA_IOCTL_ADD_FLT_RULE_AFTER || nr == IPA_IOCTL_ADD_FLT_RULE_AFTER_V2 ||
	     nr == IPA_IOCTL_MDFY_FLT_RULE || nr == IPA_IOCTL_MDFY_FLT_RULE_V2 ||
	     nr == IPA_IOCTL_DEL_FLT_RULE || nr == IPA_IOCTL_GENERATE_FLT_EQ))
		log_rt_flt(nr, arg, ret);
	return (int)ret;
}

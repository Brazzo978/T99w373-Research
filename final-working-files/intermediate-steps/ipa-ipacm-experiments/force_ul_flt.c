#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/msm_ipa.h>

static int get_rt_tbl(int fd, enum ipa_ip_type ip, const char *name, uint32_t *hdl)
{
    struct ipa_ioc_get_rt_tbl g;
    memset(&g, 0, sizeof(g));
    g.ip = ip;
    strncpy(g.name, name, IPA_RESOURCE_NAME_MAX - 1);
    if (ioctl(fd, IPA_IOC_GET_RT_TBL, &g) < 0)
        return -1;
    *hdl = g.hdl;
    return 0;
}

static int add_flt_rule(int fd, enum ipa_ip_type ip, enum ipa_client_type ep, uint32_t rt_tbl_hdl)
{
    size_t sz = sizeof(struct ipa_ioc_add_flt_rule) + sizeof(struct ipa_flt_rule_add);
    struct ipa_ioc_add_flt_rule *fr = calloc(1, sz);
    int ret;

    if (!fr)
        return -1;

    fr->commit = 1;
    fr->ip = ip;
    fr->ep = ep;
    fr->global = 0;
    fr->num_rules = 1;

    fr->rules[0].at_rear = 1;
    fr->rules[0].rule.action = IPA_PASS_TO_ROUTING;
    fr->rules[0].rule.rt_tbl_hdl = rt_tbl_hdl;
    fr->rules[0].rule.attrib.attrib_mask = 0;
    fr->rules[0].status = -1;

    errno = 0;
    ret = ioctl(fd, IPA_IOC_ADD_FLT_RULE, fr);
    printf("ADD_FLT ip=%d ep=%d ret=%d errno=%d(%s) status=%d hdl=0x%x rt_tbl=0x%x\n",
           ip, ep, ret, errno, strerror(errno),
           fr->rules[0].status, fr->rules[0].flt_rule_hdl, rt_tbl_hdl);

    free(fr);
    return ret;
}

int main(void)
{
    const char *tbls[] = {"ipa_dflt_wan_rt", "ipa_dflt_rt", "default", "wan"};
    int fd = open("/dev/ipa", O_RDWR);
    uint32_t h4 = 0, h6 = 0;
    size_t i;

    if (fd < 0) {
        perror("open /dev/ipa");
        return 1;
    }

    for (i = 0; i < sizeof(tbls)/sizeof(tbls[0]); i++) {
        if (!h4 && get_rt_tbl(fd, IPA_IP_v4, tbls[i], &h4) == 0)
            printf("GET_RT v4 name=%s hdl=0x%x\n", tbls[i], h4);
        if (!h6 && get_rt_tbl(fd, IPA_IP_v6, tbls[i], &h6) == 0)
            printf("GET_RT v6 name=%s hdl=0x%x\n", tbls[i], h6);
    }

    if (!h4 || !h6)
        printf("WARN: missing rt table handles v4=0x%x v6=0x%x\n", h4, h6);

    if (h4)
        add_flt_rule(fd, IPA_IP_v4, IPA_CLIENT_RTK_ETHERNET_PROD, h4);
    if (h6)
        add_flt_rule(fd, IPA_IP_v6, IPA_CLIENT_RTK_ETHERNET_PROD, h6);

    close(fd);
    return 0;
}

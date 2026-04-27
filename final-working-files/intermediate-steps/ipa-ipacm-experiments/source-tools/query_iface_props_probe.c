#include <errno.h>
#include <fcntl.h>
#include <linux/msm_ipa.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void dump_one(const char *ifname)
{
    int fd = open("/dev/ipa", O_RDWR);
    struct ipa_ioc_query_intf qi;
    int ret;
    unsigned int i;

    if (fd < 0) { perror("open /dev/ipa"); return; }
    memset(&qi, 0, sizeof(qi));
    snprintf(qi.name, sizeof(qi.name), "%s", ifname);
    errno = 0;
    ret = ioctl(fd, IPA_IOC_QUERY_INTF, &qi);
    printf("IF name=%s query_ret=%d errno=%d(%s) tx=%u rx=%u ext=%u excp=%u\n",
           ifname, ret, errno, strerror(errno), qi.num_tx_props, qi.num_rx_props,
           qi.num_ext_props, (unsigned)qi.excp_pipe);

    if (qi.num_tx_props > 0 && qi.num_tx_props < 32) {
        size_t sz = sizeof(struct ipa_ioc_query_intf_tx_props) + qi.num_tx_props * sizeof(struct ipa_ioc_tx_intf_prop);
        struct ipa_ioc_query_intf_tx_props *qt = calloc(1, sz);
        snprintf(qt->name, sizeof(qt->name), "%s", ifname);
        qt->num_tx_props = qi.num_tx_props;
        errno = 0;
        ret = ioctl(fd, IPA_IOC_QUERY_INTF_TX_PROPS, qt);
        printf("  TX ret=%d errno=%d(%s) n=%u\n", ret, errno, strerror(errno), qt->num_tx_props);
        for (i = 0; i < qt->num_tx_props && i < 16; i++) {
            printf("    tx[%u] ip=%u dst=%u alt=%u l2=%u hdr='%s' attrib=0x%x\n",
                   i, (unsigned)qt->tx[i].ip, (unsigned)qt->tx[i].dst_pipe,
                   (unsigned)qt->tx[i].alt_dst_pipe, (unsigned)qt->tx[i].hdr_l2_type,
                   qt->tx[i].hdr_name, qt->tx[i].attrib.attrib_mask);
        }
        free(qt);
    }

    if (qi.num_rx_props > 0 && qi.num_rx_props < 32) {
        size_t sz = sizeof(struct ipa_ioc_query_intf_rx_props) + qi.num_rx_props * sizeof(struct ipa_ioc_rx_intf_prop);
        struct ipa_ioc_query_intf_rx_props *qr = calloc(1, sz);
        snprintf(qr->name, sizeof(qr->name), "%s", ifname);
        qr->num_rx_props = qi.num_rx_props;
        errno = 0;
        ret = ioctl(fd, IPA_IOC_QUERY_INTF_RX_PROPS, qr);
        printf("  RX ret=%d errno=%d(%s) n=%u\n", ret, errno, strerror(errno), qr->num_rx_props);
        for (i = 0; i < qr->num_rx_props && i < 16; i++) {
            printf("    rx[%u] ip=%u src=%u l2=%u attrib=0x%x\n",
                   i, (unsigned)qr->rx[i].ip, (unsigned)qr->rx[i].src_pipe,
                   (unsigned)qr->rx[i].hdr_l2_type, qr->rx[i].attrib.attrib_mask);
        }
        free(qr);
    }

    if (qi.num_ext_props > 0 && qi.num_ext_props < 32) {
        size_t sz = sizeof(struct ipa_ioc_query_intf_ext_props) + qi.num_ext_props * sizeof(struct ipa_ioc_ext_intf_prop);
        struct ipa_ioc_query_intf_ext_props *qe = calloc(1, sz);
        snprintf(qe->name, sizeof(qe->name), "%s", ifname);
        qe->num_ext_props = qi.num_ext_props;
        errno = 0;
        ret = ioctl(fd, IPA_IOC_QUERY_INTF_EXT_PROPS, qe);
        printf("  EXT ret=%d errno=%d(%s) n=%u\n", ret, errno, strerror(errno), qe->num_ext_props);
        for (i = 0; i < qe->num_ext_props && i < 16; i++) {
            printf("    ext[%u] ip=%u action=%u rtidx=%u mux=%u filter=0x%x xlat=%u attrib=0x%x\n",
                   i, (unsigned)qe->ext[i].ip, (unsigned)qe->ext[i].action,
                   qe->ext[i].rt_tbl_idx, qe->ext[i].mux_id, qe->ext[i].filter_hdl,
                   qe->ext[i].is_xlat_rule, qe->ext[i].eq_attrib.rule_eq_bitmap);
        }
        free(qe);
    }
    close(fd);
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        int i;
        for (i = 1; i < argc; i++) dump_one(argv[i]);
    } else {
        dump_one("eth0");
        dump_one("bridge0");
        dump_one("rmnet_data0");
        dump_one("rmnet_data1");
    }
    return 0;
}

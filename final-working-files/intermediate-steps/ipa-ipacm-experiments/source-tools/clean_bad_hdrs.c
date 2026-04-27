#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/msm_ipa.h>

static int get_hdr(int fd, const char *name, uint32_t *hdl)
{
    struct ipa_ioc_get_hdr g;
    memset(&g, 0, sizeof(g));
    strncpy(g.name, name, IPA_RESOURCE_NAME_MAX - 1);
    if (ioctl(fd, IPA_IOC_GET_HDR, &g) < 0)
        return -1;
    *hdl = g.hdl;
    return 0;
}

static int del_hdr(int fd, uint32_t hdl)
{
    size_t sz = sizeof(struct ipa_ioc_del_hdr) + sizeof(struct ipa_hdr_del);
    struct ipa_ioc_del_hdr *d = calloc(1, sz);
    int ret;
    if (!d)
        return -1;
    d->commit = 1;
    d->num_hdls = 1;
    d->hdl[0].hdl = hdl;
    d->hdl[0].status = -1;

    errno = 0;
    ret = ioctl(fd, IPA_IOC_DEL_HDR, d);
    printf("DEL_HDR hdl=0x%x ret=%d errno=%d(%s) status=%d\n",
           hdl, ret, errno, strerror(errno), d->hdl[0].status);
    free(d);
    return ret;
}

int main(void)
{
    const char *names[] = {
        "35_IPACM_ODU_v4",
        "35_IPACM_ODU_v6",
        "35_IPACM_ETH_v4_0",
        "35_IPACM_ETH_v6_0",
        "35_IPACM_ETH_v4_1",
        "35_IPACM_ETH_v6_1"
    };
    int fd = open("/dev/ipa", O_RDWR);
    size_t i;

    if (fd < 0) {
        perror("open /dev/ipa");
        return 1;
    }

    for (i = 0; i < sizeof(names)/sizeof(names[0]); i++) {
        uint32_t hdl = 0;
        if (get_hdr(fd, names[i], &hdl) == 0) {
            printf("GET_HDR name=%s hdl=0x%x\n", names[i], hdl);
            del_hdr(fd, hdl);
        } else {
            printf("GET_HDR name=%s missing errno=%d(%s)\n", names[i], errno, strerror(errno));
        }
    }

    close(fd);
    return 0;
}

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/msm_ipa.h>

static int try_get(int fd, const char *name)
{
    struct ipa_ioc_get_hdr g;
    int r;
    memset(&g, 0, sizeof(g));
    strncpy(g.name, name, IPA_RESOURCE_NAME_MAX - 1);
    errno = 0;
    r = ioctl(fd, IPA_IOC_GET_HDR, &g);
    printf("GET_HDR name='%s' ret=%d errno=%d(%s) hdl=0x%x\n", name, r, errno, strerror(errno), g.hdl);
    if (r == 0) {
        int pr = ioctl(fd, IPA_IOC_PUT_HDR, g.hdl);
        printf("  PUT_HDR hdl=0x%x ret=%d errno=%d(%s)\n", g.hdl, pr, errno, strerror(errno));
    }
    return r;
}

int main(void)
{
    int fd = open("/dev/ipa", O_RDWR);
    if (fd < 0) { perror("open /dev/ipa"); return 1; }

    try_get(fd, "1");
    try_get(fd, "35_IPACM_ETH_v4_0");
    try_get(fd, "35_IPACM_ETH_v6_0");
    try_get(fd, "35_IPACM_ODU_v4");
    try_get(fd, "35_IPACM_ODU_v6");
    try_get(fd, "rmnet_data0");
    try_get(fd, "eth0");

    close(fd);
    return 0;
}

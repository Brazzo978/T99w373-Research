#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/msm_ipa.h>

static void run_scan_copy(int fd, const char *name)
{
    struct ipa_ioc_copy_hdr c;
    unsigned int size;
    int ret;
    unsigned long cmd;

    printf("COPY_SCAN name=%s\n", name);
    for (size = 0; size <= 192; size++) {
        memset(&c, 0, sizeof(c));
        strncpy(c.name, name, IPA_RESOURCE_NAME_MAX - 1);
        cmd = _IOC(_IOC_READ | _IOC_WRITE, IPA_IOC_MAGIC, IPA_IOCTL_COPY_HDR, size);
        errno = 0;
        ret = ioctl(fd, cmd, &c);
        if (ret == 0 || errno != ENOTTY) {
            printf("COPY size=%3u cmd=0x%08lx ret=%d errno=%d(%s) hdr_len=%u part=%u type=%u\n",
                   size, cmd, ret, errno, strerror(errno), c.hdr_len, c.is_partial, c.type);
        }
    }
}

static void run_scan_get(int fd, const char *name)
{
    struct ipa_ioc_get_hdr g;
    unsigned int size;
    int ret;
    unsigned long cmd;

    printf("GET_SCAN name=%s\n", name);
    for (size = 0; size <= 64; size++) {
        memset(&g, 0, sizeof(g));
        strncpy(g.name, name, IPA_RESOURCE_NAME_MAX - 1);
        cmd = _IOC(_IOC_READ | _IOC_WRITE, IPA_IOC_MAGIC, IPA_IOCTL_GET_HDR, size);
        errno = 0;
        ret = ioctl(fd, cmd, &g);
        if (ret == 0 || errno != ENOTTY) {
            printf("GET  size=%3u cmd=0x%08lx ret=%d errno=%d(%s) hdl=0x%x\n",
                   size, cmd, ret, errno, strerror(errno), g.hdl);
            if (ret == 0) {
                int pr = ioctl(fd, IPA_IOC_PUT_HDR, g.hdl);
                printf("     PUT ret=%d errno=%d(%s)\n", pr, errno, strerror(errno));
            }
        }
    }
}

int main(void)
{
    int fd = open("/dev/ipa", O_RDWR);
    if (fd < 0) {
        perror("open /dev/ipa");
        return 1;
    }

    run_scan_get(fd, "35_IPACM_ODU_v4");
    run_scan_copy(fd, "35_IPACM_ODU_v4");

    close(fd);
    return 0;
}

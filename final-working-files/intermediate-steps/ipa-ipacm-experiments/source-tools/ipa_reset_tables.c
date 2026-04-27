#define _GNU_SOURCE
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/msm_ipa.h>

static void do_ioc0(int fd, unsigned long req, const char *name)
{
    errno = 0;
    int r = ioctl(fd, req);
    printf("%s ret=%d errno=%d(%s)\n", name, r, errno, strerror(errno));
}

static void do_ioc_ip(int fd, unsigned long req, enum ipa_ip_type ip, const char *name)
{
    errno = 0;
    int r = ioctl(fd, req, ip);
    printf("%s ip=%d ret=%d errno=%d(%s)\n", name, ip, r, errno, strerror(errno));
}

int main(void)
{
    int fd = open("/dev/ipa", O_RDWR);
    if (fd < 0) { perror("open /dev/ipa"); return 1; }

    do_ioc0(fd, IPA_IOC_RESET_HDR, "RESET_HDR");
    do_ioc_ip(fd, IPA_IOC_RESET_RT, IPA_IP_v4, "RESET_RT");
    do_ioc_ip(fd, IPA_IOC_RESET_RT, IPA_IP_v6, "RESET_RT");
    do_ioc_ip(fd, IPA_IOC_RESET_FLT, IPA_IP_v4, "RESET_FLT");
    do_ioc_ip(fd, IPA_IOC_RESET_FLT, IPA_IP_v6, "RESET_FLT");

    close(fd);
    return 0;
}

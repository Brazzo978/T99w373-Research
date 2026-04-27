#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/msm_ipa.h>

static int do_one(int fd, const char *name)
{
    struct ipa_ioc_copy_hdr c;
    int ret;
    memset(&c, 0, sizeof(c));
    if (name)
        strncpy(c.name, name, IPA_RESOURCE_NAME_MAX - 1);
    errno = 0;
    ret = ioctl(fd, IPA_IOC_COPY_HDR, &c);
    printf("name='%s' ret=%d errno=%d(%s) hdr_len=%u type=%u partial=%u eth2_valid=%u eth2_ofst=%u hdr12=%02x hdr13=%02x\n",
        name ? name : "", ret, errno, strerror(errno), c.hdr_len, (unsigned)c.type,
        c.is_partial, c.is_eth2_ofst_valid, c.eth2_ofst, c.hdr[12], c.hdr[13]);
    return ret;
}

int main(int argc, char **argv)
{
    int fd, i;
    fd = open("/dev/ipa", O_RDWR);
    if (fd < 0) {
        perror("open /dev/ipa");
        return 1;
    }
    if (argc <= 1) {
        do_one(fd, "eth0_ipv4");
        do_one(fd, "eth0_ipv6");
        do_one(fd, "35_IPACM_ODU_v4");
        do_one(fd, "35_IPACM_ODU_v6");
        do_one(fd, "35_IPACM_ETH_v4_0");
        do_one(fd, "35_IPACM_ETH_v6_0");
    } else {
        for (i = 1; i < argc; i++)
            do_one(fd, argv[i]);
    }
    close(fd);
    return 0;
}

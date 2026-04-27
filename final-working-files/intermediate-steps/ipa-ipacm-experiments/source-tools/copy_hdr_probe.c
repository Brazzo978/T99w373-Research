#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/msm_ipa.h>

static void try_name(int fd, const char *name)
{
    struct ipa_ioc_copy_hdr c;
    int ret;

    memset(&c, 0, sizeof(c));
    if (name)
        strncpy(c.name, name, IPA_RESOURCE_NAME_MAX - 1);

    errno = 0;
    ret = ioctl(fd, IPA_IOC_COPY_HDR, &c);
    printf("name='%s' ret=%d errno=%d(%s) hdr_len=%u type=%u partial=%u eth2_valid=%u eth2_ofst=%u first8=%02x%02x%02x%02x%02x%02x%02x%02x\n",
           c.name, ret, errno, strerror(errno), c.hdr_len, (unsigned)c.type, c.is_partial,
           c.is_eth2_ofst_valid, c.eth2_ofst,
           c.hdr[0], c.hdr[1], c.hdr[2], c.hdr[3],
           c.hdr[4], c.hdr[5], c.hdr[6], c.hdr[7]);
}

int main(void)
{
    int fd = open("/dev/ipa", O_RDWR);
    if (fd < 0) {
        perror("open /dev/ipa");
        return 1;
    }

    try_name(fd, "");
    try_name(fd, "1");
    try_name(fd, "2");
    try_name(fd, "rmnet_data0");
    try_name(fd, "eth0");
    try_name(fd, "35");

    close(fd);
    return 0;
}

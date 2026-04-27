#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/msm_ipa.h>

int main(void)
{
    struct ipa_ioc_copy_hdr c;
    int fd, ret;

    memset(&c, 0, sizeof(c));
    strncpy(c.name, "1", IPA_RESOURCE_NAME_MAX - 1);

    fd = open("/dev/ipa", O_RDWR);
    if (fd < 0) {
        perror("open /dev/ipa");
        return 1;
    }

    errno = 0;
    ret = ioctl(fd, IPA_IOC_COPY_HDR, &c);

    printf("sizeof(copy_hdr)=%zu IPA_HDR_MAX_SIZE=%d ret=%d errno=%d(%s) hdr_len=%u partial=%u type=%u eth2_valid=%u eth2_ofst=%u\n",
           sizeof(c), IPA_HDR_MAX_SIZE, ret, errno, strerror(errno),
           c.hdr_len, c.is_partial, (unsigned)c.type,
           c.is_eth2_ofst_valid, c.eth2_ofst);

    close(fd);
    return 0;
}

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/msm_ipa.h>

#define MAX_TX 8

struct req_buf {
    struct ipa_ioc_query_intf_tx_props q;
    struct ipa_ioc_tx_intf_prop tx[MAX_TX];
};

static void run_one(int fd, const char *ifname, unsigned int size)
{
    struct req_buf r;
    unsigned long cmd;
    int ret;

    memset(&r, 0, sizeof(r));
    strncpy(r.q.name, ifname, IPA_RESOURCE_NAME_MAX - 1);
    r.q.num_tx_props = MAX_TX;
    cmd = _IOC(_IOC_READ | _IOC_WRITE, IPA_IOC_MAGIC, IPA_IOCTL_QUERY_INTF_TX_PROPS, size);

    errno = 0;
    ret = ioctl(fd, cmd, &r.q);
    if (ret == 0 || errno != ENOTTY) {
        printf("if=%-12s size=%3u cmd=0x%08lx ret=%d errno=%d(%s) n=%u ",
               ifname, size, cmd, ret, errno, strerror(errno), r.q.num_tx_props);
        if (r.q.num_tx_props > 0 && r.q.num_tx_props <= MAX_TX) {
            printf("tx0{ip=%u dst=%u alt=%u l2=%u hdr='%s'}",
                (unsigned)r.tx[0].ip,
                (unsigned)r.tx[0].dst_pipe,
                (unsigned)r.tx[0].alt_dst_pipe,
                (unsigned)r.tx[0].hdr_l2_type,
                r.tx[0].hdr_name);
        }
        printf("\n");
    }
}

int main(void)
{
    int fd;
    unsigned int s;

    fd = open("/dev/ipa", O_RDWR);
    if (fd < 0) {
        perror("open /dev/ipa");
        return 1;
    }

    for (s = 0; s <= 128; s++) {
        run_one(fd, "eth0", s);
    }
    for (s = 0; s <= 128; s++) {
        run_one(fd, "rmnet_data0", s);
    }

    close(fd);
    return 0;
}

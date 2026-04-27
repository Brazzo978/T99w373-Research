#include <errno.h>
#include <fcntl.h>
#include <linux/msm_ipa.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void hex_dump(const unsigned char *p, size_t len){
  size_t i;
  for(i=0;i<len;i++){
    if(i%16==0) printf("\n  %04zu:", i);
    printf(" %02x", p[i]);
  }
  printf("\n");
}
static void ascii_marks(const unsigned char *p, size_t len){
  size_t i;
  printf("  ascii:");
  for(i=0;i<len;i++) putchar((p[i]>=32 && p[i]<=126)?p[i]:'.');
  putchar('\n');
}
static void dump_one(const char *ifname){
 int fd=open("/dev/ipa",O_RDWR); struct ipa_ioc_query_intf qi; int ret; size_t sz; unsigned char *buf;
 if(fd<0){perror("open");return;}
 memset(&qi,0,sizeof(qi)); snprintf(qi.name,sizeof(qi.name),"%s",ifname);
 errno=0; ret=ioctl(fd,IPA_IOC_QUERY_INTF,&qi);
 printf("IF %s ret=%d errno=%d tx=%u rx=%u ext=%u excp=%u\n",ifname,ret,errno,qi.num_tx_props,qi.num_rx_props,qi.num_ext_props,(unsigned)qi.excp_pipe);
 sz=36+512; buf=calloc(1,sz); snprintf((char*)buf,32,"%s",ifname); *(uint32_t*)(buf+32)=qi.num_tx_props;
 errno=0; ret=ioctl(fd,IPA_IOC_QUERY_INTF_TX_PROPS,buf);
 printf(" TX ret=%d errno=%d n=%u raw:",ret,errno,*(uint32_t*)(buf+32)); hex_dump(buf, sz<320?sz:320); ascii_marks(buf, sz<320?sz:320); free(buf);
 sz=36+512; buf=calloc(1,sz); snprintf((char*)buf,32,"%s",ifname); *(uint32_t*)(buf+32)=qi.num_rx_props;
 errno=0; ret=ioctl(fd,IPA_IOC_QUERY_INTF_RX_PROPS,buf);
 printf(" RX ret=%d errno=%d n=%u raw:",ret,errno,*(uint32_t*)(buf+32)); hex_dump(buf, sz<260?sz:260); ascii_marks(buf, sz<260?sz:260); free(buf);
 close(fd);
}
int main(){dump_one("eth0"); dump_one("rmnet_data0"); return 0;}

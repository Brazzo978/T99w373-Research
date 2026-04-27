#include <stdio.h>
#include <linux/msm_ipa.h>
int main(void){
  printf("IPA_RULE_ATTRIB=%zu\n", sizeof(struct ipa_rule_attrib));
  printf("IPA_FLT_RULE=%zu\n", sizeof(struct ipa_flt_rule));
  printf("IPA_FLT_RULE_ADD=%zu\n", sizeof(struct ipa_flt_rule_add));
  printf("IPA_FLT_RULE_ADD_V2=%zu\n", sizeof(struct ipa_flt_rule_add_v2));
  printf("IPA_IOC_ADD_FLT_RULE=%zu\n", sizeof(struct ipa_ioc_add_flt_rule));
  printf("IPA_IOC_ADD_FLT_RULE_V2=%zu\n", sizeof(struct ipa_ioc_add_flt_rule_v2));
  printf("IPA_RT_RULE=%zu\n", sizeof(struct ipa_rt_rule));
  printf("IPA_RT_RULE_ADD=%zu\n", sizeof(struct ipa_rt_rule_add));
  printf("IPA_HDR_ADD=%zu\n", sizeof(struct ipa_hdr_add));
  printf("IPA_IOC_COPY_HDR=%zu\n", sizeof(struct ipa_ioc_copy_hdr));
  return 0;
}

#include <stdio.h>
#include <linux/msm_ipa.h>
int main(void){
  printf("FLT_RULE_V2=%zu\n", sizeof(struct ipa_flt_rule_v2));
  printf("FLT_RULE_ADD_V2=%zu\n", sizeof(struct ipa_flt_rule_add_v2));
  printf("RT_RULE_V2=%zu\n", sizeof(struct ipa_rt_rule_v2));
  printf("RT_RULE_ADD_V2=%zu\n", sizeof(struct ipa_rt_rule_add_v2));
  return 0;
}

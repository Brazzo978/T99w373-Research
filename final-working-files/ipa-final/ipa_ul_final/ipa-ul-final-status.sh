#!/bin/sh
echo SERVICES
systemctl is-active pcie-enabler.service ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service ipa-iptables-dedupe.timer ipa-stack-healthcheck.timer qcmap_httpd.service 2>/dev/null || true
echo IPACM
systemctl show ipacm.service -p ActiveState -p SubState -p MainPID -p NRestarts --no-pager 2>/dev/null || true
echo WATCHDOG
systemctl show ipa-stack-healthcheck.timer -p ActiveState -p SubState -p NextElapseUSecRealtime --no-pager 2>/dev/null || true
tail -n 20 /tmp/ipa-stack-healthcheck.log 2>/dev/null || true
echo ENV
for p in $(pidof ipacm 2>/dev/null); do echo PID=$p; tr '\0' '\n' </proc/$p/environ 2>/dev/null | grep -E 'LD_PRELOAD|IPA_ABI_BRIDGE' || true; done
echo NAT
iptables -t nat -S POSTROUTING | sort | uniq -c 2>/dev/null || true
echo IPA
cat /sys/kernel/debug/ipa/stats 2>/dev/null | grep -E 'sw_tx=|hw_tx=|tx_compl=|lan_rx_excp\[(6|7|8|10|11|12)' || true
echo IPA_ETH_RTK
cat /sys/kernel/debug/ipa/eth/RTK8125B_0_stats 2>/dev/null | grep -E 'RingUtilCount|ringUsageHigh|ringEmpty|erCount|erCound|trCount' || true
echo ROUTE
ip route

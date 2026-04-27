#!/bin/sh
set -u
mkdir -p /root/ipa_stabilize_backups
iptables-save > /root/ipa_stabilize_backups/iptables_dedupe_before_$(date +%Y%m%d_%H%M%S).save 2>/dev/null || true
for iface in rmnet_data0 rmnet_data1; do
  count=$(iptables -t nat -S POSTROUTING | grep -F -- "-A POSTROUTING -o $iface -j MASQUERADE --random" | wc -l)
  while [ "$count" -gt 1 ]; do
    iptables -t nat -D POSTROUTING -o "$iface" -j MASQUERADE --random || break
    count=$((count-1))
  done
done
for iface in rmnet_data0 rmnet_data1; do
  for port in 80 443; do
    count=$(iptables -S INPUT | grep -F -- "-A INPUT -i $iface -p tcp -m tcp --dport $port -j DROP" | wc -l)
    while [ "$count" -gt 1 ]; do
      iptables -D INPUT -i "$iface" -p tcp -m tcp --dport "$port" -j DROP || break
      count=$((count-1))
    done
  done
done
iptables -t nat -S POSTROUTING | sort | uniq -c

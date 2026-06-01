#!/bin/sh
set -u
BACKUP_DIR="${IPA_DEDUPE_BACKUP_DIR:-/tmp/ipa_stabilize_backups}"
mkdir -p "$BACKUP_DIR"
iptables-save > "$BACKUP_DIR/iptables_dedupe_before_$(date +%Y%m%d_%H%M%S).save" 2>/dev/null || true
ls -1t "$BACKUP_DIR"/iptables_dedupe_before_*.save 2>/dev/null | sed -n '5,$p' | xargs rm -f 2>/dev/null || true
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

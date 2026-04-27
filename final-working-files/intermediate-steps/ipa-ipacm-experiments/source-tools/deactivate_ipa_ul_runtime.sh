#!/usr/bin/env bash
set -euo pipefail
WAIT_AFTER_RESTART=${WAIT_AFTER_RESTART:-45}
log(){ printf '%s %s\n' "$(date '+%F %T')" "$*"; }
log "remove runtime ABI bridge drop-in"
adb shell 'rm -f /run/systemd/system/ipacm.service.d/31-abi-bridge-v5-runtime.conf /run/systemd/system/ipacm.service.d/31-abi-bridge-v6-runtime.conf; systemctl daemon-reload; systemctl reset-failed ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service; systemctl restart ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service'
log "wait ${WAIT_AFTER_RESTART}s"
sleep "$WAIT_AFTER_RESTART"
log "dedupe iptables after deactivation"
adb shell 'for iface in rmnet_data0 rmnet_data1; do count=$(iptables -t nat -S POSTROUTING | grep -F -- "-A POSTROUTING -o $iface -j MASQUERADE --random" | wc -l); while [ "$count" -gt 1 ]; do iptables -t nat -D POSTROUTING -o "$iface" -j MASQUERADE --random || break; count=$((count-1)); done; done; for iface in rmnet_data0 rmnet_data1; do for port in 80 443; do count=$(iptables -S INPUT | grep -F -- "-A INPUT -i $iface -p tcp -m tcp --dport $port -j DROP" | wc -l); while [ "$count" -gt 1 ]; do iptables -D INPUT -i "$iface" -p tcp -m tcp --dport "$port" -j DROP || break; count=$((count-1)); done; done; done; systemctl is-active pcie-enabler.service ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service || true; for p in $(pidof ipacm 2>/dev/null); do echo PID=$p; tr "\0" "\n" </proc/$p/environ 2>/dev/null | grep -E "LD_PRELOAD|IPA_ABI_BRIDGE" || true; done; ip route'

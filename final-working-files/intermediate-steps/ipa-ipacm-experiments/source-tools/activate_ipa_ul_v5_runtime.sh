#!/usr/bin/env bash
set -euo pipefail
BASE=${BASE:-"/home/manu/Scrivania/linaro 5.4/load_ipa_ES"}
LIB="$BASE/libipacm_abi_bridge_v5.so"
WAIT_AFTER_QCMAP=${WAIT_AFTER_QCMAP:-75}
if [ ! -f "$LIB" ]; then echo "Missing $LIB"; exit 1; fi
log(){ printf '%s %s\n' "$(date '+%F %T')" "$*"; }
log "push v5 library"
adb push "$LIB" /tmp/libipacm_abi_bridge_v5.so >/dev/null
log "install runtime drop-in and restart ipacm/qcmap stack"
adb shell 'chmod 755 /tmp/libipacm_abi_bridge_v5.so; rm -f /tmp/ipacm_abi_bridge_v5.log /tmp/ipacm_v5_runtime.out; mkdir -p /run/systemd/system/ipacm.service.d; cat > /run/systemd/system/ipacm.service.d/31-abi-bridge-v5-runtime.conf <<'"'"'EOS'"'"'
[Service]
Environment=IPA_ABI_BRIDGE_MODE=translate
Environment=IPA_ABI_BRIDGE_ALLOW_IFACES=eth0,rmnet_data0,rmnet_data1
Environment=IPA_ABI_BRIDGE_TRANSLATE_IFACES=eth0,rmnet_data0,rmnet_data1
Environment=IPA_ABI_BRIDGE_TX=1
Environment=IPA_ABI_BRIDGE_RX=1
Environment=IPA_ABI_BRIDGE_EXT=1
Environment=IPA_ABI_BRIDGE_RT_FLT_LOG=1
Environment=IPA_ABI_BRIDGE_RT_FLT_TRANSLATE=1
Environment=IPA_ABI_BRIDGE_FAIL_CLOSED=1
Environment=IPA_ABI_BRIDGE_HEXDUMP_ONCE=1
Environment=IPA_ABI_BRIDGE_LOG=/tmp/ipacm_abi_bridge_v5.log
Environment=LD_PRELOAD=/tmp/libipacm_abi_bridge_v5.so
StandardOutput=append:/tmp/ipacm_v5_runtime.out
StandardError=append:/tmp/ipacm_v5_runtime.out
EOS
systemctl daemon-reload
systemctl restart ipacm.service
sleep 8
systemctl restart QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service
'
log "wait qcmap ${WAIT_AFTER_QCMAP}s"
sleep "$WAIT_AFTER_QCMAP"
log "dedupe iptables"
adb shell 'mkdir -p /root/ipa_stabilize_backups; iptables-save > /root/ipa_stabilize_backups/iptables_activate_v5_before_dedupe_$(date +%Y%m%d_%H%M%S).save 2>/dev/null || true; for iface in rmnet_data0 rmnet_data1; do count=$(iptables -t nat -S POSTROUTING | grep -F -- "-A POSTROUTING -o $iface -j MASQUERADE --random" | wc -l); while [ "$count" -gt 1 ]; do iptables -t nat -D POSTROUTING -o "$iface" -j MASQUERADE --random || break; count=$((count-1)); done; done; for iface in rmnet_data0 rmnet_data1; do for port in 80 443; do count=$(iptables -S INPUT | grep -F -- "-A INPUT -i $iface -p tcp -m tcp --dport $port -j DROP" | wc -l); while [ "$count" -gt 1 ]; do iptables -D INPUT -i "$iface" -p tcp -m tcp --dport "$port" -j DROP || break; count=$((count-1)); done; done; done'
log "status"
adb shell 'echo SERVICES; systemctl is-active pcie-enabler.service ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service || true; echo IPACM; systemctl show ipacm.service -p ActiveState -p SubState -p MainPID -p NRestarts --no-pager; echo ENV; for p in $(pidof ipacm 2>/dev/null); do echo PID=$p; tr "\0" "\n" </proc/$p/environ 2>/dev/null | grep -E "LD_PRELOAD|IPA_ABI_BRIDGE" || true; done; echo NAT; iptables -t nat -S POSTROUTING | sort | uniq -c; echo ROUTE; ip route'

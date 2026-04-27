#!/usr/bin/env bash
set -euo pipefail
BASE=${BASE:-"/home/manu/Scrivania/linaro 5.4/load_ipa_ES"}
STAGE="$BASE/final_ul_stage"
TS=$(date +%Y%m%d_%H%M%S)
log(){ printf '%s %s\n' "$(date '+%F %T')" "$*"; }
for f in libipacm_abi_bridge_final.so 31-abi-bridge-final.conf ipa-iptables-dedupe.sh ipa-iptables-dedupe.service ipa-iptables-dedupe.timer ipa-ul-final-status.sh; do
  [ -f "$STAGE/$f" ] || { echo "Missing $STAGE/$f"; exit 1; }
done
log "push staged final files"
adb push "$STAGE/libipacm_abi_bridge_final.so" /tmp/libipacm_abi_bridge_final.so >/dev/null
adb push "$STAGE/31-abi-bridge-final.conf" /tmp/31-abi-bridge-final.conf >/dev/null
adb push "$STAGE/ipa-iptables-dedupe.sh" /tmp/ipa-iptables-dedupe.sh >/dev/null
adb push "$STAGE/ipa-iptables-dedupe.service" /tmp/ipa-iptables-dedupe.service >/dev/null
adb push "$STAGE/ipa-iptables-dedupe.timer" /tmp/ipa-iptables-dedupe.timer >/dev/null
adb push "$STAGE/ipa-ul-final-status.sh" /tmp/ipa-ul-final-status.sh >/dev/null
log "install persistent files"
adb shell "set -e; mkdir -p /root/ipa_ul_final_backups/$TS /etc/systemd/system/ipacm.service.d; cp -a /etc/systemd/system/ipacm.service.d /root/ipa_ul_final_backups/$TS/ipacm.service.d 2>/dev/null || true; cp -a /usr/lib/libipacm_abi_bridge_final.so /root/ipa_ul_final_backups/$TS/libipacm_abi_bridge_final.so.bak 2>/dev/null || true; cp -a /usr/bin/ipa-iptables-dedupe.sh /root/ipa_ul_final_backups/$TS/ipa-iptables-dedupe.sh.bak 2>/dev/null || true; cp -a /etc/systemd/system/ipa-iptables-dedupe.service /root/ipa_ul_final_backups/$TS/ipa-iptables-dedupe.service.bak 2>/dev/null || true; cp -a /etc/systemd/system/ipa-iptables-dedupe.timer /root/ipa_ul_final_backups/$TS/ipa-iptables-dedupe.timer.bak 2>/dev/null || true; cp /tmp/libipacm_abi_bridge_final.so /usr/lib/libipacm_abi_bridge_final.so; chmod 755 /usr/lib/libipacm_abi_bridge_final.so; cp /tmp/31-abi-bridge-final.conf /etc/systemd/system/ipacm.service.d/31-abi-bridge-final.conf; cp /tmp/ipa-iptables-dedupe.sh /usr/bin/ipa-iptables-dedupe.sh; chmod 755 /usr/bin/ipa-iptables-dedupe.sh; cp /tmp/ipa-iptables-dedupe.service /etc/systemd/system/ipa-iptables-dedupe.service; cp /tmp/ipa-iptables-dedupe.timer /etc/systemd/system/ipa-iptables-dedupe.timer; cp /tmp/ipa-ul-final-status.sh /usr/bin/ipa-ul-final-status.sh; chmod 755 /usr/bin/ipa-ul-final-status.sh; systemctl daemon-reload; systemctl enable ipa-iptables-dedupe.timer >/dev/null; systemctl start ipa-iptables-dedupe.timer; /usr/bin/ipa-iptables-dedupe.sh >/tmp/ipa_iptables_dedupe_install.log 2>&1 || true; echo BACKUP=/root/ipa_ul_final_backups/$TS"
log "installed final files; current process may still use runtime /tmp lib until ipacm restart/reboot"
adb shell 'ls -l /usr/lib/libipacm_abi_bridge_final.so /etc/systemd/system/ipacm.service.d/31-abi-bridge-final.conf /usr/bin/ipa-iptables-dedupe.sh /etc/systemd/system/ipa-iptables-dedupe.service /etc/systemd/system/ipa-iptables-dedupe.timer /usr/bin/ipa-ul-final-status.sh; systemctl is-enabled ipa-iptables-dedupe.timer; systemctl is-active ipa-iptables-dedupe.timer; /usr/bin/ipa-ul-final-status.sh'

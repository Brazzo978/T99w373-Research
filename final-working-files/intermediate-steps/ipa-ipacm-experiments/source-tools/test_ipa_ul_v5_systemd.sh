#!/usr/bin/env bash
set -u
BASE=${BASE:-"/home/manu/Scrivania/linaro 5.4/load_ipa_ES"}
IFACE=${IFACE:-enx00e04c6802a5}
GW=${GW:-192.168.225.1}
UPLOAD_MB=${UPLOAD_MB:-10}
WAIT_AFTER_QCMAP=${WAIT_AFTER_QCMAP:-45}
CONNECT_TRIES=${CONNECT_TRIES:-12}
CONNECT_SLEEP=${CONNECT_SLEEP:-5}
DO_HOTPLUG=${DO_HOTPLUG:-0}
AUTO_HOTPLUG=${AUTO_HOTPLUG:-0}
RUN_TS=$(date +%Y-%m-%d_%H-%M-%S)
OUT="$BASE/ul_v5_systemd_$RUN_TS"
mkdir -p "$OUT"
echo "$OUT" > "$BASE/latest_ul_v5_systemd_path.txt"
LIB="$BASE/libipacm_abi_bridge_v5.so"
if [ ! -f "$LIB" ]; then echo "Missing $LIB"; exit 1; fi
log(){ printf '%s %s\n' "$(date '+%F %T')" "$*" | tee -a "$OUT/run.log"; }
adb_pull_tmp(){ adb pull "$1" "$2" >/dev/null 2>&1 || true; }
collect_modem(){
  local tag="$1"
  adb shell "mkdir -p /tmp/ul_v5_systemd_collect; \
    cat /sys/kernel/debug/ipa/stats > /tmp/ul_v5_systemd_collect/stats_$tag.txt 2>/dev/null || true; \
    cat /sys/kernel/debug/ipa/msg > /tmp/ul_v5_systemd_collect/msg_$tag.txt 2>/dev/null || true; \
    systemctl is-active pcie-enabler.service ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service > /tmp/ul_v5_systemd_collect/services_$tag.txt 2>&1 || true; \
    systemctl show ipacm.service -p ActiveState -p SubState -p MainPID -p NRestarts -p Restart -p ExecMainStatus -p DropInPaths -p Environment > /tmp/ul_v5_systemd_collect/ipacm_show_$tag.txt 2>&1 || true; \
    ip route > /tmp/ul_v5_systemd_collect/route_$tag.txt 2>&1; \
    pidof ipacm > /tmp/ul_v5_systemd_collect/pid_$tag.txt 2>&1 || true; \
    ps | grep -E '[i]pacm|abi_bridge' > /tmp/ul_v5_systemd_collect/ps_$tag.txt 2>&1 || true; \
    for p in \$(pidof ipacm 2>/dev/null); do echo PID=\$p; tr '\0' '\n' </proc/\$p/environ 2>/dev/null | grep -E 'LD_PRELOAD|IPA_ABI_BRIDGE' || true; done > /tmp/ul_v5_systemd_collect/env_$tag.txt 2>&1; \
    wc -c /tmp/ipacm_abi_bridge_v5.log /tmp/ipacm_v5_systemd.out > /tmp/ul_v5_systemd_collect/logsize_$tag.txt 2>/dev/null || true; \
    tail -260 /tmp/ipacm_abi_bridge_v5.log > /tmp/ul_v5_systemd_collect/abi_tail_$tag.txt 2>/dev/null || true; \
    tail -320 /tmp/ipacm_v5_systemd.out > /tmp/ul_v5_systemd_collect/ipacm_tail_$tag.txt 2>/dev/null || true; \
    journalctl -b -u ipacm.service --no-pager | tail -220 > /tmp/ul_v5_systemd_collect/journal_ipacm_$tag.txt 2>&1 || true; \
    dmesg | tail -140 > /tmp/ul_v5_systemd_collect/dmesg_$tag.txt 2>/dev/null || true" >/dev/null 2>&1 || true
  for f in stats msg services ipacm_show route pid ps env logsize abi_tail ipacm_tail journal_ipacm dmesg; do adb_pull_tmp "/tmp/ul_v5_systemd_collect/${f}_${tag}.txt" "$OUT/${f}_${tag}.txt"; done
}
rollback(){
  log "rollback start"
  adb shell 'rm -rf /run/systemd/system/ipacm.service.d/31-abi-bridge-v5-runtime.conf; systemctl daemon-reload; systemctl reset-failed ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service; systemctl restart ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service; sleep 45; systemctl is-active pcie-enabler.service ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service; ip route' > "$OUT/rollback.txt" 2>&1 || true
  collect_modem rollback
  log "rollback done"
}
trap 'rollback' EXIT
log "preflight baseline"
collect_modem baseline
log "push v5 to /tmp and install runtime systemd drop-in"
adb push "$LIB" /tmp/libipacm_abi_bridge_v5.so >/dev/null || exit 1
adb shell 'chmod 755 /tmp/libipacm_abi_bridge_v5.so; rm -f /tmp/ipacm_abi_bridge_v5.log /tmp/ipacm_v5_systemd.out; mkdir -p /run/systemd/system/ipacm.service.d; cat > /run/systemd/system/ipacm.service.d/31-abi-bridge-v5-runtime.conf <<'"'"'EOS'"'"'
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
StandardOutput=append:/tmp/ipacm_v5_systemd.out
StandardError=append:/tmp/ipacm_v5_systemd.out
EOS
systemctl daemon-reload
systemctl restart ipacm.service
sleep 8
systemctl restart QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service
' || exit 1
log "wait qcmap $WAIT_AFTER_QCMAP s"
sleep "$WAIT_AFTER_QCMAP"
collect_modem after_activate
log "wait connectivity"
OK=0
: > "$OUT/connectivity_wait.txt"
for i in $(seq 1 "$CONNECT_TRIES"); do
  echo "TRY $i" >> "$OUT/connectivity_wait.txt"
  ping -I "$IFACE" -c 1 -W 2 "$GW" >> "$OUT/connectivity_wait.txt" 2>&1 || true
  curl -4 --interface "$IFACE" --max-time 15 -L -w 'exit=%{exitcode} http=%{http_code} down=%{size_download} time=%{time_total}\n' 'https://speed.cloudflare.com/__down?bytes=1024' -o /tmp/ul_v5_systemd_wait_dl.bin >> "$OUT/connectivity_wait.txt" 2>&1 || true
  if tail -n 1 "$OUT/connectivity_wait.txt" | grep -q 'http=200'; then OK=1; break; fi
  sleep "$CONNECT_SLEEP"
done
if [ "$OK" != 1 ]; then log "connectivity failed under v5 systemd"; collect_modem connectivity_failed; exit 2; fi
log "connectivity ok"
collect_modem before_upload
UPLOAD_FILE="/tmp/ul_v5_systemd_upload_${UPLOAD_MB}m.bin"
dd if=/dev/urandom of="$UPLOAD_FILE" bs=1M count="$UPLOAD_MB" status=none
curl -4 --interface "$IFACE" --max-time 300 -L -X POST --data-binary @"$UPLOAD_FILE" -w '\nexit=%{exitcode}\nhttp=%{http_code}\nup=%{size_upload}\ndown=%{size_download}\ntime=%{time_total}\n' 'https://speed.cloudflare.com/__up' -o /tmp/ul_v5_systemd_upload_resp.bin > "$OUT/upload_${UPLOAD_MB}m.txt" 2>&1 || true
collect_modem after_upload
if [ "$DO_HOTPLUG" = 1 ]; then
  if [ "$AUTO_HOTPLUG" = 1 ]; then
    log "auto hotplug: eth0 down/up on modem"
    adb shell 'ip link set eth0 down; sleep 8; ip link set eth0 up; sleep 35' > "$OUT/auto_hotplug.txt" 2>&1 || true
  else
    log "hotplug wait: disconnect/reconnect eth0 now if desired, 45s window"
    sleep 45
  fi
  collect_modem after_hotplug_window
  : > "$OUT/connectivity_after_hotplug.txt"
  for i in $(seq 1 "$CONNECT_TRIES"); do
    echo "TRY $i" >> "$OUT/connectivity_after_hotplug.txt"
    ping -I "$IFACE" -c 1 -W 2 "$GW" >> "$OUT/connectivity_after_hotplug.txt" 2>&1 || true
    curl -4 --interface "$IFACE" --max-time 15 -L -w 'exit=%{exitcode} http=%{http_code} down=%{size_download} time=%{time_total}\n' 'https://speed.cloudflare.com/__down?bytes=1024' -o /tmp/ul_v5_systemd_hotplug_dl.bin >> "$OUT/connectivity_after_hotplug.txt" 2>&1 || true
    tail -n 1 "$OUT/connectivity_after_hotplug.txt" | grep -q 'http=200' && break
    sleep "$CONNECT_SLEEP"
  done
fi
python3 - "$OUT" <<'PY' | tee "$OUT/summary.txt"
from pathlib import Path
import re, sys
out=Path(sys.argv[1])
def read(name):
 p=out/name
 return p.read_text(errors='ignore') if p.exists() else ''
def parse_stats(tag):
 s=read(f'stats_{tag}.txt'); d={}
 for k in ['sw_tx','hw_tx','tx_compl','wan_rx','stat_compl','act_clnt']:
  m=re.search(r'\b'+k+r'=(\d+)',s); d[k]=int(m.group(1)) if m else None
 for name in ['NONE','SW_FILT','NAT','INVALID_PIPE','HDRI','CSUM']:
  m=re.search(r'lan_rx_excp\[\d+:IPAHAL_PKT_STATUS_EXCEPTION_'+name+r'\]=(\d+)',s); d['lan_'+name]=int(m.group(1)) if m else None
 return d
def parse_msg(tag):
 s=read(f'msg_{tag}.txt'); d={}
 for name in ['WAN_UPSTREAM_ROUTE_ADD','WAN_UPSTREAM_ROUTE_DEL','IPA_SET_MTU']:
  m=re.search(r'\b'+name+r'\]\s+W:(\d+)\s+R:(\d+)',s); d[name]=(int(m.group(1)), int(m.group(2))) if m else None
 return d
def service_line(tag):
 lines=[x.strip() for x in read(f'services_{tag}.txt').splitlines()]
 return '/'.join(lines) if lines else 'NA'
def show_val(tag, key):
 m=re.search(r'^'+re.escape(key)+r'=(.*)$', read(f'ipacm_show_{tag}.txt'), re.M)
 return m.group(1) if m else 'NA'
b=parse_stats('before_upload'); a=parse_stats('after_upload')
print('UPLOAD_COUNTER_DELTA')
for k in ['sw_tx','hw_tx','tx_compl','wan_rx','lan_NONE','lan_SW_FILT','lan_NAT','lan_INVALID_PIPE','lan_HDRI','lan_CSUM']:
 bv=b.get(k); av=a.get(k); print(f'{k},before={bv},after={av},delta={(av-bv) if bv is not None and av is not None else None}')
print('MSG_DELTA')
bm=parse_msg('before_upload'); am=parse_msg('after_upload')
for k in ['WAN_UPSTREAM_ROUTE_ADD','WAN_UPSTREAM_ROUTE_DEL','IPA_SET_MTU']:
 print(f'{k},before={bm.get(k)},after={am.get(k)}')
print('SERVICE_STATE')
for tag in ['after_activate','before_upload','after_upload','after_hotplug_window','rollback']:
 if (out/f'services_{tag}.txt').exists():
  print(f'{tag},services={service_line(tag)},ipacm_active={show_val(tag,"ActiveState")},sub={show_val(tag,"SubState")},pid={show_val(tag,"MainPID")},nrestarts={show_val(tag,"NRestarts")}')
PY
log "test complete"

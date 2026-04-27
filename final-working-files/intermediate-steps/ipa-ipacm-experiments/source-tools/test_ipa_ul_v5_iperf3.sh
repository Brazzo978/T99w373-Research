#!/usr/bin/env bash
set -u
BASE=${BASE:-"/home/manu/Scrivania/linaro 5.4/load_ipa_ES"}
IFACE=${IFACE:-enx00e04c6802a5}
SERVER=${SERVER:-185.242.181.206}
PORT=${PORT:-5201}
DURATION=${DURATION:-30}
PARALLEL=${PARALLEL:-1}
UDP=${UDP:-0}
BITRATE=${BITRATE:-20M}
BIND_MODE=${BIND_MODE:-dev}
WAIT_AFTER_QCMAP=${WAIT_AFTER_QCMAP:-75}
CONNECT_TRIES=${CONNECT_TRIES:-18}
CONNECT_SLEEP=${CONNECT_SLEEP:-5}
IPERF_PREFLIGHT_TRIES=${IPERF_PREFLIGHT_TRIES:-3}
RESET_REMOTE_IPERF=${RESET_REMOTE_IPERF:-0}
SERVER_SSH=${SERVER_SSH:-root@$SERVER}
SERVER_SSH_PORT=${SERVER_SSH_PORT:-22}
DEDUP_IPTABLES=${DEDUP_IPTABLES:-1}
RUN_TS=$(date +%Y-%m-%d_%H-%M-%S)
OUT="$BASE/ul_v5_iperf3_$RUN_TS"
mkdir -p "$OUT"
echo "$OUT" > "$BASE/latest_ul_v5_iperf3_path.txt"
LIB="$BASE/libipacm_abi_bridge_v5.so"
if [ ! -f "$LIB" ]; then echo "Missing $LIB"; exit 1; fi
if ! command -v iperf3 >/dev/null 2>&1; then echo "Missing iperf3 on host"; exit 1; fi
HOST_BIND=${HOST_BIND:-$(ip -4 -o addr show dev "$IFACE" | awk '{split($4,a,"/"); print a[1]; exit}')}
if [ -z "$HOST_BIND" ]; then echo "Cannot determine IPv4 for $IFACE"; exit 1; fi
if [ "$BIND_MODE" = "ip" ]; then
  BIND_ARGS=(-B "$HOST_BIND")
else
  BIND_ARGS=(--bind-dev "$IFACE")
fi
ACTIVATED=0
log(){ printf '%s %s\n' "$(date '+%F %T')" "$*" | tee -a "$OUT/run.log"; }
adb_pull_tmp(){ adb pull "$1" "$2" >/dev/null 2>&1 || true; }
reset_remote_iperf(){
  if [ "$RESET_REMOTE_IPERF" != 1 ]; then return 0; fi
  log "remote iperf reset start"
  if command -v sshpass >/dev/null 2>&1 && [ -n "${SSHPASS:-}" ]; then
    sshpass -e ssh -p "$SERVER_SSH_PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/ipa_iperf_known_hosts "$SERVER_SSH" 'systemctl restart iperf3-ipa.service; sleep 1; systemctl is-active iperf3-ipa.service; ss -ltnp | grep :5201 || true' > "$OUT/remote_iperf_reset_$(date +%H%M%S).txt" 2>&1 || true
  else
    ssh -p "$SERVER_SSH_PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/ipa_iperf_known_hosts "$SERVER_SSH" 'systemctl restart iperf3-ipa.service; sleep 1; systemctl is-active iperf3-ipa.service; ss -ltnp | grep :5201 || true' > "$OUT/remote_iperf_reset_$(date +%H%M%S).txt" 2>&1 || true
  fi
  log "remote iperf reset done"
}
dedup_modem_iptables(){
  local tag="$1"
  if [ "$DEDUP_IPTABLES" != 1 ]; then return 0; fi
  log "iptables dedupe start $tag"
  adb shell "mkdir -p /root/ipa_stabilize_backups; \
    iptables-save > /root/ipa_stabilize_backups/iptables_${tag}_before_dedupe_\$(date +%Y%m%d_%H%M%S).save 2>/dev/null || true; \
    for iface in rmnet_data0 rmnet_data1; do \
      count=\$(iptables -t nat -S POSTROUTING | grep -F -- \"-A POSTROUTING -o \$iface -j MASQUERADE --random\" | wc -l); \
      while [ \"\$count\" -gt 1 ]; do \
        iptables -t nat -D POSTROUTING -o \"\$iface\" -j MASQUERADE --random || break; \
        count=\$((count-1)); \
      done; \
    done; \
    for iface in rmnet_data0 rmnet_data1; do \
      for port in 80 443; do \
        count=\$(iptables -S INPUT | grep -F -- \"-A INPUT -i \$iface -p tcp -m tcp --dport \$port -j DROP\" | wc -l); \
        while [ \"\$count\" -gt 1 ]; do \
          iptables -D INPUT -i \"\$iface\" -p tcp -m tcp --dport \"\$port\" -j DROP || break; \
          count=\$((count-1)); \
        done; \
      done; \
    done; \
    echo NAT; iptables -t nat -S POSTROUTING | sort | uniq -c; \
    echo FILTER; iptables -S INPUT | sort | uniq -c | sort -nr | head -12" > "$OUT/iptables_dedupe_${tag}.txt" 2>&1 || true
  log "iptables dedupe done $tag"
}
collect_modem(){
  local tag="$1"
  adb shell "mkdir -p /tmp/ul_v5_iperf_collect; \
    cat /sys/kernel/debug/ipa/stats > /tmp/ul_v5_iperf_collect/stats_$tag.txt 2>/dev/null || true; \
    cat /sys/kernel/debug/ipa/msg > /tmp/ul_v5_iperf_collect/msg_$tag.txt 2>/dev/null || true; \
    systemctl is-active pcie-enabler.service ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service > /tmp/ul_v5_iperf_collect/services_$tag.txt 2>&1 || true; \
    systemctl show ipacm.service -p ActiveState -p SubState -p MainPID -p NRestarts -p Restart -p ExecMainStatus -p DropInPaths -p Environment > /tmp/ul_v5_iperf_collect/ipacm_show_$tag.txt 2>&1 || true; \
    ip route > /tmp/ul_v5_iperf_collect/route_$tag.txt 2>&1; \
    pidof ipacm > /tmp/ul_v5_iperf_collect/pid_$tag.txt 2>&1 || true; \
    ps | grep -E '[i]pacm|abi_bridge' > /tmp/ul_v5_iperf_collect/ps_$tag.txt 2>&1 || true; \
    for p in \$(pidof ipacm 2>/dev/null); do echo PID=\$p; tr '\0' '\n' </proc/\$p/environ 2>/dev/null | grep -E 'LD_PRELOAD|IPA_ABI_BRIDGE' || true; done > /tmp/ul_v5_iperf_collect/env_$tag.txt 2>&1; \
    wc -c /tmp/ipacm_abi_bridge_v5.log /tmp/ipacm_v5_iperf.out > /tmp/ul_v5_iperf_collect/logsize_$tag.txt 2>/dev/null || true; \
    tail -360 /tmp/ipacm_abi_bridge_v5.log > /tmp/ul_v5_iperf_collect/abi_tail_$tag.txt 2>/dev/null || true; \
    tail -420 /tmp/ipacm_v5_iperf.out > /tmp/ul_v5_iperf_collect/ipacm_tail_$tag.txt 2>/dev/null || true; \
    journalctl -b -u ipacm.service --no-pager | tail -260 > /tmp/ul_v5_iperf_collect/journal_ipacm_$tag.txt 2>&1 || true; \
    dmesg | tail -180 > /tmp/ul_v5_iperf_collect/dmesg_$tag.txt 2>/dev/null || true" >/dev/null 2>&1 || true
  for f in stats msg services ipacm_show route pid ps env logsize abi_tail ipacm_tail journal_ipacm dmesg; do adb_pull_tmp "/tmp/ul_v5_iperf_collect/${f}_${tag}.txt" "$OUT/${f}_${tag}.txt"; done
}
rollback(){
  if [ "$ACTIVATED" != 1 ]; then
    log "rollback skipped: v5 was not activated"
    return 0
  fi
  log "rollback start"
  adb shell 'rm -rf /run/systemd/system/ipacm.service.d/31-abi-bridge-v5-runtime.conf; systemctl daemon-reload; systemctl reset-failed ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service; systemctl restart ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service; sleep 45; systemctl is-active pcie-enabler.service ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service; ip route' > "$OUT/rollback.txt" 2>&1 || true
  dedup_modem_iptables rollback
  collect_modem rollback
  log "rollback done"
}
trap 'rollback; reset_remote_iperf' EXIT
log "preflight server=$SERVER port=$PORT iface=$IFACE bind=$HOST_BIND bind_mode=$BIND_MODE duration=${DURATION}s parallel=$PARALLEL udp=$UDP bitrate=$BITRATE"
collect_modem baseline
reset_remote_iperf
log "quick iperf reachability check"
PREFLIGHT_OK=0
: > "$OUT/iperf_preflight.txt"
for i in $(seq 1 "$IPERF_PREFLIGHT_TRIES"); do
  echo "TRY $i" >> "$OUT/iperf_preflight.txt"
  if iperf3 -c "$SERVER" -p "$PORT" "${BIND_ARGS[@]}" -t 1 --connect-timeout 8000 >> "$OUT/iperf_preflight.txt" 2>&1; then
    PREFLIGHT_OK=1
    break
  fi
  sleep 4
done
if [ "$PREFLIGHT_OK" != 1 ]; then
  log "iperf server not reachable; see $OUT/iperf_preflight.txt"
  exit 3
fi
log "push v5 to /tmp and install runtime systemd drop-in"
adb push "$LIB" /tmp/libipacm_abi_bridge_v5.so >/dev/null || exit 1
adb shell 'chmod 755 /tmp/libipacm_abi_bridge_v5.so; rm -f /tmp/ipacm_abi_bridge_v5.log /tmp/ipacm_v5_iperf.out; mkdir -p /run/systemd/system/ipacm.service.d; cat > /run/systemd/system/ipacm.service.d/31-abi-bridge-v5-runtime.conf <<'"'"'EOS'"'"'
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
StandardOutput=append:/tmp/ipacm_v5_iperf.out
StandardError=append:/tmp/ipacm_v5_iperf.out
EOS
systemctl daemon-reload
systemctl restart ipacm.service
sleep 8
systemctl restart QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service
' || exit 1
ACTIVATED=1
log "wait qcmap $WAIT_AFTER_QCMAP s"
sleep "$WAIT_AFTER_QCMAP"
dedup_modem_iptables after_activate
collect_modem after_activate
log "wait connectivity"
OK=0
: > "$OUT/connectivity_wait.txt"
for i in $(seq 1 "$CONNECT_TRIES"); do
  echo "TRY $i" >> "$OUT/connectivity_wait.txt"
  curl -4 --interface "$IFACE" --max-time 15 -k -L -w 'exit=%{exitcode} http=%{http_code} down=%{size_download} time=%{time_total}\n' 'https://1.1.1.1/cdn-cgi/trace' -o /tmp/ul_v5_iperf_wait_dl.bin >> "$OUT/connectivity_wait.txt" 2>&1 || true
  if tail -n 1 "$OUT/connectivity_wait.txt" | grep -q 'http=200'; then OK=1; break; fi
  sleep "$CONNECT_SLEEP"
done
if [ "$OK" != 1 ]; then log "connectivity failed under v5 iperf test"; collect_modem connectivity_failed; exit 2; fi
log "connectivity ok"
collect_modem before_upload
IPERF_ARGS=(-c "$SERVER" -p "$PORT" "${BIND_ARGS[@]}" -t "$DURATION" -P "$PARALLEL" --connect-timeout 5000 --json)
if [ "$UDP" = 1 ]; then IPERF_ARGS+=(-u -b "$BITRATE"); fi
log "run iperf3 ${IPERF_ARGS[*]}"
iperf3 "${IPERF_ARGS[@]}" > "$OUT/iperf3.json" 2> "$OUT/iperf3.stderr"
IPERF_RC=$?
echo "$IPERF_RC" > "$OUT/iperf3.rc"
collect_modem after_upload
python3 - "$OUT" <<'PY' | tee "$OUT/summary.txt"
from pathlib import Path
import json, re, sys
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
print('IPERF_RESULT')
print('rc='+read('iperf3.rc').strip())
try:
 data=json.loads(read('iperf3.json'))
 end=data.get('end',{})
 sent=end.get('sum_sent') or end.get('sum') or {}
 recv=end.get('sum_received') or {}
 print(f"sent_bytes={sent.get('bytes')} sent_bps={sent.get('bits_per_second')}")
 if recv: print(f"received_bytes={recv.get('bytes')} received_bps={recv.get('bits_per_second')}")
 print(f"error={data.get('error')}")
except Exception as e:
 print('json_parse_error='+str(e))
print('UPLOAD_COUNTER_DELTA')
b=parse_stats('before_upload'); a=parse_stats('after_upload')
for k in ['sw_tx','hw_tx','tx_compl','wan_rx','lan_NONE','lan_SW_FILT','lan_NAT','lan_INVALID_PIPE','lan_HDRI','lan_CSUM']:
 bv=b.get(k); av=a.get(k); print(f'{k},before={bv},after={av},delta={(av-bv) if bv is not None and av is not None else None}')
print('MSG_DELTA')
bm=parse_msg('before_upload'); am=parse_msg('after_upload')
for k in ['WAN_UPSTREAM_ROUTE_ADD','WAN_UPSTREAM_ROUTE_DEL','IPA_SET_MTU']:
 print(f'{k},before={bm.get(k)},after={am.get(k)}')
print('SERVICE_STATE')
for tag in ['after_activate','before_upload','after_upload','rollback']:
 if (out/f'services_{tag}.txt').exists():
  print(f'{tag},services={service_line(tag)},ipacm_active={show_val(tag,"ActiveState")},sub={show_val(tag,"SubState")},pid={show_val(tag,"MainPID")},nrestarts={show_val(tag,"NRestarts")}')
PY
rg -n "ret=0xffffffff|nf_nat_setup_info|rt rule does not|failed to add rt rule|duplicate hdr|WARNING|Call trace|ADD_FLT_V2|ADD_RT_V2" "$OUT" -S > "$OUT/key_hits.txt" 2>&1 || true
log "test complete rc=$IPERF_RC"
exit "$IPERF_RC"

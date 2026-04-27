#!/usr/bin/env bash
set -euo pipefail
BASE=${BASE:-"/home/manu/Scrivania/linaro 5.4/load_ipa_ES"}
IFACE=${IFACE:-enx00e04c6802a5}
SERVER=${SERVER:-5.182.48.12}
PORT=${PORT:-5201}
DURATION=${DURATION:-30}
PARALLEL=${PARALLEL:-1}
RUNS=${RUNS:-1}
RESET_REMOTE_IPERF=${RESET_REMOTE_IPERF:-0}
SERVER_SSH=${SERVER_SSH:-root@$SERVER}
SERVER_SSH_PORT=${SERVER_SSH_PORT:-22}
ABI_LOG=${ABI_LOG:-/tmp/ipacm_abi_bridge_final.log}
RUN_TS=$(date +%Y-%m-%d_%H-%M-%S)
OUT="$BASE/ul_v5_active_iperf3_$RUN_TS"
mkdir -p "$OUT"
echo "$OUT" > "$BASE/latest_ul_v5_active_iperf3_path.txt"
log(){ printf '%s %s\n' "$(date '+%F %T')" "$*" | tee -a "$OUT/run.log"; }
reset_remote(){
  [ "$RESET_REMOTE_IPERF" = 1 ] || return 0
  log "remote iperf reset"
  if command -v sshpass >/dev/null 2>&1 && [ -n "${SSHPASS:-}" ]; then
    sshpass -e ssh -p "$SERVER_SSH_PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/ipa_iperf_known_hosts "$SERVER_SSH" 'systemctl restart iperf3-ipa.service; sleep 1; systemctl is-active iperf3-ipa.service; ss -ltnp | grep :5201 || true' >> "$OUT/remote_iperf_reset.txt" 2>&1 || true
  else
    ssh -p "$SERVER_SSH_PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/tmp/ipa_iperf_known_hosts "$SERVER_SSH" 'systemctl restart iperf3-ipa.service; sleep 1; systemctl is-active iperf3-ipa.service; ss -ltnp | grep :5201 || true' >> "$OUT/remote_iperf_reset.txt" 2>&1 || true
  fi
}
dedup(){
  adb shell 'for iface in rmnet_data0 rmnet_data1; do count=$(iptables -t nat -S POSTROUTING | grep -F -- "-A POSTROUTING -o $iface -j MASQUERADE --random" | wc -l); while [ "$count" -gt 1 ]; do iptables -t nat -D POSTROUTING -o "$iface" -j MASQUERADE --random || break; count=$((count-1)); done; done; for iface in rmnet_data0 rmnet_data1; do for port in 80 443; do count=$(iptables -S INPUT | grep -F -- "-A INPUT -i $iface -p tcp -m tcp --dport $port -j DROP" | wc -l); while [ "$count" -gt 1 ]; do iptables -D INPUT -i "$iface" -p tcp -m tcp --dport "$port" -j DROP || break; count=$((count-1)); done; done; done' >/dev/null 2>&1 || true
}
collect(){
  local tag="$1"
  adb shell "mkdir -p /tmp/ul_active_collect; cat /sys/kernel/debug/ipa/stats > /tmp/ul_active_collect/stats_$tag.txt 2>/dev/null || true; cat /sys/kernel/debug/ipa/msg > /tmp/ul_active_collect/msg_$tag.txt 2>/dev/null || true; { cat /sys/kernel/debug/ipa/eth/RTK8125B_0_stats 2>/dev/null; echo ---; cat /sys/kernel/debug/ipa/eth/RTK8125B_0_status 2>/dev/null; } > /tmp/ul_active_collect/eth_$tag.txt 2>&1 || true; systemctl is-active pcie-enabler.service ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service > /tmp/ul_active_collect/services_$tag.txt 2>&1 || true; systemctl show ipacm.service -p ActiveState -p SubState -p MainPID -p NRestarts -p Environment > /tmp/ul_active_collect/ipacm_show_$tag.txt 2>&1 || true; for p in \$(pidof ipacm 2>/dev/null); do echo PID=\$p; tr '\0' '\n' </proc/\$p/environ 2>/dev/null | grep -E 'LD_PRELOAD|IPA_ABI_BRIDGE' || true; done > /tmp/ul_active_collect/env_$tag.txt 2>&1; ip route > /tmp/ul_active_collect/route_$tag.txt 2>&1; iptables -t nat -S POSTROUTING > /tmp/ul_active_collect/nat_$tag.txt 2>&1; dmesg | tail -220 > /tmp/ul_active_collect/dmesg_$tag.txt 2>&1; tail -360 '$ABI_LOG' > /tmp/ul_active_collect/abi_tail_$tag.txt 2>/dev/null || true; grep -nEi 'handle_uplink_filter_rule|TriggerWANUp|Setting up QMAP|AddEntry\\(|Added rule|Duplicate rule|Failed|failed' /usrdata/ipacm_log.txt 2>/dev/null | tail -260 > /tmp/ul_active_collect/ipacm_hits_$tag.txt || true" >/dev/null 2>&1 || true
  for f in stats msg eth services ipacm_show env route nat dmesg abi_tail ipacm_hits; do adb pull "/tmp/ul_active_collect/${f}_${tag}.txt" "$OUT/${f}_${tag}.txt" >/dev/null 2>&1 || true; done
}
log "active iperf test server=$SERVER port=$PORT duration=$DURATION runs=$RUNS"
dedup
collect baseline
for i in $(seq 1 "$RUNS"); do
  reset_remote
  log "run $i start"
  collect "run${i}_before"
  iperf3 -c "$SERVER" -p "$PORT" --bind-dev "$IFACE" -t "$DURATION" -P "$PARALLEL" --connect-timeout 8000 --json > "$OUT/iperf_run${i}.json" 2> "$OUT/iperf_run${i}.stderr" || echo $? > "$OUT/iperf_run${i}.rc"
  [ -f "$OUT/iperf_run${i}.rc" ] || echo 0 > "$OUT/iperf_run${i}.rc"
  collect "run${i}_after"
  dedup
  log "run $i done rc=$(cat "$OUT/iperf_run${i}.rc")"
  sleep 5
done
collect final
python3 - "$OUT" "$RUNS" <<'PY' | tee "$OUT/summary.csv"
from pathlib import Path
import json,re,sys
out=Path(sys.argv[1]); runs=int(sys.argv[2])
def read(n):
 p=out/n
 return p.read_text(errors='ignore') if p.exists() else ''
def stat(tag,k):
 s=read(f'stats_{tag}.txt')
 m=re.search(r'\b'+re.escape(k)+r'=(\d+)',s)
 return int(m.group(1)) if m else None
def eth(tag,k):
 s=read(f'eth_{tag}.txt')
 m=re.search(r'\b'+re.escape(k)+r'=(\d+)',s)
 return int(m.group(1)) if m else None
def exc(tag,name):
 s=read(f'stats_{tag}.txt')
 m=re.search(r'lan_rx_excp\[\d+:IPAHAL_PKT_STATUS_EXCEPTION_'+name+r'\]=(\d+)',s)
 return int(m.group(1)) if m else None
def show(tag,key):
 m=re.search(r'^'+re.escape(key)+r'=(.*)$', read(f'ipacm_show_{tag}.txt'), re.M)
 return m.group(1) if m else 'NA'
print('run,rc,sent_bytes,sent_bps,received_bytes,received_bps,sw_tx_delta,hw_tx_delta,rtk_cons_ring_delta,rtk_prod_ring_delta,SW_FILT_delta,NAT_delta,INVALID_PIPE_delta,HDRI_delta,CSUM_delta,pid_before,pid_after,nrestarts_after')
for i in range(1,runs+1):
 data={}
 try: data=json.loads(read(f'iperf_run{i}.json'))
 except Exception: pass
 end=data.get('end',{}) if isinstance(data,dict) else {}
 sent=end.get('sum_sent') or end.get('sum') or {}
 recv=end.get('sum_received') or {}
 b=f'run{i}_before'; a=f'run{i}_after'
 def delta(fn,*args):
  x=fn(b,*args); y=fn(a,*args); return '' if x is None or y is None else y-x
 print(','.join(map(str,[i,read(f'iperf_run{i}.rc').strip(),sent.get('bytes',''),sent.get('bits_per_second',''),recv.get('bytes',''),recv.get('bits_per_second',''),delta(stat,'sw_tx'),delta(stat,'hw_tx'),delta(eth,'IPA_CLIENT_RTK_ETHERNET_CONS_RingUtilCount'),delta(eth,'IPA_CLIENT_RTK_ETHERNET_PROD_RingUtilCount'),delta(exc,'SW_FILT'),delta(exc,'NAT'),delta(exc,'INVALID_PIPE'),delta(exc,'HDRI'),delta(exc,'CSUM'),show(b,'MainPID'),show(a,'MainPID'),show(a,'NRestarts') ])))
PY
rg -n "ret=0xffffffff|nf_nat_setup_info|rt rule does not|failed to add rt rule|duplicate hdr|WARNING|ADD_FLT_V2|ADD_RT_V2" "$OUT" -S > "$OUT/key_hits.txt" 2>&1 || true
log "all runs complete: $OUT"

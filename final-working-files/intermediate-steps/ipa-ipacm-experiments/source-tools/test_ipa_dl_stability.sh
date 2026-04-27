#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IFACE="${IFACE:-enx00e04c6802a5}"
BYTES="${BYTES:-50000000}"
RUNS="${RUNS:-3}"
MAX_TIME="${MAX_TIME:-120}"
ADB="${ADB:-adb}"
URL="${URL:-https://speed.cloudflare.com/__down?bytes=${BYTES}}"
STAMP="$(date '+%Y-%m-%d_%H-%M-%S')"
OUT_ROOT="${OUT_ROOT:-${SCRIPT_DIR}/dl_validation_${STAMP}}"
SUMMARY_CSV="${OUT_ROOT}/summary.csv"
SUMMARY_TXT="${OUT_ROOT}/summary.txt"

mkdir -p "$OUT_ROOT"

log() {
  printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S %Z')" "$*" | tee -a "$SUMMARY_TXT"
}

adb_capture() {
  local out_dir="$1"
  local phase="$2"
  mkdir -p "$out_dir"
  "$ADB" shell 'date; uptime; cat /proc/uptime' > "${out_dir}/${phase}_uptime.txt" 2>&1 || true
  "$ADB" shell 'systemctl is-active pcie-enabler.service ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service' > "${out_dir}/${phase}_services.txt" 2>&1 || true
  "$ADB" shell 'systemctl status --no-pager pcie-enabler.service ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service' > "${out_dir}/${phase}_services_status.txt" 2>&1 || true
  "$ADB" shell 'cat /proc/modules | grep -E "^(rmnet_eth|r8125|ioss|r8125_ioss) "' > "${out_dir}/${phase}_modules.txt" 2>&1 || true
  "$ADB" shell 'ip route; printf "\n--- ip -br addr ---\n"; ip -br addr 2>/dev/null' > "${out_dir}/${phase}_route.txt" 2>&1 || true
  "$ADB" shell 'cat /sys/kernel/debug/ipa/stats' > "${out_dir}/${phase}_ipa_stats.txt" 2>&1 || true
  "$ADB" shell 'cat /sys/kernel/debug/ipa/msg' > "${out_dir}/${phase}_ipa_msg.txt" 2>&1 || true
  "$ADB" shell 'dmesg | tail -n 160' > "${out_dir}/${phase}_dmesg_tail.txt" 2>&1 || true
}

host_capture() {
  local out_dir="$1"
  local phase="$2"
  ip -br addr show "$IFACE" > "${out_dir}/${phase}_host_iface_addr.txt" 2>&1 || true
  ip route > "${out_dir}/${phase}_host_route.txt" 2>&1 || true
  ethtool "$IFACE" > "${out_dir}/${phase}_host_ethtool.txt" 2>&1 || true
}

counter_value() {
  local file="$1"
  local name="$2"
  grep -Eo "${name}=[0-9]+" "$file" 2>/dev/null | tail -n 1 | cut -d= -f2
}

sw_filt_value() {
  local file="$1"
  grep -Eo 'lan_rx_excp\[6[^]]*\]=[0-9]+' "$file" 2>/dev/null | tail -n 1 | cut -d= -f2
}

uptime_seconds() {
  local file="$1"
  awk 'NF >= 1 && $1 ~ /^[0-9]+(\.[0-9]+)?$/ {print int($1); exit}' "$file" 2>/dev/null
}

num_or_zero() {
  local value="${1:-}"
  if [[ "$value" =~ ^[0-9]+$ ]]; then
    printf '%s' "$value"
  else
    printf '0'
  fi
}

is_active_set() {
  local file="$1"
  local active_count total_count
  active_count="$(tr -d '\r' < "$file" 2>/dev/null | grep -cx 'active' || true)"
  total_count="$(tr -d '\r' < "$file" 2>/dev/null | grep -Ecx 'active|inactive|failed|activating|deactivating|unknown' || true)"
  [[ "$active_count" -eq 5 && "$total_count" -eq 5 ]]
}

printf 'run_id,file_bytes,hw_tx_delta,sw_tx_delta,SW_FILT_delta,curl_exit,http_code,time_total,services_active,no_reboot,esito\n' > "$SUMMARY_CSV"
log "DL validation start: iface=${IFACE}, bytes=${BYTES}, runs=${RUNS}, max_time=${MAX_TIME}, out=${OUT_ROOT}"

for run_id in $(seq 1 "$RUNS"); do
  run_dir="${OUT_ROOT}/run_${run_id}"
  mkdir -p "$run_dir"
  out_file="${run_dir}/down_${BYTES}.bin"
  log "Run ${run_id}: snapshot before"
  host_capture "$run_dir" before
  adb_capture "$run_dir" before

  before_stats="${run_dir}/before_ipa_stats.txt"
  before_hw="$(num_or_zero "$(counter_value "$before_stats" hw_tx)")"
  before_sw="$(num_or_zero "$(counter_value "$before_stats" sw_tx)")"
  before_filt="$(num_or_zero "$(sw_filt_value "$before_stats")")"
  before_uptime="$(num_or_zero "$(uptime_seconds "${run_dir}/before_uptime.txt")")"

  log "Run ${run_id}: curl download"
  curl_exit=0
  curl -4 --interface "$IFACE" --max-time "$MAX_TIME" -L "$URL" -o "$out_file" \
    -w 'http_code=%{http_code}\ntime_total=%{time_total}\nsize_download=%{size_download}\nspeed_download=%{speed_download}\n' \
    > "${run_dir}/curl_metrics.txt" 2> "${run_dir}/curl_stderr.txt" || curl_exit=$?

  log "Run ${run_id}: snapshot after"
  host_capture "$run_dir" after
  adb_capture "$run_dir" after

  after_stats="${run_dir}/after_ipa_stats.txt"
  after_hw="$(num_or_zero "$(counter_value "$after_stats" hw_tx)")"
  after_sw="$(num_or_zero "$(counter_value "$after_stats" sw_tx)")"
  after_filt="$(num_or_zero "$(sw_filt_value "$after_stats")")"
  after_uptime="$(num_or_zero "$(uptime_seconds "${run_dir}/after_uptime.txt")")"

  hw_delta=$((after_hw - before_hw))
  sw_delta=$((after_sw - before_sw))
  filt_delta=$((after_filt - before_filt))
  file_bytes="$(stat -c '%s' "$out_file" 2>/dev/null || printf '0')"
  http_code="$(awk -F= '$1 == "http_code" {print $2}' "${run_dir}/curl_metrics.txt" 2>/dev/null | tail -n 1)"
  time_total="$(awk -F= '$1 == "time_total" {print $2}' "${run_dir}/curl_metrics.txt" 2>/dev/null | tail -n 1)"

  services_active=no
  if is_active_set "${run_dir}/after_services.txt"; then
    services_active=yes
  fi

  no_reboot=yes
  if [[ "$after_uptime" -lt "$before_uptime" ]]; then
    no_reboot=no
  fi

  esito=FAIL
  if [[ "$curl_exit" -eq 0 && "$file_bytes" -eq "$BYTES" && "$hw_delta" -gt 0 && "$sw_delta" -eq 0 && "$services_active" == yes && "$no_reboot" == yes ]]; then
    esito=PASS
  fi

  cat > "${run_dir}/result_summary.txt" <<RUNEOF
run_id=${run_id}
file_bytes=${file_bytes}
hw_tx_before=${before_hw}
hw_tx_after=${after_hw}
hw_tx_delta=${hw_delta}
sw_tx_before=${before_sw}
sw_tx_after=${after_sw}
sw_tx_delta=${sw_delta}
SW_FILT_before=${before_filt}
SW_FILT_after=${after_filt}
SW_FILT_delta=${filt_delta}
curl_exit=${curl_exit}
http_code=${http_code:-NA}
time_total=${time_total:-NA}
services_active=${services_active}
no_reboot=${no_reboot}
esito=${esito}
RUNEOF

  printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
    "$run_id" "$file_bytes" "$hw_delta" "$sw_delta" "$filt_delta" "$curl_exit" "${http_code:-NA}" "${time_total:-NA}" "$services_active" "$no_reboot" "$esito" >> "$SUMMARY_CSV"
  log "Run ${run_id}: ${esito} bytes=${file_bytes} hw_tx_delta=${hw_delta} sw_tx_delta=${sw_delta} SW_FILT_delta=${filt_delta}"
done

log "DL validation complete: ${SUMMARY_CSV}"

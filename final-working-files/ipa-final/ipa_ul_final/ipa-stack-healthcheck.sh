#!/bin/sh
# Conservative watchdog for the QCMAP + IPA final stack.
# It only acts on failed/inactive units, never restarts already healthy services.

PATH=/usr/sbin:/usr/bin:/sbin:/bin
LOG=${IPA_STACK_HEALTH_LOG:-/tmp/ipa-stack-healthcheck.log}
LOCK=/tmp/ipa-stack-healthcheck.lock

log() {
    printf '%s %s\n' "$(date '+%F %T')" "$*" >> "$LOG"
}

unit_state() {
    systemctl is-active "$1" 2>/dev/null || true
}

is_ok() {
    case "$(unit_state "$1")" in
        active|activating) return 0 ;;
        *) return 1 ;;
    esac
}

restart_unit() {
    unit="$1"
    log "restart $unit state=$(unit_state "$unit")"
    systemctl reset-failed "$unit" >/dev/null 2>&1 || true
    systemctl restart "$unit" >> "$LOG" 2>&1 || log "restart failed $unit rc=$?"
}

# Avoid overlapping timer runs if systemctl hangs for a moment.
if ! mkdir "$LOCK" 2>/dev/null; then
    exit 0
fi
trap 'rmdir "$LOCK" 2>/dev/null || true' EXIT

# Keep the log bounded on tmpfs.
if [ -f "$LOG" ]; then
    size=$(wc -c < "$LOG" 2>/dev/null || echo 0)
    if [ "$size" -gt 131072 ]; then
        tail -n 400 "$LOG" > "$LOG.tmp" 2>/dev/null && mv "$LOG.tmp" "$LOG"
    fi
fi

changed=0

# Reload if units were updated on disk; harmless when nothing changed.
systemctl daemon-reload >/dev/null 2>&1 || true

if ! is_ok pcie-enabler.service; then
    restart_unit pcie-enabler.service
    changed=1
    sleep 2
fi

if ! is_ok ipacm.service; then
    restart_unit ipacm.service
    changed=1
    sleep 3
fi

if ! is_ok netmgrd.service; then
    restart_unit netmgrd.service
    changed=1
    sleep 2
fi

if ! is_ok QCMAP_ConnectionManagerd.service; then
    restart_unit QCMAP_ConnectionManagerd.service
    changed=1
    sleep 3
fi

if ! is_ok qcmap-radio-on.service; then
    restart_unit qcmap-radio-on.service
    changed=1
fi

if ! is_ok qcmap_httpd.service; then
    restart_unit qcmap_httpd.service
    changed=1
fi

if ! is_ok ipa-iptables-dedupe.timer; then
    log "enable/start ipa-iptables-dedupe.timer state=$(unit_state ipa-iptables-dedupe.timer)"
    systemctl enable ipa-iptables-dedupe.timer >/dev/null 2>&1 || true
    systemctl start ipa-iptables-dedupe.timer >> "$LOG" 2>&1 || true
    changed=1
fi

# Dedup is intentionally safe/idempotent and prevents QCMAP retry loops from
# accumulating duplicate MASQUERADE/drop rules.
/usr/bin/ipa-iptables-dedupe.sh >/dev/null 2>> "$LOG" || true

if [ "$changed" -eq 1 ]; then
    log "post-check services: $(systemctl is-active pcie-enabler.service ipacm.service QCMAP_ConnectionManagerd.service netmgrd.service qcmap-radio-on.service ipa-iptables-dedupe.timer qcmap_httpd.service 2>/dev/null | tr '\n' ' ')"
fi

exit 0

#!/bin/sh

log() {
    printf '[pcie-non-es] %s\n' "$*"
}

load_module() {
    module_name="$1"
    module_path="$2"
    shift 2

    if grep -q "^${module_name} " /proc/modules 2>/dev/null; then
        log "already loaded: ${module_name}"
        return 0
    fi

    if [ ! -f "${module_path}" ]; then
        echo "Missing module: ${module_path}" >&2
        return 1
    fi

    log "insmod ${module_name}"
    /sbin/insmod "${module_path}" "$@"
}

# Out-of-tree module load on stock NON-ES needs permissive or a matching SELinux
# policy. The original bundle already uses disable-selinux.service; keep this as
# a belt-and-braces fallback for manual/service restarts.
setenforce 0 2>/dev/null || true

# kallsyms addresses are hidden on stock boot with kptr_restrict=2.
echo 0 > /proc/sys/kernel/kptr_restrict 2>/dev/null || true
KALLSYMS_ADDR=$(grep ' kallsyms_lookup_name$' /proc/kallsyms 2>/dev/null | cut -d' ' -f1 | tail -n 1)
if [ -z "$KALLSYMS_ADDR" ] || [ "$KALLSYMS_ADDR" = "00000000" ]; then
    echo "Unable to resolve kallsyms_lookup_name address" >&2
    exit 1
fi
log "kallsyms_lookup_name=0x${KALLSYMS_ADDR}"

# NON-ES secure-boot path:
# 0) runtime-enable PCIe RC + inject RTL8125/IOSS DT + apply V22 PMDS fix
# 1) rmnet_eth prerequisite for IPA WAN
# 2) Realtek NIC driver
# 3) IOSS core
# 4) Realtek/IOSS glue
load_module "pcie_enabler_non_es_v1" "/moduli/pcie_enabler_non_es_v1.ko" "kallsyms_lookup_name_addr=0x${KALLSYMS_ADDR}" || exit 1
sleep 2
load_module "rmnet_eth" "/moduli/rmnet_eth.ko" || exit 1
sleep 1
load_module "r8125" "/moduli/r8125_rebuilt.ko" || exit 1
sleep 2
load_module "ioss" "/moduli/ioss_rebuilt.ko" || exit 1
sleep 2
load_module "r8125_ioss" "/moduli/r8125_ioss_rebuilt.ko" || exit 1
sleep 2

# Bring up the NIC if present; carrier may take a few seconds.
ip link set eth0 up 2>/dev/null || true

exit 0

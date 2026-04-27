#!/bin/sh

load_module() {
    module_name="$1"
    module_path="$2"

    if grep -q "^${module_name} " /proc/modules 2>/dev/null; then
        return 0
    fi

    if [ ! -f "${module_path}" ]; then
        echo "Missing module: ${module_path}" >&2
        return 1
    fi

    /sbin/insmod "${module_path}"
}

# Stack corretto per IPA ETH:
# 0) rmnet_eth (prerequisito IPA WAN),
# 1) driver NIC, 2) IOSS core, 3) glue IOSS<->R8125
load_module "rmnet_eth" "/moduli/rmnet_eth.ko" || exit 1
load_module "r8125" "/moduli/r8125_rebuilt.ko" || exit 1
load_module "ioss" "/moduli/ioss_rebuilt.ko" || exit 1
load_module "r8125_ioss" "/moduli/r8125_ioss_rebuilt.ko" || exit 1

# Porta su la NIC se presente (non fallire se carrier assente)
ip link set eth0 up 2>/dev/null || true

exit 0

#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "Usage: $0 <kernel-src> <workspace-dir> <data-eth-src>" >&2
    exit 1
fi

KERNEL_SRC="$1"
WORKSPACE_DIR="$2"
DATA_ETH_SRC="$3"

SAFE_ROOT="$(mktemp -d)"
trap 'rm -rf "${SAFE_ROOT}"' EXIT

ln -s "${KERNEL_SRC}" "${SAFE_ROOT}/kernel"
mkdir -p "${SAFE_ROOT}/data-eth"
cp -a "${DATA_ETH_SRC}/." "${SAFE_ROOT}/data-eth/"

SAFE_KERNEL_SRC="${SAFE_ROOT}/kernel"
SAFE_KERNEL_OUT="${SAFE_ROOT}/kernel-out"
SAFE_DATA_ETH_SRC="${SAFE_ROOT}/data-eth"
mkdir -p "${SAFE_KERNEL_OUT}"

cp "${WORKSPACE_DIR}/config.txt" "${SAFE_KERNEL_OUT}/.config"
sed -i '/^CONFIG_IOSS=/d;/^CONFIG_R8125=/d;/^CONFIG_R8125_IOSS=/d' "${SAFE_KERNEL_OUT}/.config"
printf 'CONFIG_IOSS=m\nCONFIG_R8125=m\nCONFIG_R8125_IOSS=m\n' >> "${SAFE_KERNEL_OUT}/.config"

if grep -q '^CONFIG_MODULE_SIG_KEY=' "${SAFE_KERNEL_OUT}/.config"; then
    sed -i 's|^CONFIG_MODULE_SIG_KEY=.*|CONFIG_MODULE_SIG_KEY="certs/signing_key.pem"|' "${SAFE_KERNEL_OUT}/.config"
fi

mkdir -p "${SAFE_KERNEL_OUT}/certs"
openssl req -new -x509 -newkey rsa:2048 \
    -keyout "${SAFE_KERNEL_OUT}/certs/signing_key.pem" \
    -out "${SAFE_KERNEL_OUT}/certs/signing_key.pem" \
    -nodes -days 36500 -subj "/CN=Out-of-tree module build/" >/dev/null 2>&1

export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-
export LLVM=1
export LLVM_IAS=0
export KCFLAGS="${KCFLAGS:-} -Wno-error -Wno-error=unused-label"

make -C "${SAFE_KERNEL_SRC}" O="${SAFE_KERNEL_OUT}" olddefconfig >/dev/null
make -C "${SAFE_KERNEL_SRC}" O="${SAFE_KERNEL_OUT}" modules_prepare >/dev/null
make -C "${SAFE_KERNEL_SRC}" O="${SAFE_KERNEL_OUT}" M="${SAFE_DATA_ETH_SRC}" \
    CONFIG_IOSS=m CONFIG_R8125=m CONFIG_R8125_IOSS=m building_out_of_srctree=1 modules

cp "${SAFE_DATA_ETH_SRC}/drivers/ioss/ioss.ko" "${WORKSPACE_DIR}/ioss_rebuilt.ko"
cp "${SAFE_DATA_ETH_SRC}/drivers/r8125/src/r8125.ko" "${WORKSPACE_DIR}/r8125_stack.ko"
cp "${SAFE_DATA_ETH_SRC}/drivers/r8125_ioss/r8125_ioss.ko" "${WORKSPACE_DIR}/r8125_ioss_rebuilt.ko"

"${CROSS_COMPILE}strip" --strip-debug "${WORKSPACE_DIR}/ioss_rebuilt.ko" || true
"${CROSS_COMPILE}strip" --strip-debug "${WORKSPACE_DIR}/r8125_stack.ko" || true
"${CROSS_COMPILE}strip" --strip-debug "${WORKSPACE_DIR}/r8125_ioss_rebuilt.ko" || true

modinfo "${WORKSPACE_DIR}/ioss_rebuilt.ko" | sed -n '1,20p'
echo ---
modinfo "${WORKSPACE_DIR}/r8125_stack.ko" | sed -n '1,20p'
echo ---
modinfo "${WORKSPACE_DIR}/r8125_ioss_rebuilt.ko" | sed -n '1,20p'

#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <kernel-src> <workspace-dir>" >&2
    exit 1
fi

KERNEL_SRC="$1"
WORKSPACE_DIR="$2"

SAFE_ROOT="$(mktemp -d)"
trap 'rm -rf "${SAFE_ROOT}"' EXIT

ln -s "${KERNEL_SRC}" "${SAFE_ROOT}/kernel"
SAFE_KERNEL_SRC="${SAFE_ROOT}/kernel"
SAFE_KERNEL_OUT="${SAFE_ROOT}/kernel-out"
mkdir -p "${SAFE_KERNEL_OUT}"

cp "${WORKSPACE_DIR}/config.txt" "${SAFE_KERNEL_OUT}/.config"
sed -i '/^CONFIG_R8169=/d;/^CONFIG_REALTEK_PHY=/d' "${SAFE_KERNEL_OUT}/.config"
printf 'CONFIG_R8169=m\nCONFIG_REALTEK_PHY=m\n' >> "${SAFE_KERNEL_OUT}/.config"

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

make -C "${SAFE_KERNEL_SRC}" O="${SAFE_KERNEL_OUT}" olddefconfig >/dev/null
make -C "${SAFE_KERNEL_SRC}" O="${SAFE_KERNEL_OUT}" modules_prepare >/dev/null
make -C "${SAFE_KERNEL_SRC}" O="${SAFE_KERNEL_OUT}" M=drivers/net/ethernet/realtek modules
make -C "${SAFE_KERNEL_SRC}" O="${SAFE_KERNEL_OUT}" M=drivers/net/phy modules

cp "${SAFE_KERNEL_OUT}/drivers/net/ethernet/realtek/r8169.ko" "${WORKSPACE_DIR}/r8169.ko"
cp "${SAFE_KERNEL_OUT}/drivers/net/phy/realtek.ko" "${WORKSPACE_DIR}/realtek.ko"
"${CROSS_COMPILE}strip" --strip-debug "${WORKSPACE_DIR}/r8169.ko" || true
"${CROSS_COMPILE}strip" --strip-debug "${WORKSPACE_DIR}/realtek.ko" || true

modinfo "${WORKSPACE_DIR}/r8169.ko" | sed -n '1,20p'

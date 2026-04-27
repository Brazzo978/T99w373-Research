#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "Usage: $0 <kernel-src> <workspace-dir> <module-src>" >&2
    exit 1
fi

KERNEL_SRC="$1"
WORKSPACE_DIR="$2"
MODULE_SRC="$3"

SAFE_ROOT="$(mktemp -d)"
trap 'rm -rf "${SAFE_ROOT}"' EXIT

ln -s "${KERNEL_SRC}" "${SAFE_ROOT}/kernel"
mkdir -p "${SAFE_ROOT}/module"
cp -a "${MODULE_SRC}/." "${SAFE_ROOT}/module/"

SAFE_KERNEL_SRC="${SAFE_ROOT}/kernel"
SAFE_KERNEL_OUT="${SAFE_ROOT}/kernel-out"
SAFE_MODULE_SRC="${SAFE_ROOT}/module"
mkdir -p "${SAFE_KERNEL_OUT}"

cp "${WORKSPACE_DIR}/config.txt" "${SAFE_KERNEL_OUT}/.config"
sed -i '/^CONFIG_R8125=/d' "${SAFE_KERNEL_OUT}/.config"
printf 'CONFIG_R8125=m\n' >> "${SAFE_KERNEL_OUT}/.config"

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
make -C "${SAFE_KERNEL_SRC}" O="${SAFE_KERNEL_OUT}" M="${SAFE_MODULE_SRC}" CONFIG_R8125=m modules

cp "${SAFE_MODULE_SRC}/r8125.ko" "${WORKSPACE_DIR}/r8125_rebuilt.ko"
"${CROSS_COMPILE}strip" --strip-debug "${WORKSPACE_DIR}/r8125_rebuilt.ko" || true

modinfo "${WORKSPACE_DIR}/r8125_rebuilt.ko" | sed -n '1,30p'

#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${KERNEL_SRC:-}" ]]; then
    echo "KERNEL_SRC is not set" >&2
    exit 1
fi

if [[ -z "${MODULE_SRC:-}" ]]; then
    echo "MODULE_SRC is not set" >&2
    exit 1
fi

if [[ ! -d "${KERNEL_SRC}" ]]; then
    echo "KERNEL_SRC does not exist: ${KERNEL_SRC}" >&2
    exit 1
fi

if [[ ! -d "${MODULE_SRC}" ]]; then
    echo "MODULE_SRC does not exist: ${MODULE_SRC}" >&2
    exit 1
fi

CONFIG_SRC="${CONFIG_SRC:-${MODULE_SRC}/config.txt}"

if [[ ! -f "${CONFIG_SRC}" ]]; then
    echo "Kernel config file not found: ${CONFIG_SRC}" >&2
    exit 1
fi

SAFE_ROOT="$(mktemp -d)"
trap 'rm -rf "${SAFE_ROOT}"' EXIT

ln -s "${KERNEL_SRC}" "${SAFE_ROOT}/kernel"
mkdir -p "${SAFE_ROOT}/module"
cp -a "${MODULE_SRC}/." "${SAFE_ROOT}/module/"

SAFE_KERNEL_SRC="${SAFE_ROOT}/kernel"
SAFE_MODULE_SRC="${SAFE_ROOT}/module"
SAFE_KERNEL_OUT="${SAFE_ROOT}/kernel-out"

mkdir -p "${SAFE_KERNEL_OUT}"

# Install the device config as the kernel build config.
cp "${CONFIG_SRC}" "${SAFE_KERNEL_OUT}/.config"

if grep -q '^CONFIG_MODULE_SIG_KEY=' "${SAFE_KERNEL_OUT}/.config"; then
    sed -i 's|^CONFIG_MODULE_SIG_KEY=.*|CONFIG_MODULE_SIG_KEY="certs\/signing_key.pem"|' "${SAFE_KERNEL_OUT}/.config"
fi

if grep -q '^CONFIG_ARM64=y' "${SAFE_KERNEL_OUT}/.config"; then
    export ARCH="${ARCH:-arm64}"
    export CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"
elif grep -q '^CONFIG_ARM=y' "${SAFE_KERNEL_OUT}/.config"; then
    export ARCH="${ARCH:-arm}"
    export CROSS_COMPILE="${CROSS_COMPILE:-arm-linux-gnueabihf-}"
else
    echo "Unable to infer target architecture from ${SAFE_KERNEL_OUT}/.config" >&2
    exit 1
fi

if grep -q '^CONFIG_CC_IS_CLANG=y' "${SAFE_KERNEL_OUT}/.config"; then
    export LLVM="${LLVM:-1}"
    export LLVM_IAS="${LLVM_IAS:-0}"
fi

if grep -q '^CONFIG_MODULE_SIG_ALL=y' "${SAFE_KERNEL_OUT}/.config" && [[ ! -f "${SAFE_KERNEL_OUT}/certs/signing_key.pem" ]]; then
    mkdir -p "${SAFE_KERNEL_OUT}/certs"
    openssl req -new -x509 -newkey rsa:2048 \
        -keyout "${SAFE_KERNEL_OUT}/certs/signing_key.pem" \
        -out "${SAFE_KERNEL_OUT}/certs/signing_key.pem" \
        -nodes -days 36500 -subj "/CN=Out-of-tree module build/"
fi

# Prepare generated headers and module build metadata.
make -C "${SAFE_KERNEL_SRC}" O="${SAFE_KERNEL_OUT}" modules_prepare

# Build the out-of-tree module against the prepared kernel tree.
make -C "${SAFE_KERNEL_SRC}" O="${SAFE_KERNEL_OUT}" M="${SAFE_MODULE_SRC}" modules

if [[ -n "${MODULE_NAME:-}" ]]; then
    KO_PATH="${SAFE_MODULE_SRC}/${MODULE_NAME}.ko"
else
    mapfile -t KO_CANDIDATES < <(find "${SAFE_MODULE_SRC}" -maxdepth 1 -type f -name '*.ko' | sort)
    if [[ "${#KO_CANDIDATES[@]}" -eq 1 ]]; then
        KO_PATH="${KO_CANDIDATES[0]}"
    else
        echo "Unable to determine output .ko. Set MODULE_NAME explicitly." >&2
        printf 'Found candidates:\n' >&2
        printf '  %s\n' "${KO_CANDIDATES[@]}" >&2
        exit 1
    fi
fi

if [[ ! -f "${KO_PATH}" ]]; then
    echo "Expected module not found: ${KO_PATH}" >&2
    exit 1
fi

# Optional size reduction: remove debug symbols if the matching strip tool is available.
STRIP_BIN="${CROSS_COMPILE}strip"
if command -v "${STRIP_BIN}" >/dev/null 2>&1; then
    "${STRIP_BIN}" --strip-debug "${KO_PATH}" || true
fi

FINAL_KO_PATH="${MODULE_SRC}/$(basename "${KO_PATH}")"
cp "${KO_PATH}" "${FINAL_KO_PATH}"

echo "Build completed: ${FINAL_KO_PATH}"

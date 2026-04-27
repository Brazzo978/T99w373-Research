FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        bc \
        binutils-arm-linux-gnueabihf \
        binutils-aarch64-linux-gnu \
        bison \
        build-essential \
        clang \
        cpio \
        flex \
        gcc-arm-linux-gnueabihf \
        gcc-aarch64-linux-gnu \
        kmod \
        libelf-dev \
        libncurses5-dev \
        libssl-dev \
        lld \
        openssl \
        python3 \
        sudo && \
    rm -rf /var/lib/apt/lists/*

# Build as an unprivileged user instead of root.
RUN useradd --create-home --shell /bin/bash builder && \
    echo "builder ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/builder && \
    chmod 0440 /etc/sudoers.d/builder

USER builder
WORKDIR /home/builder

COPY --chown=builder:builder build_module.sh /usr/local/bin/build_module.sh
RUN chmod +x /usr/local/bin/build_module.sh

ENTRYPOINT ["/usr/local/bin/build_module.sh"]

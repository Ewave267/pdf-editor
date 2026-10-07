#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Provision a Rocky Linux 9 build machine; requires root.
set -Eeuo pipefail
source /etc/os-release
if [[ "$ID" != rocky || "${VERSION_ID%%.*}" != 9 ]]; then
    echo 'This build setup requires Rocky Linux 9.' >&2
    exit 1
fi
if (( EUID != 0 )); then
    echo 'Run this setup as root; run the build itself as a regular user.' >&2
    exit 1
fi
dnf -y install epel-release dnf-plugins-core
dnf config-manager --set-enabled crb
dnf -y install gcc gcc-c++ cmake ninja-build git ca-certificates \
    python3.12 python3.12-pip bubblewrap squashfs-tools binutils \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg qt6-qtimageformats \
    liberation-sans-fonts tar xz gzip which findutils \
    meson libcap-devel libselinux-devel gtk3 cpio shadow-utils util-linux
ln -sf /usr/bin/python3.12 /usr/local/bin/python3
if ! command -v ninja >/dev/null; then
    ln -s /usr/bin/ninja-build /usr/local/bin/ninja
fi

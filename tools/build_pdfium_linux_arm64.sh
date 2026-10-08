#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Cross-build only PDFium using Chromium's pinned x86_64 Linux compiler.
# XML tests must pass on the native ARM runner before this build is cached.
set -Eeuo pipefail
[[ $(uname -m) == x86_64 ]] || { echo 'Requires an x86_64 Rocky 9 builder' >&2; exit 1; }
sysroot="$PWD/.deps/arm64-sysroot"
mkdir -p "$sysroot/etc/pki/rpm-gpg"
cp /etc/pki/rpm-gpg/* "$sysroot/etc/pki/rpm-gpg/"
dnf -y --forcearch=aarch64 --installroot="$sysroot" --releasever=9 \
    --setopt=reposdir=/etc/yum.repos.d --setopt=module_platform_id=platform:el9 \
    --setopt=install_weak_deps=False --setopt=tsflags=noscripts \
    install glibc-devel libstdc++-devel
useradd --create-home native-builder
chown -R native-builder:native-builder "$PWD"
runuser -u native-builder -- env HOME=/home/native-builder \
    python3 tools/build_pdfium.py --jobs 4 --target-cpu arm64 \
    --sysroot "$sysroot" --defer-xml-tests

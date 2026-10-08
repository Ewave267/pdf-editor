#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Cross-build only PDFium using Chromium's pinned x86_64 Linux compiler.
# XML tests must pass on the native ARM runner before this build is cached.
set -Eeuo pipefail
[[ $(uname -m) == x86_64 ]] || { echo 'Requires an x86_64 Rocky 9 builder' >&2; exit 1; }
sysroot="$PWD/.deps/arm64-sysroot"
mkdir -p "$sysroot/etc/pki/rpm-gpg"
cp /etc/pki/rpm-gpg/* "$sysroot/etc/pki/rpm-gpg/"
for variables in /etc/dnf/vars /etc/yum/vars; do
    if [[ -d "$variables" ]]; then
        mkdir -p "$sysroot${variables%/*}"
        cp -a "$variables" "$sysroot${variables%/*}/"
    fi
done
dnf -y --forcearch=aarch64 --installroot="$sysroot" --releasever=9 \
    --setopt=reposdir=/etc/yum.repos.d --setopt=module_platform_id=platform:el9 \
    --setopt=install_weak_deps=False --setopt=tsflags=noscripts \
    install gcc gcc-c++ glibc-devel libstdc++-devel
# Clang discovers the target GCC installation through these files. Catch an
# incomplete sysroot before the lengthy PDFium/V8 compilation reaches its link.
compiler_dirs=("$sysroot"/usr/lib/gcc/aarch64-redhat-linux/*)
if (( ${#compiler_dirs[@]} != 1 )); then
    echo 'Cannot locate the ARM GCC startup/runtime directory.' >&2
    exit 1
fi
for file in crtbeginS.o crtendS.o libgcc.a libgcc_s.so; do
    [[ -e "${compiler_dirs[0]}/$file" ]] || {
        echo "ARM sysroot is missing $file" >&2
        exit 1
    }
done
useradd --create-home native-builder
chown -R native-builder:native-builder "$PWD"
runuser -u native-builder -- env HOME=/home/native-builder \
    python3 tools/build_pdfium.py --jobs 4 --target-cpu arm64 \
    --sysroot "$sysroot" --defer-xml-tests

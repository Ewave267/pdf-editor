# Build environment only: end-user artifacts contain no Docker runtime.
# SPDX-License-Identifier: GPL-3.0-only
FROM rockylinux:9
RUN dnf -y install epel-release dnf-plugins-core && \
    dnf config-manager --set-enabled crb && \
    dnf -y install gcc gcc-c++ cmake ninja-build git ca-certificates \
      python3.12 python3.12-pip bubblewrap squashfs-tools binutils \
      qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg qt6-qtimageformats \
      liberation-sans-fonts tar xz gzip which findutils && \
    ln -s /usr/bin/python3.12 /usr/local/bin/python3 && \
    if ! command -v ninja >/dev/null; then ln -s /usr/bin/ninja-build /usr/local/bin/ninja; fi && \
    dnf clean all
RUN dnf -y install meson libcap-devel libselinux-devel gtk3 cpio && dnf clean all
WORKDIR /source

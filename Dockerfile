# syntax=docker/dockerfile:1.7
# SPDX-License-Identifier: GPL-3.0-only
ARG UBUNTU_IMAGE=ubuntu:24.04@sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55
FROM ${UBUNTU_IMAGE} AS build-base
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential bubblewrap ca-certificates cmake curl git ninja-build python3 python3-venv xz-utils \
    pkg-config qt6-base-dev qt6-declarative-dev && rm -rf /var/lib/apt/lists/*

FROM build-base AS pdfium
ARG BUILD_JOBS=4
WORKDIR /source
COPY LICENSE ./
COPY tools/build_pdfium.py tools/build_pdfium.py
COPY third_party/pdfium/ third_party/pdfium/
RUN --mount=type=cache,id=pdf-editor-native-amd64,target=/source/.deps,sharing=locked \
    python3 tools/build_pdfium.py --jobs "${BUILD_JOBS}" && \
    cp -a .deps/pdfium-patched /pdfium && \
    tar --exclude=.git --exclude=out --exclude=.cipd --exclude=.cipd-bin \
      --exclude=.cipd_bin --exclude=.cipd-cache --exclude=.venv --exclude=__pycache__ \
      -czf /pdfium/corresponding-source.tar.gz \
      LICENSE tools third_party .deps/pdfium-source .deps/depot_tools .deps/pdfium-build

FROM build-base AS builder
ARG BUILD_JOBS=4
WORKDIR /source
COPY --from=pdfium /pdfium/ /pdfium/
COPY CMakeLists.txt LICENSE README.md ./
COPY cmake/ cmake/
COPY src/ src/
COPY qml/ qml/
COPY packaging/ packaging/
COPY docs/ docs/
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DPDF_EDITOR_BUILD_VIEWER=ON -DBUILD_TESTING=OFF -DPDFium_DIR=/pdfium && \
    cmake --build /build --parallel "${BUILD_JOBS}" && \
    cmake --install /build --prefix /opt/pdf-editor

FROM ${UBUNTU_IMAGE} AS runtime
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    bubblewrap ca-certificates curl fonts-liberation \
    libqt6gui6 libqt6qml6 libqt6quick6 libqt6quickcontrols2-6 qt6-qpa-plugins \
    qml6-module-qtquick qml6-module-qtquick-controls qml6-module-qtquick-dialogs \
    qml6-module-qtquick-layouts qml6-module-qtquick-templates \
    qml6-module-qtquick-window qml6-module-qtqml-workerscript \
    qml6-module-qt-labs-folderlistmodel \
    novnc websockify openbox tini x11vnc xauth x11-utils xvfb && \
    rm -rf /var/lib/apt/lists/* && \
    mkdir -p /documents /home/pdfeditor && chmod 1777 /documents /home/pdfeditor
COPY --from=builder /opt/pdf-editor/ /opt/pdf-editor/
COPY packaging/docker/start.sh packaging/docker/healthcheck.sh /usr/local/bin/
COPY packaging/docker/host_home.py /usr/local/lib/pdf-editor/host_home.py
COPY packaging/docker/index.html /usr/share/novnc/index.html
RUN chmod 755 /usr/local/bin/start.sh /usr/local/bin/healthcheck.sh
ENV HOME=/home/pdfeditor DISPLAY=:99 QT_QUICK_BACKEND=software \
    QT_QUICK_CONTROLS_STYLE=Basic PATH=/opt/pdf-editor/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
USER 1000:1000
WORKDIR /documents
EXPOSE 8080
HEALTHCHECK --interval=15s --timeout=5s --start-period=30s --retries=3 \
    CMD ["/usr/local/bin/healthcheck.sh"]
ENTRYPOINT ["/usr/bin/tini", "-g", "--", "/usr/local/bin/start.sh"]

# Test tools and fixtures are only in this separately built verification target.
FROM builder AS test-builder
COPY tests/ tests/
COPY tools/ tools/
RUN python3 tools/generate_probe_fixtures.py && \
    cmake -S . -B /test-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DPDF_EDITOR_BUILD_VIEWER=ON -DBUILD_TESTING=ON -DPDFium_DIR=/pdfium && \
    cmake --build /test-build --parallel 4 \
      --target viewer-tests viewer-safety-tests sandbox-policy-worker

FROM runtime AS verification
COPY --from=test-builder /usr/lib/x86_64-linux-gnu/libQt6Test.so.6* /usr/lib/x86_64-linux-gnu/
COPY --from=test-builder /test-build/viewer-tests /test-build/viewer-safety-tests \
    /test-build/sandbox-policy-worker /opt/pdf-editor/bin/
COPY --from=test-builder /source/tests/ /source/tests/
COPY --from=test-builder /source/tools/ /source/tools/
COPY --from=pdfium /pdfium/ /pdfium/
COPY packaging/docker/verify.sh /usr/local/bin/verify.sh
ENTRYPOINT ["/usr/bin/tini", "-g", "--", "/bin/bash", "/usr/local/bin/verify.sh"]

FROM scratch AS source-export
COPY --from=pdfium /pdfium/corresponding-source.tar.gz /native-source.tar.gz
COPY . /application/

FROM runtime AS final
LABEL org.opencontainers.image.title="PDF Form Editor" \
      org.opencontainers.image.version="0.1.0" \
      org.opencontainers.image.licenses="GPL-3.0-only"

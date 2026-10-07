#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run inside the RHEL 9-compatible CI build image, as the checkout's owner.
set -Eeuo pipefail
mkdir -p -- "$HOME"
python3 - <<'PY'
import platform, sys
assert platform.libc_ver() == ('glibc', '2.34'), platform.libc_ver()
assert sys.version_info >= (3, 12)
PY
python3 tools/build_bubblewrap.py
export PATH="$PWD/.deps/bubblewrap-build:$PATH"
if [[ ! -f .deps/pdfium-patched/PDFiumConfig.cmake ]]; then
    python3 tools/build_pdfium.py --jobs 4
fi
cmake -S . -B build-native -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DPDF_EDITOR_BUILD_VIEWER=ON -DBUILD_TESTING=OFF \
    -DPDF_EDITOR_BUILD_NATIVE_SMOKE=ON -DPDFium_DIR="$PWD/.deps/pdfium-patched"
cmake --build build-native --parallel 4
python3 tools/fetch_native_inputs.py --appimage --portable-libgcc
fonts=$(python3 - <<'PY'
from pathlib import Path
import subprocess
paths = subprocess.check_output(['rpm', '-ql', 'liberation-sans-fonts'], text=True).splitlines()
print(next(Path(path).parent for path in paths if path.endswith('.ttf')))
PY
)
python3 tools/package_native.py --build-dir build-native \
    --qt-runtime /usr/lib64/qt6 --font-dir "$fonts" \
    --appimage-runtime .deps/release-inputs/appimage-runtime \
    --appimage-license .deps/release-inputs/APPIMAGE-LICENSE \
    --bwrap-notices .deps/bubblewrap-build/notices \
    --libgcc-runtime .deps/release-inputs/libgcc-runtime/lib64/libgcc_s.so.1 \
    --libgcc-notices .deps/release-inputs/libgcc-notices

#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
pdf-form-editor --version
/opt/pdf-editor/bin/viewer-tests
/opt/pdf-editor/bin/viewer-safety-tests
python3 /source/tests/test_worker_safety.py \
    /opt/pdf-editor/bin/pdf-render-worker /opt/pdf-editor/bin/sandbox-policy-worker /pdfium

#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -euo pipefail
export XAUTHORITY=/tmp/pdf-editor-runtime/Xauthority
read -r gui_pid < /tmp/pdf-editor-runtime/gui.pid
kill -0 "$gui_pid"
xdpyinfo >/dev/null 2>&1
curl --fail --silent --max-time 3 http://127.0.0.1:8080/ >/dev/null

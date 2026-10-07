#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -Eeuo pipefail
umask 077
export XDG_RUNTIME_DIR=/tmp/pdf-editor-runtime
mkdir -p "$XDG_RUNTIME_DIR" "$HOME/.config" "$HOME/.cache" /tmp/.X11-unix
chmod 1777 /tmp/.X11-unix
chmod 700 "$XDG_RUNTIME_DIR"
export XAUTHORITY="$XDG_RUNTIME_DIR/Xauthority"

if [[ -d "$HOME/host_fs/etc" ]]; then
    export PDF_EDITOR_INITIAL_FOLDER
    PDF_EDITOR_INITIAL_FOLDER=$(python3 /usr/local/lib/pdf-editor/host_home.py "$HOME/host_fs") || exit 78
    printf 'Opening files in host home: %s\n' "$PDF_EDITOR_INITIAL_FOLDER"
fi

# Fail before exposing a browser session if the PDF sandbox cannot start.
if ! bwrap --unshare-all --die-with-parent --new-session --cap-drop ALL \
    --ro-bind /usr /usr --symlink usr/lib /lib --symlink usr/lib64 /lib64 \
    --dir /proc --dir /dev --ro-bind /dev/null /dev/null \
    --ro-bind /dev/zero /dev/zero --ro-bind /dev/urandom /dev/urandom \
    --ro-bind /dev/random /dev/random --tmpfs /tmp -- /usr/bin/true; then
    echo 'PDF sandbox unavailable: Docker/host policy blocks Bubblewrap namespaces.' >&2
    echo 'See docs/DOCKER-DEPLOYMENT.md. There is no unsandboxed fallback.' >&2
    exit 78
fi
if [[ ! -r /documents || ! -w /documents ]]; then
    echo '/documents must be readable and writable by the configured container UID/GID.' >&2
    exit 78
fi

children=()
cleanup() {
    trap - EXIT INT TERM
    if ((${#children[@]})); then
        kill "${children[@]}" 2>/dev/null || true
        # Bound shutdown even if a display or network service ignores TERM.
        sleep 1
        kill -KILL "${children[@]}" 2>/dev/null || true
        wait "${children[@]}" 2>/dev/null || true
    fi
}
trap cleanup EXIT
trap 'exit 0' INT TERM

touch "$XAUTHORITY"
xauth -f "$XAUTHORITY" add "$DISPLAY" . "$(mcookie)"
Xvfb "$DISPLAY" -screen 0 "${PDF_EDITOR_SCREEN:-1600x1000x24}" \
    -nolisten tcp -auth "$XAUTHORITY" &
children+=("$!")
for attempt in {1..50}; do
    xdpyinfo >/dev/null 2>&1 && break
    kill -0 "${children[0]}" || exit 1
    sleep 0.1
done
xdpyinfo >/dev/null
openbox & children+=("$!")
x11vnc -display "$DISPLAY" -auth "$XAUTHORITY" -localhost -rfbport 5900 \
    -forever -shared -nopw -noxdamage & children+=("$!")
websockify --web /usr/share/novnc 8080 localhost:5900 & children+=("$!")
pdf-form-editor "$@" & gui_pid=$!
children+=("$gui_pid")
printf '%s\n' "$gui_pid" > "$XDG_RUNTIME_DIR/gui.pid"

# Closing the GUI ends the session. Losing another service is an error.
finished=''
status=0
wait -n -p finished "${children[@]}" || status=$?
if [[ "$finished" != "$gui_pid" && "$status" == 0 ]]; then status=1; fi
exit "$status"

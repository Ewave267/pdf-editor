#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
set -Eeuo pipefail

usage() {
    cat <<'HELP'
Usage: ./docker.sh [start|stop|rebuild|build|status|logs|url] [--native-source] [--jobs 1-8]

  start     Start the existing container, or create it with Compose (default)
  stop      Stop the container, keeping saved host files
  rebuild   Build the image, then recreate and start the container
  build     Build the image without interrupting the running application
  status    Show container state and the browser link
  logs      Show the last 100 log lines
  url       Print the browser link

Builds reuse a matching .deps/pdfium-patched package when available. Otherwise
Docker builds pinned PDFium/V8 from source, which takes substantial time.
--native-source forces the source build; --jobs controls build parallelism.
Rebuild replaces the desktop session: save your PDF edits first.
The Compose host-root mount and its security configuration apply on creation.
HELP
}
fail() { printf 'Error: %s\n' "$*" >&2; exit 1; }
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$root"
action=start
if (($#)) && [[ "$1" != --* ]]; then action=$1; shift; fi
jobs=4
native_source=false
while (($#)); do
    case "$1" in
        --native-source) native_source=true ;;
        --jobs) (($# >= 2)) || fail '--jobs needs a number'; jobs=$2; shift ;;
        -h|--help) usage; exit 0 ;;
        *) fail "Unknown option: $1 (see --help)" ;;
    esac
    shift
done
[[ "$action" != -h ]] || { usage; exit 0; }
case "$action" in start|stop|rebuild|build|status|logs|url) ;; *) fail "Unknown command: $action (see --help)" ;; esac
[[ "$jobs" =~ ^[1-8]$ ]] || fail '--jobs must be between 1 and 8'
if "$native_source" && [[ "$action" != build && "$action" != rebuild ]]; then
    fail '--native-source is only valid with build or rebuild'
fi
command -v docker >/dev/null || fail 'Install Docker and its Compose plugin first.'
docker info >/dev/null 2>&1 || fail 'Cannot reach Docker. Start the daemon and check your Docker access.'
docker compose version >/dev/null 2>&1 || fail 'The Docker Compose plugin is required.'
compose=(docker compose --project-name pdf-editor --file "$root/compose.yaml")
container=pdf-editor
context=''
cleanup() { [[ -z "$context" ]] || rm -rf -- "$context"; }
trap cleanup EXIT

exists() { docker container inspect "$container" >/dev/null 2>&1; }
print_link() {
    local binding='' port=''
    if exists; then
        binding=$(docker port "$container" 8080/tcp 2>/dev/null || true)
        binding=${binding%%$'\n'*}
        port=${binding##*:}
    fi
    if [[ -z "$port" ]]; then
        port=$("${compose[@]}" config | awk '/^[[:space:]-]*published:/ {sub(/^[[:space:]-]*published:[[:space:]]*/, ""); gsub(/"/, ""); print; exit}')
    fi
    [[ "$port" =~ ^[0-9]+$ ]] || fail 'Cannot determine the configured browser port.'
    printf '\nOpen in Firefox:\n  http://localhost:%s/vnc.html?autoconnect=true&resize=scale\n' "$port"
}
wait_healthy() {
    local state health
    for ((attempt=0; attempt<60; attempt++)); do
        state=$(docker inspect "$container" --format '{{.State.Status}}')
        health=$(docker inspect "$container" --format '{{if .State.Health}}{{.State.Health.Status}}{{else}}none{{end}}')
        if [[ "$state" == running && ( "$health" == healthy || "$health" == none ) ]]; then return; fi
        if [[ "$state" != running || "$health" == unhealthy ]]; then break; fi
        sleep 1
    done
    docker logs --tail 50 "$container" >&2 || true
    fail 'The container did not become healthy. See the startup error above.'
}
prepare_documents() {
    local directory
    directory=$("${compose[@]}" config | awk '
        $1 == "source:" {sub(/^[[:space:]]*source:[[:space:]]*/, ""); source=$0; sub(/^"/, "", source); sub(/"$/, "", source)}
        $1 == "target:" && $2 == "/documents" {print source; exit}')
    [[ -n "$directory" ]] || fail 'Cannot determine the documents mount.'
    mkdir -p -- "$directory"
}
manifest_value() {
    sed -n 's/^[[:space:]]*"'"$1"'"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' \
        "$root/.deps/pdfium-patched/build-info.json"
}
cached_native_matches() {
    local key path expected actual
    [[ -f .deps/pdfium-patched/build-info.json && -f .deps/pdfium-patched/PDFiumConfig.cmake ]] || return 1
    command -v sha256sum >/dev/null || return 1
    [[ $(manifest_value version) == $(sed -n 's/^VERSION = "\([^"]*\)"/\1/p' tools/build_pdfium.py) ]] || return 1
    for key in patch_sha256 radio_font_patch_sha256 v8_tls_patch_sha256 library_sha256; do
        case "$key" in
            patch_sha256) path=third_party/pdfium/patches/0001-xfa-persistence.patch ;;
            radio_font_patch_sha256) path=third_party/pdfium/patches/0003-xfa-radio-and-font-fallback.patch ;;
            v8_tls_patch_sha256) path=third_party/pdfium/patches/0002-v8-shared-library-tls.patch ;;
            library_sha256) path=.deps/pdfium-patched/lib/libpdfium.so ;;
        esac
        [[ -f "$path" ]] || return 1
        expected=$(manifest_value "$key")
        actual=$(sha256sum "$path"); actual=${actual%% *}
        [[ -n "$expected" && "$expected" == "$actual" ]] || return 1
    done
}
build_image() {
    local image
    local -a build_args=(--platform linux/amd64 --build-arg "BUILD_JOBS=$jobs")
    image=$("${compose[@]}" config --images)
    [[ -n "$image" && "$image" != *$'\n'* && "$image" != *@* ]] || fail 'Rebuilding requires one image tag, not an immutable digest.'
    if ! "$native_source" && cached_native_matches; then
        context=$(mktemp -d /tmp/pdf-editor-docker-native.XXXXXX)
        cp -a -- "$root/.deps/pdfium-patched" "$context/pdfium"
        build_args+=(--build-context "pdfium=$context")
        printf 'Reusing patched PDFium; only the application/runtime layers need building.\n'
    else
        printf 'Building pinned PDFium/V8 from source; the first build can take a long time.\n'
    fi
    docker build "${build_args[@]}" --tag "$image" "$root"
}

case "$action" in
    start)
        if exists; then
            if [[ $(docker inspect "$container" --format '{{.State.Status}}') != running ]]; then
                docker start "$container"
            fi
        else
            prepare_documents
            "${compose[@]}" up -d --wait --wait-timeout 60
        fi
        wait_healthy
        printf 'PDF editor is running.\n'
        print_link
        ;;
    stop)
        if exists; then docker stop "$container"; else printf 'PDF editor is already stopped (no container).\n'; fi
        printf 'Start again with: %q start\n' "$root/docker.sh"
        print_link
        ;;
    rebuild)
        printf 'Rebuild will replace the desktop session. Save any unsaved edits before running it.\n'
        build_image
        prepare_documents
        "${compose[@]}" up -d --force-recreate --wait --wait-timeout 60
        wait_healthy
        print_link
        ;;
    build) build_image; printf 'Image built. Run ./docker.sh rebuild to apply it.\n'; print_link ;;
    status)
        if exists; then docker inspect "$container" --format '{{.Name}}: {{.State.Status}}{{if .State.Health}} ({{.State.Health.Status}}){{end}}';
        else printf 'PDF editor has not been created.\n'; fi
        print_link
        ;;
    logs) if exists; then docker logs --tail 100 "$container"; else fail 'No pdf-editor container exists.'; fi ;;
    url) print_link ;;
esac

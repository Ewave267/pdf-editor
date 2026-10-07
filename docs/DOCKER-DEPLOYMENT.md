# Docker deployment with browser access

Date: 2026-10-07

Status: Implemented and validated locally. Image repository:
`ewave/pdf-form-editor`. No image has been published to Docker Hub yet.

## Start, stop and rebuild

From this checkout:

```sh
./docker.sh start      # Also the default when no command is supplied
./docker.sh stop
./docker.sh rebuild    # Save native PDF edits first: replaces the desktop session
./docker.sh status
./docker.sh url
./docker.sh logs
```

The helper changes to the checkout directory automatically and prints the
scaling-enabled browser link using the actual published port. `start` preserves
an existing container configuration; on first use it creates the container from
Compose. `rebuild` builds the configured image and force-recreates the container
with the current Compose configuration. Host files persist; unsaved edits do not.
No implicit sudo or Docker Hub publication is performed. Docker access and its
Compose plugin are required. If the image has not been published or built locally,
run `./docker.sh build` first.

Builds reuse `.deps/pdfium-patched` when its version and patch/library hashes
match. This copies the package to a temporary named build context and removes the
context afterward. Without a matching package, Docker uses the full pinned native
source recipe/cache. Force that path with `./docker.sh rebuild --native-source`.
Use `--jobs 2` to reduce build parallelism (1–8). `./docker.sh build` builds the
image without replacing the current desktop session. Reusing a native package is
a local development shortcut, not the corresponding-source publication gate.

## Run on a new machine

After a versioned image has been published, install Docker with Compose and copy
`compose.yaml`, `.env.docker.example` and `packaging/docker/` from this repository.
No application compiler, Qt SDK, Node.js or PDFium build is needed on the host.
The security profile and its notices must remain beside the Compose file.

```sh
cp .env.docker.example .env
mkdir -p documents
id -u
id -g
# Set PDF_EDITOR_UID and PDF_EDITOR_GID in .env to these numbers.
docker compose pull
docker compose up -d --wait
```

Open **http://localhost:8080**. This is the existing Qt application streamed through
noVNC, with one desktop session per container. The landing page uses local
scaling so the virtual desktop fits the browser viewport automatically. On an
existing image, open `http://localhost:8080/vnc.html?autoconnect=true&resize=scale`.
The application starts fullscreen automatically, filling the virtual desktop.
Use the noVNC fullscreen button or Firefox F11 to also fill your physical screen. Scaling preserves aspect
ratio, so differently shaped browser windows can leave margins. The host's `/` is mounted at
`/home/pdfeditor/host_fs` (`~/host_fs` inside Docker). Open, Save As and image
pickers initially show the host user's home beneath that mount, for example
`/home/pdfeditor/host_fs/home/ewave`. The startup helper reads host `/etc/passwd`
using the configured container UID; uniquely owned `/home` directories provide a
fallback for directory-service accounts. No `HOST_HOME` variable is required.
Set the existing `PDF_EDITOR_UID/GID` to your host user's IDs when they differ
from the default 1000. An unidentifiable/inaccessible home fails startup clearly.

The root mount is writable under that user's ordinary host permissions. The GUI
can therefore access host files beyond a dedicated PDF folder. `/documents` remains
an additional convenient mount of `PDF_EDITOR_DOCUMENTS` (default `./documents`).
On SELinux hosts Compose uses container-scoped `label=disable` to permit the
requested host-root access; **never use `:z` or `:Z` on the root mount**, since that
would relabel host files. No host policy or host labels are changed. The application
remains non-root with all capabilities dropped, and PDF workers remain isolated.
This mode requires a local Linux Docker daemon; remote-daemon roots belong to the
remote machine, and Docker Desktop exposes its VM rather than the client's root.

The HTTP endpoint is bound to localhost, and VNC is bound to container loopback.
The browser session has no password and is intended for local use. Remote/shared
access requires a separate authentication/TLS design. noVNC keyboard, mouse,
scrolling and clipboard controls operate the virtual desktop. Use the noVNC
clipboard panel to paste text into the app; native copy-back and direct browser
system-clipboard integration were inconsistent in repeated local checks.

The container has the fixed name `pdf-editor`. After it has been created, use
`docker start pdf-editor` to start it and `docker stop pdf-editor` to stop it
from any directory. These commands keep its existing configuration; changes to
Compose require recreating the container. One container with this name can exist
per Docker daemon.

Check startup with `docker compose ps` and `docker compose logs`. Closing the Qt
window ends the session; start again with `docker compose up -d`. Stop with
`docker compose down`. PDFs saved through either host mount remain. Temporary clipboard,
settings and desktop state are not persisted. This deployment does not autosave
unsaved native form edits when stopped.

For updates, change `PDF_EDITOR_IMAGE` in `.env` to a published version or immutable
Docker Hub digest, then pull and recreate the service. The initial target is
**Linux amd64**. Linux hosts with working unprivileged user namespaces are required.
Docker Desktop and other architectures have not been validated.

## Build locally

```sh
docker build -t ewave/pdf-form-editor:0.1.0 .
mkdir -p documents
docker compose up -d --wait
```

The first source build compiles pinned PDFium/V8 and runs upstream XML tests.
Build arguments `BUILD_JOBS` (1–8, default 4) and `UBUNTU_IMAGE` are available.
The default Ubuntu 24.04 base is pinned by digest. Dependencies are built before
application source is copied, so UI/application changes reuse the expensive
native build layer. A locked BuildKit cache retains source/toolchain/build data
for incremental dependency changes. A multi-stage build excludes compilers,
SDKs, source trees and test tools from the runtime image.

The runtime includes Qt/QML, Liberation Sans, Bubblewrap, Xvfb, Openbox, x11vnc,
noVNC, websockify and Tini. Python is a runtime dependency of websockify; it is not
required on the host. Render-worker libraries resolve from the installed image.

For the local validation, the existing exact patched PDFium package was reused
while the application was compiled inside Ubuntu. A named `pdfium` build context
can replace the native stage:

```sh
mkdir -p /tmp/pdf-editor-native-context
cp -a .deps/pdfium-patched /tmp/pdf-editor-native-context/pdfium
docker build --build-context pdfium=/tmp/pdf-editor-native-context \
  -t ewave/pdf-form-editor:0.1.0 .
```

Use only a compatible Linux amd64 dependency package produced by the pinned
recipe. This shortcut validates the runtime/application build, not a fresh native
source build inside Docker. The full native Docker build has not been run locally.

## Sandbox configuration

Docker's default seccomp policy blocked the required namespace creation in the
local experiment. Compose uses the reviewed, Moby-default-derived profile in
`packaging/docker/seccomp.json`; the user explicitly approved the six additional
namespace/mount calls. See [runtime policy](../packaging/docker/README.md) for the
exact rule, upstream revision, retained license and generation command.

The container is non-root, drops all host capabilities and retains Docker's
masked paths. Each worker still creates its own isolated namespaces and installs
its stricter syscall/allocation policy before parsing. Its filesystem now has an
empty `/proc` and four essential read-only device files, avoiding blocked procfs
and devpts mounts. Existing GUI/form/save/safety gates pass with this change.

Startup tests Bubblewrap before opening the browser session. If a host's kernel,
AppArmor or other policy rejects it, the container exits with an actionable error
and code 78. No privileged/unconfined default or unsandboxed fallback is supplied.
Only the Fedora Docker host has been validated; Ubuntu image compatibility does
not establish compatibility with every host's security policy.

## Repeatable validation

A separate image target contains verification tools; they are not shipped in the
runtime image. Build and run it as follows:

```sh
docker build --target verification -t ewave/pdf-form-editor:verification .
docker run --rm --user 1000:1000 --cap-drop ALL \
  --security-opt no-new-privileges:true \
  --security-opt seccomp=packaging/docker/seccomp.json --read-only \
  --tmpfs /tmp:rw,nosuid,nodev,size=256m,mode=1777 \
  --tmpfs /home/pdfeditor:rw,nosuid,nodev,size=64m,mode=1777 \
  ewave/pdf-form-editor:verification
```

For a real browser walkthrough, start the runtime with the default 1600×1000
virtual display and a **dedicated temporary documents folder**. Then run:

```sh
npm ci --prefix tests/browser --ignore-scripts
npm ci --prefix tests/reader --ignore-scripts
tests/browser/node_modules/.bin/playwright install chromium
node tests/browser/check.mjs http://localhost:8080 /path/to/temporary/documents
```

The browser harness copies original synthetic AcroForm/XFA fixtures into that
folder, drives native file dialogs and controls through noVNC, saves/reopens,
checks clipboard-panel paste, independently verifies exact saved values with
PDF.js, and stores screenshots. Browser-test dependencies are not needed to use
the deployment. The normal existing host gate remains `ctest --test-dir
build-viewer --output-on-failure` with the combined viewer/probe configuration.

Locally validated on Docker 29.4.2 / Compose 5.1.2, Fedora Linux x86-64 host,
Ubuntu 24.04 container, Qt 6.4.2:

- Runtime image builds; installed GUI starts in a browser with no compiler/SDK.
- Container verification: 20 viewer results, 13 safety results and 7 native
  kernel/hostile-document tests pass.
- Host suite: 6/6 CTest entries pass, including independent-reader XFA round trips.
- Browser checks and mounted-file persistence results are recorded in PROGRESS.md.

A clean second-machine pull/run, additional host policies, other architectures and
remote CI remain unverified. Existing MVP limitations remain: full XFA with added
text/signatures is refused during save, and broader form compatibility is incomplete.

## Docker Hub build and publication

The manual **Docker Preview** workflow requires a dedicated self-hosted Linux x86-64
runner labeled `pdf-editor-sandbox`, with Docker/Compose, Python 3, and Chromium
system dependencies installed. Use a host on which the documented sandbox
verification succeeds; Fedora was validated locally. Hosted Ubuntu runners have
additional AppArmor restrictions and are not assumed compatible. Do not weaken
host policy to bypass a failed check.

The workflow builds the verification and runtime images,
exports its build cache, runs native and browser gates, and exports corresponding
source. The source-export target contains the application build context plus an
archive of exact patched PDFium/V8 sources, dependency checkouts, build tooling,
patches and notices. Generated build outputs and checkout metadata are excluded;
the pinned recipe remains available to reproduce them.

Add repository Actions secrets `DOCKERHUB_USERNAME=ewave` and `DOCKERHUB_TOKEN`
with permission to push `ewave/pdf-form-editor`. Run the workflow first with
`publish=false`. After the image, source artifact and checks are reviewed, run
with `publish=true` and an explicit version tag. The workflow logs in using
secrets and pushes only after validation/source export succeed. Credentials are
never committed or placed in deployment `.env` files.

Publication and corresponding-source export have not been performed locally.
The native-context shortcut lacks the native source archive, so it cannot be used
for the source-export/publication gate. Remote hosts with incompatible namespace
policies will fail validation before publication rather than weakening isolation.

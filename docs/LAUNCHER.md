# Standalone PDF Editor launcher

The launcher is a compiled Go executable that manages the existing Linux Docker
editor. Users need Docker Engine on Linux or Docker Desktop in Linux-container
mode on Windows/macOS, and a browser. Go, Python, Qt, a compiler, Compose and a
source checkout are not needed for normal use. Docker must already be running
and accessible to the user; the launcher does not install it or invoke sudo.

## Download and run

Developer builds are in `dist/launcher/`. The Launcher workflow produces ZIP
artifacts; reviewed archives can be attached to a GitHub release. No release or
upload has been performed by this implementation. Download the archive matching
your operating system and CPU, extract it, and run the executable. Examples:

```sh
# Linux x86-64:
chmod +x pdf-editor-linux-amd64
./pdf-editor-linux-amd64 start

# macOS Intel (experimental):
./pdf-editor-darwin-amd64 start
```

```powershell
# Windows x86-64 PowerShell (experimental):
.\pdf-editor-windows-amd64.exe start
```

With no command, the default is `start`. The launcher pulls
`ewave267/pdf-editor:0.1.0` only if it is not already available locally, creates a
container once, waits for Docker health, and prints a URL like:

`http://localhost:8080/vnc.html?autoconnect=true&resize=scale`

Add `--open` to open your default browser automatically. The editor starts
fullscreen and noVNC scales it to the browser. `--port 0` asks Docker to select an
available localhost port; the launcher prints the actual port. Use `--port 8081`
for a specific alternative. Existing containers retain their configuration on
start; creation flags apply only to a new container.

The versioned image must be published by the maintainer or built locally. A
missing image/registry-access failure is reported, rather than silently compiling
PDFium. This work does not verify that the Docker Hub tag has been published.

## Local files and sandbox

New launcher containers share your real home directory at `/documents`. The Open,
Save As and image dialogs start there without any environment-variable setup.
Use `--home /path/to/pdfs` to share a particular folder. Paths with spaces are
supported; paths containing commas are currently rejected. Windows and macOS
folders must be allowed by Docker Desktop's file-sharing configuration.

Linux and macOS use the launching user's UID/GID. Windows uses container UID/GID
1000:1000; Docker Desktop sharing permissions still need validation on each platform. Root
launching is rejected when creating containers. Remote/TCP Docker daemons are
rejected for creation/update because their filesystem is not the user's machine.

`--host-root` preserves the Linux-only whole-filesystem mode: `/` appears at
`/home/pdfeditor/host_fs`, and host user records determine the initial folder.
It cannot be combined with `--home`. The launcher keeps an existing Compose
container's root mount unchanged when starting it.

The executable embeds the reviewed seccomp profile and its GPL/Apache notices.
It extracts them under the user's cache directory before container creation;
the security profile must be supplied to Docker before the container starts.
Nothing needs to be copied manually. Containers run non-root, drop capabilities,
use a read-only image and private temporary runtime directories, and publish only
on localhost. On SELinux Docker hosts container labeling is disabled for the
requested host-file access, without relabeling the host. PDF workers retain their
own restrictive sandbox. Startup fails if Bubblewrap is blocked; the launcher
does not weaken host policy or provide an unsandboxed fallback.

## Lifecycle commands

```text
pdf-editor start
pdf-editor stop
pdf-editor status
pdf-editor url
pdf-editor logs
pdf-editor update
pdf-editor update --image ewave267/pdf-editor:NEW_VERSION
pdf-editor build --source /path/to/source-checkout --jobs 4
pdf-editor version
pdf-editor license
```

Replace `pdf-editor` with your downloaded executable name, or rename/install the
executable on PATH. `--name another-editor` allows a separate container.

`stop` keeps saved host PDFs and the container. Restart policy `unless-stopped`
automatically restarts a running container with Docker, but leaves an explicitly
stopped container stopped. Closing the app causes a fresh desktop session to
start. Docker must itself start at boot for automatic boot-time launch.

**Save edits before `update`.** It pulls the requested image and validates sharing
prerequisites before stopping/removing a launcher-owned container and creating its
replacement. Host files persist; unsaved edits, settings and temporary desktop
state do not. The existing shared folder and published port are retained unless
overridden. A failed image pull leaves the running container untouched. A later
creation/startup failure is reported; correct the error and run `start` again.

A Compose-owned container can be started, stopped and inspected, but is not
replaced by `update`; use the existing `./docker.sh rebuild` or a different
`--name` to create a launcher-owned container. Foreign containers are refused.

`build` is an explicit developer option: it invokes Docker on a supplied source
checkout and does not replace the running container. All Qt/PDFium compilation
happens inside Docker; the first source build can take substantial time and disk
space. BuildKit caches subsequent builds. This command does not automatically
use a host `.deps/pdfium-patched`; the existing `docker.sh build` development
shortcut remains available. Users pulling an image never run this build.

## Platform support

| Launcher target | Editor runtime status |
| --- | --- |
| Linux amd64 | Locally validated with Docker on Fedora |
| Windows amd64 | Cross-built; Docker Desktop runtime unvalidated |
| macOS amd64 | Cross-built; Docker Desktop runtime unvalidated |
| Linux/Windows/macOS arm64 | Cross-built; native ARM editor image not implemented |

The current PDFium recipe and editor image are Linux amd64. ARM launchers refuse
new creation/update unless `--allow-emulation` is explicitly supplied. This is an
experimental attempt using Docker's amd64 emulation, not a supported native ARM
release; Bubblewrap/V8 compatibility must be verified. No platform is declared
supported merely because its launcher compiles. Windows/macOS signing, packaging
trust prompts and full desktop/sandbox validation remain release work.

## Singularity and Apptainer

The launcher currently calls Docker only. Neither Singularity nor Apptainer is
installed in the validation environment, and this image has not been validated
with either runtime. They are not a drop-in replacement for `docker.sh` or the
compiled launcher.

[Apptainer supports importing Docker Hub images into SIF files](https://apptainer.org/docs/user/latest/docker_and_oci.html).
Image import alone does not establish application compatibility. A separate
backend needs writable temporary/home directories, host file binds, start/stop
lifecycle, and validation of the PDF worker's nested Bubblewrap namespaces.
Cluster policies may prohibit those namespaces; the worker must fail closed.

Networking also needs adaptation: the current websockify listener relies on
Docker publishing its port on localhost only. With a runtime sharing the host
network, the listener must explicitly bind localhost. Docker's seccomp policy
and Compose settings are not automatically applied by importing the image.
A plain `singularity run` or `apptainer run` is not a supported deployment yet.

## Build the launcher and release archives

Developers need Go 1.23+; Python 3 is only needed for the archive-building helper:

```sh
go test ./...
go vet ./...
go build -trimpath -o dist/launcher/pdf-editor ./cmd/pdf-editor
python3 tools/build_launcher.py
# Build only one target:
python3 tools/build_launcher.py --target linux-amd64
```

The helper disables CGO, builds six OS/CPU targets, bundles the application, Moby
profile and Go notices, creates a matching launcher-source ZIP, and writes
`SHA256SUMS` for all ZIP archives. The source ZIP covers the launcher only, not the
separate editor image and its PDFium dependency. The normal
app's CMake build remains independent of Go. The launcher embeds the profile from
`packaging/docker/seccomp.json` directly so there is no duplicate policy to drift.
Retain notices and provide corresponding launcher/application/PDFium sources when
publishing binaries; see [release instructions](RELEASE.md).

# Native release candidates

The native application opens its own desktop window. End users do not need Go,
Docker, a browser, a compiler, or a Qt installation. Three platform releases are
planned, with two distribution formats on Linux:

| Platform | Download | Run | Current status |
| --- | --- | --- | --- |
| Windows x64 | `pdf-editor-VERSION-windows-x64.zip` | Extract, double-click `pdf-editor.exe` | Native worker and patched PDFium port required |
| Linux x86_64 | `pdf-editor-VERSION-linux-x86_64.tar.gz` | Extract, run `./pdf-editor` | Packaging implemented; host compatibility must be validated |
| Linux x86_64 | `pdf-editor-VERSION-linux-x86_64.AppImage` | Mark executable, run | Packaging implemented; host compatibility must be validated |
| macOS | ZIP containing `PDF Editor.app`, then a signed/notarized DMG | Open the app | Native worker and patched PDFium port required |

The previous Docker/Go implementation is preserved on `archive/docker-go`.
Native packaging development happens on `native-releases`. No release is
published automatically, and Windows/macOS artifacts must not contain the
foundation smoke executable or Docker launcher in place of the actual editor.

## Linux usage

Extract the tarball into any user-owned directory and run its top-level
`pdf-editor` executable script. Keep the complete extracted folder together.
Optional: `./pdf-editor /path/to/document.pdf`.

For the AppImage:

```sh
chmod +x pdf-editor-0.1.0-rc.1-linux-x86_64.AppImage
./pdf-editor-0.1.0-rc.1-linux-x86_64.AppImage
# If the host does not provide a usable FUSE mount:
./pdf-editor-0.1.0-rc.1-linux-x86_64.AppImage --appimage-extract-and-run
```

Qt libraries, QML modules, platform plugins, PDFium, Bubblewrap, and Liberation
fonts are bundled. The OS still supplies glibc, the kernel, and a desktop session.
Targets are RHEL 9 (glibc 2.34), Fedora, and Ubuntu 22.04/24.04 or newer, on
x86_64. This is the intended test matrix, not completed compatibility validation.
Build all native components on a RHEL 9-compatible host, including PDFium and
Bubblewrap. The packaging tool refuses a requirement above glibc 2.34 by default.
The precise glibc minimum is recorded in `release-info.json`; an AppImage cannot
remove this requirement. A local Fedora build is not a compatible binary for
older RHEL systems. The release builder should use the oldest supported build
environment, followed by tests on the intended RHEL versions.

Bubblewrap must be permitted to create unprivileged user namespaces. No root
installation or setuid Bubblewrap is distributed. A host that blocks namespaces
cannot run the PDF worker; no unsandboxed fallback is provided. Running the
AppImage without FUSE does not bypass the worker sandbox.

## Build Linux artifacts

Configure a Release viewer using the commands in README.md.
Reuse the existing patched PDFium package; packaging does not
recompile it. Build-time tools include Python 3, CMake, `ldd`, `readelf`, and an
RPM/DEB package database for dependency notices. Relocation validation requires
Python 3.12+. AppImage creation also needs
`mksquashfs` from squashfs-tools.

```sh
python3 tools/package_native.py \
  --build-dir build-release \
  --qt-runtime /usr/lib64/qt6 \
  --font-dir /usr/share/fonts/liberation-sans-fonts
```

Use the Qt runtime matching the viewer build. On Debian/Ubuntu, Qt is usually
under `/usr/lib/x86_64-linux-gnu/qt6`, and Liberation fonts under
`/usr/share/fonts/truetype/liberation`. For an extracted SDK, point `--qt-runtime`
to its directory containing `plugins/` and `qml/`.

To also produce the AppImage, provide the official runtime matching the hash
in `packaging/native/appimage-runtime.json`, and its upstream license:

```sh
python3 tools/package_native.py \
  --build-dir build-release \
  --qt-runtime /usr/lib64/qt6 \
  --font-dir /usr/share/fonts/liberation-sans-fonts \
  --appimage-runtime /path/to/runtime-x86_64 \
  --appimage-license /path/to/type2-runtime-LICENSE
```

For a Fedora-only local preview, pass `--version 0.1.0-preview.fedora42
--max-glibc 2.39`. This override does not establish RHEL 9 compatibility.
The manual `native-linux.yml` workflow requires a configured self-hosted
`pdf-editor-rhel9` builder with build tools, Qt 6.4+, Liberation fonts, Python 3.12+
and Node.js 24+ already installed. It performs the source build, safety tests,
packaging and relocation checks for both formats. It does not publish a release.

Outputs go to `dist/native/`, with `SHA256SUMS`. The tool checks the runtime hash,
collects shared-library dependencies and distro notices, verifies relocated
linkage, and records the build host and minimum glibc. It does not download or
install anything. The runtime is prepended to a SquashFS payload, following the
[type-2 AppImage format](https://docs.appimage.org/reference/architecture.html).

## Windows and macOS implementation gates

The current renderer is Linux-specific: Bubblewrap starts the worker, seccomp
restricts its syscalls, input snapshots use POSIX APIs, and the PDFium build
recipe explicitly targets Linux x64. Removing the CMake platform check would
not create a working native release.

Windows requires a native worker process sandbox, bounded resources and lifetime,
safe document-handle transfer, Windows-compatible input snapshots, and a pinned
Windows build of the patched XFA/V8-enabled PDFium. Package the viewer and worker
with `windeployqt`, including QML, plugins, Qt DLLs, PDFium and the compiler
runtime. Test the extracted ZIP on Windows without a development Qt installation.

macOS requires the equivalent native worker policy, input/library paths,
resource limits, and a pinned patched PDFium build for each supported CPU.
Package a real `.app` bundle with `macdeployqt`, including the worker and required
frameworks/plugins; sign all nested code and notarize the distributed artifact.
Intel and Apple Silicon must each be validated before offering a universal app.

These are implementation tasks, not packaging-only changes. No Windows/macOS
candidate is currently advertised as runnable. Their native build and UI checks
require Windows and macOS builders; neither is available in this workspace.

## Release verification and sources

Before publishing, test relocated artifacts with no host Qt/QML dependency,
open normal PDFs, edit AcroForm and XFA controls, add content where supported,
save/reopen twice, and rerun hostile-document isolation tests. Test the tarball
and AppImage independently, including paths containing spaces. Verify all
archive checksums and the minimum host OS on a clean desktop.

Keep GPL-3.0 application sources, patched PDFium/V8 corresponding source, Qt and
all bundled dependency notices/rebuild inputs with each exact binary release.
Collected license notices alone are not a corresponding-source distribution.
Signing credentials are provided through release secrets, never committed.

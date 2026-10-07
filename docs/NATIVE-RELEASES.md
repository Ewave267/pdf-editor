# Native release candidates

These packages run the Qt desktop editor directly, with bundled runtime
dependencies. End users do not need a compiler or a separate Qt installation.

## Build and download on GitHub

Push the repository, including `.github/workflows/native.yml`, to GitHub on
`native-releases`, `main` or `master`. Relevant code changes trigger **Native
release candidates** automatically. You can also select **Actions → Native
release candidates → Run workflow**, choose the branch and start it manually.
No self-hosted runner or signing secrets are required.

Wait for the jobs to finish, then open the workflow run and download its
**Artifacts**. Choose a `pdf-editor-*` artifact; `linux-validation-inputs`
is temporary tooling for the validation job. GitHub wraps each artifact in an extra ZIP; extract that first
to find the actual application archive and `SHA256SUMS`.

| GitHub artifact | Application package | Start |
| --- | --- | --- |
| `pdf-editor-linux-x86_64` | Portable `.tar.gz` and `.AppImage` | Extract tarball and run `./pdf-editor`, or run AppImage |
| `pdf-editor-windows-x64` | ZIP with EXE and DLLs | Extract completely, double-click `pdf-editor.exe` |
| `pdf-editor-macos-arm64` | ZIP with `PDF Editor.app` | Extract on an Apple Silicon Mac, open the app |
| `pdf-editor-macos-x64` | ZIP with `PDF Editor.app` | Extract on an Intel Mac, open the app |

Keep the full extracted application folder together. The current package
version is `0.1.0-rc.1`. These are test candidates, not an automatically published
GitHub Release. The first PDFium/V8 build is substantial; later runs cache the
patched PDFium library. Each job has a three-hour limit and separate diagnostics
on failure. Windows and macOS are new ports and require their first native CI
runs before their runtime behavior can be confirmed.

## Linux compatibility and usage

Linux binaries and bundled dependencies are built in a Rocky Linux 9 builder
with glibc 2.34 in a GitHub Actions build job. A separate Ubuntu job validates
the resulting native packages. Packaging rejects ELF
dependencies requiring a newer glibc and records the minimum in
`release-info.json`.

The bundle uses a checksum-pinned Rocky Linux 9.0 GCC 11 unwinder. Newer RHEL 9
unwinders depend on a `GLIBC_2.35` vendor backport despite reporting glibc 2.34;
the older runtime keeps the GCC ABI while avoiding that newer requirement.
Its vendor signature is checked during extraction and its notices/source URL
are included in the package.

The target is x86_64 RHEL 9, Fedora and Ubuntu 22.04/24.04 or newer with a
graphical desktop session. This baseline improves portability; it does not
replace testing on those desktops. Older glibc systems, ARM Linux and musl
systems such as Alpine are outside this candidate's target.

```sh
tar -xzf pdf-editor-0.1.0-rc.1-linux-x86_64.tar.gz
cd pdf-editor-0.1.0-rc.1-linux-x86_64
./pdf-editor
# Optional: ./pdf-editor /path/to/document.pdf
```

Alternatively:

```sh
chmod +x pdf-editor-0.1.0-rc.1-linux-x86_64.AppImage
./pdf-editor-0.1.0-rc.1-linux-x86_64.AppImage
# Use this if FUSE is unavailable:
./pdf-editor-0.1.0-rc.1-linux-x86_64.AppImage --appimage-extract-and-run
```

Qt libraries, QML modules, plugins, PDFium, Bubblewrap and Liberation fonts are
bundled. Bubblewrap is rebuilt from pinned source against glibc 2.34 because
RHEL 9's distro version lacks the bounded tmpfs option used by the worker. Its source pin and license
are included with the package.

The GUI selects its bundled fonts and includes the host font configuration when
available. The host supplies glibc, the kernel and the desktop session. Bubblewrap requires
permitted unprivileged user namespaces; SELinux/AppArmor or local
administrator policy can prevent them. The worker fails closed with no
unsandboxed fallback. FUSE extraction does not bypass this requirement.

Local Fedora preview archives require glibc 2.39 and are explicitly named
`preview.fedora42`; they are not the RHEL 9 candidates built by GitHub.

## Windows and macOS

Windows x64 builds use MSVC, Qt 6.8.3 and the Windows SDK required by pinned
PDFium. `windeployqt` collects Qt/QML/plugins; the redistributable MSVC CRT DLLs
are copied beside the EXE so no redistributable installer is needed. The PDF
worker runs through `pdf-sandbox.exe` in a capability-free AppContainer with
CPU, memory, lifetime and child-process limits. No administrator installation
is required. Keep its helper, worker and DLLs beside the editor EXE.

Mac builds target macOS 13 or newer, separately for Intel and Apple Silicon.
`macdeployqt` bundles the Qt frameworks, QML/plugins and PDFium worker. The worker
uses `/usr/bin/sandbox-exec` with a deny-default profile and resource limits;
no network, child-process or host file-write access is granted. The worker
verifies its sandbox before parsing a document. This uses Darwin sandbox SPI;
unsupported hosts fail closed rather than silently disabling isolation.
Native workers receive a minimal environment rather than the GUI's host environment.

Windows candidates are unsigned. Mac candidates are ad-hoc signed for native
execution, not Developer ID signed or notarized. Downloaded candidates may need
explicit approval through the operating system's security UI. Production signing
and notarization are future release work.

## What the workflow checks

Before uploading a package, the workflow launches the actual GUI offscreen
from a path containing spaces and tests its deployed dependencies. A separate
validation helper opens normal PDFs, adds text, edits AcroForm/XFA text,
checkboxes, radio buttons and dropdowns, saves and reopens twice, verifies the
source PDF stays unchanged, and checks malformed-document recovery. Pinned
PDF.js independently checks both saved form generations. The helper is removed
from Windows/Mac ZIPs and is not included in Linux archives.

Linux tarball and AppImage are transferred to the Ubuntu validation job and
extracted and checked separately. Checksums and the glibc baseline are checked. Windows/Mac
packaging also rejects direct worker execution outside its sandbox. Existing
Linux hostile-document/regression suites remain available via CTest.

Local validation built the glibc 2.34 candidates, passed all 69 PDFium XML tests,
and passed both relocated formats with independent PDF.js checks. GUI startup
with bundled libraries/fonts also passed on minimal Ubuntu 22.04 without Qt.
Worker tests ran on Fedora; Windows/Mac native CI and the full
clean-desktop compatibility matrix are still pending.

Offscreen checks do not establish usability on a clean desktop. Download the
matching artifact and test opening your PDFs, editing, Save As and reopening.
Existing form limits in [FORMS](FORMS.md) and [SAVING](SAVING.md) still apply;
dynamic XFA documents with added content cannot yet be saved.

## Reproduce Linux packaging locally

On a Rocky Linux 9 development machine or VM, install the baseline build
requirements, then build as a regular user:

```sh
sudo bash tools/setup_native_linux.sh
bash tools/build_native_linux.sh
```

This writes `build-native/`, `.deps/` and `dist/native/` in the checkout. Start
with a clean checkout or remove incompatible local build products first; do
not reuse a Fedora-built PDFium library for the RHEL baseline. To validate on
a host with Python 3.12+ and usable namespaces:

```sh
python3 tools/check_native.py dist/native/*.tar.gz \
  --smoke build-native/native-smoke --max-glibc 2.34
python3 tools/check_native.py dist/native/*.AppImage \
  --smoke build-native/native-smoke --max-glibc 2.34
```

Native Windows/Mac source builds use `tools/build_pdfium.py`, the viewer CMake
configuration in README and `tools/package_desktop.py`; the workflow contains
the complete SDK, Qt and packaging commands. They require native platform SDKs.

## Sources and notices

The application is GPL-3.0-only. Packages contain application/PDFium/dependency
notices and build manifests. Windows/Mac jobs also upload matching Qt source
archives as separate `qt-sources-*` artifacts. Pinned PDFium revisions, patches
and rebuild tooling are checked into this repository.

Before a public binary release, provide complete corresponding sources and
build inputs for the exact application and modified bundled dependencies.
Collected notices alone are not a corresponding-source distribution. No signing
credentials or release-publishing permissions are committed in this workflow.

# Native release candidates

These packages run the Qt desktop editor directly, with bundled runtime
dependencies. End users do not need a compiler or a separate Qt installation.

## Build and download on GitHub

Push the repository, including `.github/workflows/native.yml`, to GitHub on
`native-releases`, `main` or `master`. Relevant code changes trigger **Native
release candidates** automatically. You can also select **Actions → Native
release candidates → Run workflow**, choose the branch and start it manually.
Manual runs can select `all`, `x86_64` or `aarch64` Linux architectures and
turn Windows/macOS builds off. A commit marked `[linux-aarch64]` runs only ARM
Linux, preserving other architecture runs already in progress. No self-hosted
runner or signing secrets are required.

Wait for the jobs to finish, then open the workflow run and download its
**Artifacts**. Choose a `pdf-editor-*` artifact; `linux-validation-inputs-*` and `pdfium-linux-arm64-build-inputs`
is temporary tooling for the validation job. Extract the Windows artifact once:
`pdf-editor.exe`, DLLs and the `qml` folder are directly inside. Keep them together.
For Linux/macOS, GitHub wraps the application archives and `SHA256SUMS` in an extra ZIP.

| GitHub artifact | Application package | Start |
| --- | --- | --- |
| `pdf-editor-linux-x86_64` | Portable `.tar.gz` and `.AppImage` | Extract tarball and run `./pdf-editor`, or run AppImage |
| `pdf-editor-linux-aarch64` | ARM64 portable `.tar.gz` and `.AppImage` | Extract tarball and run `./pdf-editor`, or run AppImage on 64-bit ARM Linux |
| `pdf-editor-windows-x64` | ZIP with EXE and DLLs | Extract completely, double-click `pdf-editor.exe` |
| `pdf-editor-macos-arm64` | ZIP with `PDF Editor.app` | Extract on an Apple Silicon Mac, open the app |
| `pdf-editor-macos-x64` | ZIP with `PDF Editor.app` | Extract on an Intel Mac, open the app |

Keep the full extracted application folder together. The current package
version is `0.1.0-rc.1`. These are test candidates, not an automatically published
GitHub Release. The first PDFium/V8 build is substantial; later runs cache the
patched PDFium library. Linux builds have a three-hour limit; Windows/Mac builds
have a six-hour limit. XML-tested PDFium is cached before desktop deployment,
so a later smoke failure does not require recompiling it. Jobs provide separate
diagnostics on failure, including `worker-loader.log` and `native-smoke.log`
for desktop startup failures. Run [37781063288](https://github.com/Ewave267/pdf-editor/actions/runs/37781063288)
passed Mac Intel/Apple Silicon and both Linux formats. Windows passed the full
native job in [37783202566](https://github.com/Ewave267/pdf-editor/actions/runs/37783202566),
including independent verification of added text and both form save generations.

Linux ARM support is newly added and awaits its first successful CI run. The
x86_64 packages keep their existing names. ARM uses a native GitHub-hosted
`ubuntu-22.04-arm` runner for Qt builds, PDFium XML tests, sandboxed editing
and independent save verification. PDFium alone is cross-compiled on x86_64
using Chromium's pinned compiler and Rocky 9 ARM headers, because that upstream
compiler is distributed for x86_64 Linux. The cross build is cached only after
its XML tests pass on the native ARM runner. Each architecture has separate
runtime checksum pins, caches, intermediate inputs and final artifacts.

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

The targets are x86_64 and aarch64 (64-bit ARM) RHEL 9, Fedora and Ubuntu 22.04/24.04 or newer with a
graphical desktop session. This baseline improves portability; it does not
replace testing on those desktops. Older glibc systems, 32-bit ARM and musl
systems such as Alpine are outside this candidate's target.

Check `uname -m` to choose the matching Linux artifact. The examples below use
`x86_64`; replace that filename suffix with `aarch64` on 64-bit ARM Linux.

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
PDFium. The workflow initializes the installed Microsoft developer shell.
Qt installation uses Python 3.14; the pinned PDFium tools and packaging use
Python 3.12. `windeployqt` collects Qt/QML/plugins; the redistributable MSVC CRT DLLs
are copied beside the EXE so no redistributable installer is needed. The PDF
worker runs through `pdf-sandbox.exe` in a capability-free AppContainer with
CPU, memory, lifetime and child-process limits. No administrator installation
is required. The broker supplies the Windows profile environment paths needed
for AppContainer startup while keeping the executable search path restricted
to system libraries. Keep its helper, worker and DLLs beside the editor EXE,
including the `worker-runtime` folder. Packaging recursively scans the worker's
PE imports with MSVC `dumpbin` and collects its app-local dependencies there.
The broker stages only that smaller runtime for each launch, rather than all
editor DLLs. Missing non-system imports fail packaging; native loader and
normal/AcroForm/XFA edit/save/reopen checks validate the reduced runtime.

On Windows, the application starts one sandboxed renderer shortly after opening
its window. PDFium/V8 and the private runtime initialize while you choose a file.
The prepared worker waits on its pipe without reading a PDF; the first validated
document snapshot is sent through a bounded binary frame. Opening before warm-up
finishes waits for that same worker. Closing the app also stops the idle worker.
Warm-up moves first-document startup earlier; it does not remove Windows loading
costs, and opening immediately can still wait. Later documents and save workers
continue to use fresh isolated processes. Linux/macOS startup is unchanged.

Mac builds target macOS 13 or newer, separately for Intel and Apple Silicon.
`macdeployqt` bundles the Qt frameworks, QML/plugins and PDFium worker.
Only the editor's platform, image, icon and style plugins are selected; unused
SQL drivers and their external database clients are excluded. The worker
applies a deny-default Seatbelt profile inside the worker after trusted
dynamic loading and empty PDFium/V8 heap initialization, before Qt startup or document reads.
Resource limits are installed before Seatbelt, then verified inside the sandbox;
no network, child-process or host file-write access is granted. The Mac data limit accounts for its trusted startup VM mappings and bounds
additional growth to 768 MiB; this is not a 768 MiB resident-memory limit.
The worker verifies its sandbox before parsing a document. This uses Darwin sandbox SPI;
unsupported hosts fail closed rather than silently disabling isolation.
Native workers receive a minimal environment rather than the GUI's host environment.

If the Windows editor exits during startup, check
`%LOCALAPPDATA%\PDF Editor\startup.log` (the preceding launch is saved as
`startup.log.previous`). Qt fatal errors and failures to load the interface
also display an error dialog on desktop launches. Failures before application
entry or native crashes may not appear in this log. Keep the full extracted
folder together and launch `pdf-editor.exe`, rather than either helper.

For Windows performance diagnostics, open PowerShell in the extracted application
folder and run:

```powershell
$env:PDF_EDITOR_WORKER_DIAGNOSTICS = "1"
$env:PDF_EDITOR_SANDBOX_LOG = "$env:TEMP\pdf-editor-sandbox.log"
Remove-Item $env:PDF_EDITOR_SANDBOX_LOG -ErrorAction SilentlyContinue
.\pdf-editor.exe
```

Reproduce the slow open and typing, close the application, then collect
`$env:TEMP\pdf-editor-sandbox.log` and
`$env:LOCALAPPDATA\PDF Editor\startup.log`. Broker entries contain cumulative
`elapsed_ms` and copied DLL counts/bytes. The startup log includes worker startup
stages, request `queue_ms`, `roundtrip_ms`, `worker_ms`, and render `raster_ms` /
`png_ms`. Request identifiers connect send/reply entries; timings log operation
names and dimensions rather than typed text. `epoch_ms` timestamps correlate
the GUI launch, broker entry, `CreateProcessW`, resume and worker entry across
processes. Subtract adjacent timestamps to separate broker loading, sandbox setup,
worker process creation, pre-main loading and PDF initialization. A long pre-main
interval establishes the boundary but does not identify which OS loading/security
component caused it; that requires a Windows system performance trace.
Diagnostic logging is optional and
adds overhead; remove the two environment variables afterward for normal use.

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
Worker tests ran on Fedora. Mac Intel and Apple Silicon native CI passed
GUI startup, isolated document editing, both saved generations and independent
PDF.js verification. Windows also passed these checks after removing console
initialization from its pipe worker. Its offscreen CI font loader explicitly
uses Windows Fonts; the desktop application uses the normal Windows Qt backend.
Clean-desktop walkthroughs remain pending.

Offscreen checks do not establish usability on a clean desktop. Download the
matching artifact and test opening your PDFs, editing, Save a copy and reopening.
Existing form limits in [FORMS](FORMS.md) and [SAVING](SAVING.md) still apply;
dynamic XFA documents with added content cannot yet be saved.

## Reproduce Linux packaging locally

On a Rocky Linux 9 development machine or VM, install the baseline build
requirements, then build as a regular user:

```sh
sudo bash tools/setup_native_linux.sh
bash tools/build_native_linux.sh
```

On aarch64, first provide the ARM PDFium dependency from the workflow's
`pdfium-linux-arm64-build-inputs` artifact in `.deps/pdfium-patched/`.
The workflow handles this automatically. Bootstrapping PDFium uses the
x86_64 cross-builder because Chromium's pinned Linux compiler is x86_64;
the editor itself builds and runs on native ARM.
Before building locally with those intermediate inputs, run their XML tests
on the ARM machine:

```sh
chmod +x .deps/pdfium-patched/build-tests/pdfium_unittests
.deps/pdfium-patched/build-tests/pdfium_unittests --gtest_filter='CFXXML*'
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

## Candidate version and roadmap validation

Edit the root `VERSION` file before building a new candidate. CMake's numeric
project/bundle version is derived from it; executable version output and archive
names include the candidate suffix. Packaging rejects a mismatched Linux
executable/archive version. The native smoke helper checks page order, rotation,
merge, added-content preservation, AcroForm script conversion, flattening and
print-to-PDF alongside repeated native form saves. Its `performance.json` records
open/render timings and platform/Qt information. Physical printing and clean
native desktops remain separate acceptance checks.

Qt Widgets and PrintSupport are now GUI dependencies. Workers retain their
smaller Core/Gui dependency closure. Theme preferences use the C++ QSettings backend, retaining compatibility
with the supported Qt 6.4 baseline without additional QML settings modules. macOS deployment includes
only the required plugin categories, including printer support.

Patch 0004 changes the PDFium cache key: the first CI run after this update must
rebuild each platform's dependency. Subsequent builds reuse the new cache. See
[RELEASE](RELEASE.md) for the outstanding V8 freshness review and production gates.

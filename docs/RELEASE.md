# Linux preview package

The package is an offline Linux desktop preview, not a completed MVP release.
It includes the GUI, isolated renderer, patched PDFium, desktop entry, icon and
PDFium dependency notices. Qt and operating-system libraries come from the host.

## Build and install

On Fedora/RHEL-family systems, `./compile.sh --package` builds the Release
viewer and generates the archive in `dist/`. Dependency installation is confirmed
interactively, or permitted explicitly with `--yes`. To configure manually,
use the viewer configuration in VIEWER.md, with `CMAKE_BUILD_TYPE=Release`.
After building, run `cpack --config build-viewer/CPackConfig.cmake -B dist`.
The resulting `.tar.gz` has a single top-level directory. Extract it into a
user-owned location and run `bin/pdf-form-editor`, optionally followed by a PDF
filename. Keep `bin`, `lib` and `share` together; the entire directory can move.
For a system installation, `cmake --install build-viewer --prefix /usr/local`
installs the same files. The desktop entry requires `bin` on PATH. A GUI process
stays running until its window closes; that is expected, not compilation.

The local artifact targets Fedora 42 x86-64 with Qt 6.10.2 and glibc 2.41.
Install runtime packages `bubblewrap qt6-qtbase qt6-qtdeclarative` and
`liberation-sans-fonts` for metric-compatible Arial substitution. A desktop
session, Qt platform plugin and
Qt Quick/QML runtime modules are required; no compiler, SDK, Python, Node.js or
PDF.js is used by the application. Fedora 42 is the local build environment,
not a promise of support for other distributions. Build separately against the
runtime libraries of each target distribution; the Ubuntu CI artifact is a
separate Ubuntu 24.04 build. Do not assume binary compatibility across them.
Bubblewrap requires usable user namespaces; unsupported hosts fail closed.

## Validation and release gates

Stage with `cmake --install build-viewer --prefix /tmp/pdf-editor-package`.
For automated checks, run `python3 tools/check_package.py <extracted-directory>
build-viewer/viewer-tests` after building the integration configuration.
Check installed executable RPATHs with `readelf -d`, launch the installed GUI,
and exercise the staged renderer with the integration harness placed beside
it. The harness is validation tooling, not part of the archive. A relocated
installation must select its packaged PDFium even when the development library
is unavailable. Offscreen automated checks do not establish desktop usability
on a fresh installation; a clean desktop walkthrough remains required.

Normal PDFs support text, images, signatures, Save As and reopening. Synthetic
AcroForms and dynamic XFA forms support editing and saving native values.
Full XFA with added text or signatures is deliberately refused during save;
AcroForm keystroke conversion and foreground XFA compatibility also remain
step 6 work. These gaps block the full MVP workflow. No final release or
security certification is claimed, and no package is published automatically.

## Sources and notices

The application is GPL-3.0-only. Preserve LICENSE and the bundled PDFium notices.
Qt and operating-system libraries are not bundled. The PDFium build manifest
records exact upstream revisions and patch hashes. Application sources, native
patches and the pinned rebuild recipe are in this repository, including
`third_party/pdfium` and `tools/build_pdfium.py`; see XFA-PROBE.md for rebuilding.
Before public binary distribution, prepare complete corresponding source for
the exact application and modified dependency build, including upstream source
and build inputs. This preview archive alone is not that source distribution.

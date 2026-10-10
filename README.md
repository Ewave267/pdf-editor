# PDF Editor

An open-source desktop application for completing PDF forms and adding text,
signatures, and annotations, entirely offline.

The project is in early development. The Linux Qt/QML viewer now opens PDFs,
renders and scrolls pages, zooms, navigates by page number, and shows thumbnails.
PDFium runs in an isolated worker behind a C++ document interface. Step 2 also
verifies exact XFA values through repeated saves and independent PDF.js reopening.
The viewer also supports adding, moving, resizing, and deleting text, images,
image-based signatures, checkmarks, freehand strokes, highlights, shapes and
stamps. Added text supports font family, size, bold, italic and underline.
Everyday editing includes grouped undo/redo, copy/paste, multi-selection,
alignment and keyboard controls; see [added-content controls](docs/ADDED-CONTENT.md).
Save a copy embeds additions in a new PDF, with atomic writes and source-file
protection. Native text fields, checkboxes, radio buttons, dropdowns, and keyboard
navigation work for the synthetic AcroForm and dynamic XFA fixtures. The
[form toolbar](docs/FORMS.md) adds field highlights, required-field feedback,
cross-page navigation, reset, date/choice helpers and guided visual signatures,
with explicit XFA helper limits. Save a copy
preserves edited values and XFA calculations. Dynamic XFA copies without additions
are supported; dynamic XFA additions cannot yet be saved. The Linux safety
gate now tests malformed documents, restricted JavaScript access, memory limits
and worker recovery; see [SAFETY](docs/SAFETY.md).

The interface uses familiar File/Edit/View menus, a clear **Save a copy** action,
contextual text formatting, page thumbnails and a welcome screen with recent
files. See the [desktop guide](docs/DESKTOP-EXPERIENCE.md) for a quick walkthrough.

The stack is C++20, Qt Quick / QML, PDFium, and CMake. The default build remains
a dependency-free foundation check. Enable the viewer explicitly with
`PDF_EDITOR_BUILD_VIEWER=ON` and the source-pinned, patched PDFium package.

## Native releases

The primary distribution direction is now native desktop packages: Windows ZIP
with an executable and DLLs, Linux portable tarball and AppImage, and macOS app
bundle. See [native release status and build instructions](docs/NATIVE-RELEASES.md).
GitHub Actions builds and tests the real editor on native Windows, macOS and
Linux runners, then uploads downloadable artifacts. Push this branch to GitHub
and open **Actions → Native release candidates**. Complete successful builds
also publish **Development preview** prereleases for public downloads on the
[Releases page](https://github.com/Ewave267/pdf-editor/releases). These previews
are explicitly unstable and are not marked as the latest stable release.
The publisher must be present on GitHub's default branch; see
[preview setup and publishing an existing build](docs/NATIVE-RELEASES.md#automatic-development-previews).
See the linked instructions
for downloading and running each package. Windows and Mac Intel/Apple Silicon
ZIPs and both Linux formats have passed native CI packaging and save/reopen
validation. Clean-desktop walkthroughs remain the next release check.

Linux release targets are RHEL 9, Fedora, and Ubuntu 22.04/24.04 or newer
on x86_64 and aarch64 (64-bit ARM). Both architectures have passed native CI
packaging and save/reopen checks.
The builder must bundle dependencies compiled against glibc 2.34 or older;
the packaging tool enforces this baseline. Local Fedora previews require newer
glibc and do not establish RHEL compatibility.

## Build the native desktop app (developers)

```sh
python3 tools/build_pdfium.py --jobs 4
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
  -DPDF_EDITOR_BUILD_VIEWER=ON -DBUILD_TESTING=OFF \
  -DPDFium_DIR="$PWD/.deps/pdfium-patched"
cmake --build build-release --parallel 4
./build-release/pdf-form-editor
```

Developers need Qt 6.4+ development packages, CMake, a C++20 compiler, Ninja,
Python and Git. Linux also needs Bubblewrap; Windows needs MSVC and the Windows
SDK, and macOS needs Xcode. The commands above show the Linux build. Skip the PDFium source build when a matching patched
package already exists. End users run the packaged application directly.
See [native release instructions](docs/NATIVE-RELEASES.md) for bundling Qt and
building against the RHEL 9 compatibility baseline.

## Build and verify

Requirements: CMake 3.20 or newer, a C++20 compiler, and a native build tool
(such as Make or Ninja).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/pdf-form-editor
```

For a multi-configuration generator, add `--config Debug` to the build command
and `-C Debug` to the test command. The executable will be under `build/Debug/`.

## Minimal viewer

Reuse the patched PDFium library from step 2. With Qt 6 development packages
and Bubblewrap available:

```sh
cmake -S . -B build-viewer -DCMAKE_BUILD_TYPE=Debug \
  -DPDF_EDITOR_BUILD_VIEWER=ON -DPDFium_DIR="$PWD/.deps/pdfium-patched"
cmake --build build-viewer --parallel 4
./build-viewer/pdf-form-editor
```

See [VIEWER](docs/VIEWER.md) for complete setup, controls, limits and test
instructions, including this workspace's local Qt SDK configuration. See
[ADDED-CONTENT](docs/ADDED-CONTENT.md) for the step 5 tools and
[FORMS](docs/FORMS.md) for form controls and compatibility limits, and
[SAVING](docs/SAVING.md) for Save a copy support and remaining validation gaps.

## Development

C++ formatting uses the repository's `.clang-format` configuration:

```sh
clang-format -i src/app/main.cpp
clang-format --dry-run --Werror src/app/main.cpp
```

GitHub Actions configures, builds, and runs the smoke check on Linux.

## XFA integration probe

See [the probe instructions and persistence fixes](docs/XFA-PROBE.md) for the
source build, sandbox requirements, and validation commands. The XFA CI workflow
runs regression, isolation, and strict independent-reader checks on pushes and
pull requests. Preserve the generated package's upstream and dependency license
notices when distributing it. Current coverage is synthetic, one-page dynamic
XFA; broader form compatibility belongs to the later MVP steps.

## Project layout

- `src/app/`: application entry point and lifecycle.
- `src/pdf/`: document interface, isolated renderer and XFA probe.
- `src/content/`: application-owned text, image, and signature objects.
- `src/ui/` and `qml/`: page painting and the Qt Quick viewer shell.
- `tests/pdfs/`: compatibility corpus, organized by document type.
- `docs/adr/`: architecture decision records.

Read [GOAL](docs/GOAL.md), [MVP](docs/MVP.md), and
[ROADMAP](docs/ROADMAP.md) for the product and implementation scope.
[PROGRESS](PROGRESS.md) tracks completed work, validation, and next steps.

## License

Copyright (c) 2026 PDF Form Editor contributors.

Licensed under the GNU General Public License version 3 only
(`GPL-3.0-only`); see [LICENSE](LICENSE).

## Linux preview package

Release archive installation, runtime dependencies and validation are documented
in [RELEASE](docs/RELEASE.md). Step 8 packaging is implemented; the final MVP
release gate remains open for the documented step 6 compatibility gaps.

## Document workflow and recovery

The Document menu includes page export, merge, printing, search, recent files,
recovery, themes and optional full screen. See
[document operations](docs/DOCUMENT-WORKFLOW.md) and
[desktop controls/recovery](docs/DESKTOP-EXPERIENCE.md).
The [roadmap](docs/ROADMAP.md) distinguishes implemented core features from
remaining production acceptance. Candidate artifacts remain unsigned; the V8
freshness advisory and clean-desktop checks still require review.

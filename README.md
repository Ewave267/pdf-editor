# PDF Form Editor

An open-source desktop application for completing PDF forms and adding text,
signatures, and annotations, entirely offline.

The project is in early development. The Linux Qt/QML viewer now opens PDFs,
renders and scrolls pages, zooms, navigates by page number, and shows thumbnails.
PDFium runs in an isolated worker behind a C++ document interface. Step 2 also
verifies exact XFA values through repeated saves and independent PDF.js reopening.
The viewer also supports adding, moving, resizing, and deleting text, images,
and image-based signatures. Save As embeds additions in a new PDF, with atomic writes and source-file
protection. Viewer form editing is pending. Dynamic XFA copies without additions
are supported; dynamic XFA additions cannot yet be saved.

The stack is C++20, Qt Quick / QML, PDFium, and CMake. The default build remains
a dependency-free foundation check. Enable the viewer explicitly with
`PDF_EDITOR_BUILD_VIEWER=ON` and the source-pinned, patched PDFium package.

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
[SAVING](docs/SAVING.md) for Save As support and remaining validation gaps.

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
- `src/document/`: reserved for future document workflow models.
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

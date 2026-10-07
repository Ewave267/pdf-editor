# MVP Progress

Last updated: 2026-10-06

## Current state

Steps 1, 2, 3 and 5 are implemented and verified locally. Step 2's isolated PDFium
probe preserves exact XFA input and calculated values across two save/reopen
cycles for both packet-array and single-stream forms. Pinned PDF.js independently
opens and lays out both saved generations and verifies the values.
The Linux Qt/QML viewer opens and scrolls PDFs, zooms, fits pages, navigates by
page number, and shows thumbnails. Rendering runs in an isolated asynchronous
worker behind a C++ interface. Added text, images, and image-based signatures
can be placed, selected, moved, resized, edited, and deleted. Step 4 form editing
and step 6 saving remain pending. Additions are held in memory only.

## Step 1 — Repository Foundation

- [x] Add README with scope, build instructions, and project layout.
- [x] Preserve `docs/MVP.md` and `docs/ROADMAP.md`.
- [x] Create `docs/adr/` with guidance for future decisions.
- [x] Create `src/`, `qml/`, `tests/`, and `tests/pdfs/`.
- [x] Add GPL 3.0 license (`GPL-3.0-only`), as requested by the user.
- [x] Add `.gitignore` for build products and local configuration.
- [x] Add basic CMake project and C++20 executable.
- [x] Add `.clang-format` and `.editorconfig`.
- [x] Add minimal GitHub Actions configure/build/test workflow.
- [x] Verify local configuration, compilation, smoke test, and formatting.

## Step 2 — Prove XFA First

- [x] Pin PDFium XFA/V8 `157.0.8086.0` and its source-build tooling.
- [x] Keep PDFium behind a C++ application-facing interface.
- [x] Initialize PDFium and open a known dynamic XFA fixture.
- [x] Detect the form type and load XFA with a version-2 form environment.
- [x] Render the page with form widgets and inspect the result.
- [x] Read and edit the input field.
- [x] Verify initial calculation and explicit field-exit JavaScript results.
- [x] Deny scripted URL actions and verify process isolation/deadlines.
- [x] Save a new PDF and close original document/form/page handles.
- [x] Fix generated newlines in datasets and form serialization.
- [x] Fix silently lost edits in single-stream XFA saving.
- [x] Verify exact values after reopening and a second edit/save/reopen cycle.
- [x] Verify spaces and XML-sensitive characters without trimming values.
- [x] Run upstream XML tests, including embedded newline/tab preservation.
- [x] Open saved files with independent PDF.js and verify layout/field values.
- [x] Require the strict milestone gate on CI pushes and pull requests.
- [x] Record the integration decision and reproducible validation instructions.

Coverage is synthetic one-page dynamic XFA with known coordinates and printable
ASCII edits. The explicit exit-event calculation is proven. Automatic dependency
recalculation, real-world/static XFA, Unicode interaction, timers, and multipage
relayout need later form compatibility work. Independent PDF.js validation
parses/layouts XFA and reads persisted values; it does not execute JavaScript.
Adobe Acrobat compatibility has not been tested.

## Step 3 — Minimal Viewer

- [x] Add an opt-in Qt/QML application shell and local PDF file dialog.
- [x] Keep PDFium calls and handles inside an isolated C++ rendering worker.
- [x] Render single-page and mixed-size multi-page documents asynchronously.
- [x] Scroll pages vertically and horizontally with bounded image caching.
- [x] Add zoom in/out and fit-to-page sizing.
- [x] Navigate with Previous/Next, typed page number and thumbnail clicks.
- [x] Render thumbnails with the same page renderer and verify page identity.
- [x] Replace a document and release its old worker, page model and cache.
- [x] Close idle and actively rendering documents without crashing.
- [x] Show controlled invalid-input and sandbox-startup errors; fail closed.
- [x] Verify the actual QML controls and inspect a rendered screenshot.
- [x] Add integration tests, CI coverage and [viewer instructions](docs/VIEWER.md).
- [x] Record [ADR 0002](docs/adr/0002-viewer-render-worker.md).

Step 3 local validation used Qt 6.10.2 and the existing patched PDFium library:

```sh
cmake -S . -B build-viewer -DCMAKE_CXX_COMPILER=/usr/bin/c++ -DCMAKE_BUILD_TYPE=Debug \
  -DPDF_EDITOR_BUILD_VIEWER=ON -DPDF_EDITOR_BUILD_XFA_PROBE=ON \
  -DPDFium_DIR="$PWD/.deps/pdfium-patched" -DCMAKE_PREFIX_PATH="$PWD/.deps/qt-sdk/usr"
cmake --build build-viewer --parallel 4
ctest --test-dir build-viewer --output-on-failure
./build-viewer/pdf-form-editor
```

All 4 CTest entries pass: application identity, four viewer integration cases,
eleven XFA regression/isolation tests, and the strict independent-reader XFA
gate. Viewer tests use Qt's offscreen/software platform. Pixel checks verify
all three differently sized pages and their thumbnails; UI checks exercise
file-dialog acceptance, keyboard page entry, zoom/fit, thumbnails, navigation
and scroll tracking. Replacement and close tests verify the old PID is gone,
including close during active rendering. The default foundation build still
passes independently. Qt development files were extracted locally; no system
Qt packages were installed and PDFium was not rebuilt.

Viewer limits and remaining compatibility work are in [VIEWER](docs/VIEWER.md).
The viewer currently opens local, unencrypted PDFs; it has no form-editing or
save controls yet. CI is configured for the combined suite but has not been run
remotely. GPL-3.0-only licensing remains in place.

## Step 5 — Added Content

- [x] Add document-owned objects with stable IDs, page, geometry, type and content.
- [x] Create text, local images, and image-based signatures with click placement.
- [x] Select, drag, resize, delete, and edit added text through the QML interface.
- [x] Render additions on pages and thumbnails independently of source PDF content.
- [x] Keep coordinates stable across zoom and constrain edits to their original page.
- [x] Snapshot image content and preserve exact text content in memory.
- [x] Ask before discarding additions on open, close, or application exit.
- [x] Validate all three types through model, rendering, and QML interaction tests.

See [ADDED-CONTENT](docs/ADDED-CONTENT.md) for controls and limitations. Step 5
was implemented independently of pending step 4. It supports image-based
signatures; drawing signatures, saving, and recovery are not implemented.
The two new viewer cases pass alongside the existing four viewer cases.
The combined CTest suite passes 4/4 entries, including eleven XFA regressions
and four independent-reader opens. Formatting and whitespace checks pass.
No PDFium rebuild was needed. GPL-3.0-only licensing remains unchanged.

## Validation

Passed locally:

```sh
python3 tools/build_pdfium.py --skip-sync --jobs 8
npm ci --prefix tests/reader --ignore-scripts
cmake -S . -B build-xfa-fixed -DCMAKE_CXX_COMPILER=/usr/bin/c++ -DCMAKE_BUILD_TYPE=Debug \
  -DPDF_EDITOR_BUILD_XFA_PROBE=ON -DPDFium_DIR="$PWD/.deps/pdfium-patched"
cmake --build build-xfa-fixed --parallel
ctest --test-dir build-xfa-fixed --output-on-failure
clang-format --dry-run --Werror src/app/main.cpp src/app/xfa_probe_main.cpp src/pdf/XfaProbe.h src/pdf/XfaProbe.cpp
```

The source checkout was synced separately before the `--skip-sync` build. For a
fresh checkout, omit `--skip-sync`; see [XFA-PROBE](docs/XFA-PROBE.md).
The patched library was built with Chromium's downloaded Clang toolchain; the
application uses GNU C++ 15.2.1. All 69 upstream XML tests pass. CTest passes 3/3 entries:
foundation smoke, eleven regression/isolation tests, and the strict round-trip
gate with four independent-reader opens. Python syntax, JavaScript syntax, C++
formatting, workflow YAML, and the default foundation build/smoke test pass.

The explicit compiler path bypasses the workspace's read-only ccache directory.
Sandbox tests used approved tool escalation because the nested tool sandbox
blocks Bubblewrap namespaces; the worker retained Bubblewrap isolation. CI is
configured but has not been run remotely. No commit or push was performed.

The original unpatched binary failed both persistence encodings. Those failures
are documented with their source fixes in [XFA-PROBE](docs/XFA-PROBE.md). CMake
now requires the patched package and rejects the original binary.

## Remaining MVP steps

| Step | Status | Next milestone |
| --- | --- | --- |
| 2 — Prove XFA First | Complete for committed fixtures | Expand compatibility during forms work |
| 3 — Minimal Viewer | Complete for committed fixtures | Extend form interaction in step 4 |
| 4 — Forms | Not started | AcroForm and representative XFA field interaction |
| 5 — Added Content | Complete for synthetic fixtures | Save additions in step 6 |
| 6 — Save | Not started | Preserve form changes and added content on reopen |
| 7 — Safety | Not started | Broader untrusted PDF and JavaScript isolation testing |
| 8 — MVP Release | Not started | Package the verified workflow for one desktop platform |

Update this file as milestones land, including checks actually run and remaining
limitations. Do not infer broad PDF compatibility from the synthetic probe.

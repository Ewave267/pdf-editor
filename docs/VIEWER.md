# Step 3 — Minimal viewer

The Linux Qt/QML viewer opens local PDFs, renders pages, scrolls vertically and
horizontally, zooms, fits pages to the viewport, and navigates by page number,
Previous/Next, or thumbnails. The sidebar uses the same renderer as the main
pages. The default dependency-free foundation build remains available.
Step 5 adds [text, images and image-based signatures](ADDED-CONTENT.md) in a
separate overlay; step 6 adds [Save As](SAVING.md) with remaining form/XFA
validation gaps.

## Build and run

Requirements: CMake 3.20+, a C++20 compiler, Qt 6.4+ development packages for
Core, Gui, Qml, Quick, QuickControls2 and Test, the corresponding Qt Quick QML
runtime modules (including Dialogs and Layouts), Bubblewrap, and the patched
PDFium package from [step 2](XFA-PROBE.md).

Reuse `.deps/pdfium-patched` if it already exists; do not rebuild PDFium just to
change the viewer.

```sh
cmake -S . -B build-viewer -DCMAKE_BUILD_TYPE=Debug \
  -DPDF_EDITOR_BUILD_VIEWER=ON -DPDFium_DIR="$PWD/.deps/pdfium-patched"
cmake --build build-viewer --parallel 4
./build-viewer/pdf-form-editor
# Optional: open a document at startup.
./build-viewer/pdf-form-editor tests/pdfs/normal/multi-page.pdf
ctest --test-dir build-viewer --output-on-failure
```

For the complete viewer and XFA suite, also configure with
`-DPDF_EDITOR_BUILD_XFA_PROBE=ON`; this requires the pinned Node/PDF.js reader
from step 2. `--version` runs without a display.

In this workspace, matching Qt 6.10.2 development RPMs were extracted into the
ignored `.deps/qt-sdk/` directory and linked to the installed Qt runtime. Add
`-DCMAKE_PREFIX_PATH="$PWD/.deps/qt-sdk/usr"` and
`-DCMAKE_CXX_COMPILER=/usr/bin/c++` to configure here. No Qt system packages were
installed or PDFium rebuilt during step 3. A normal system Qt development
installation needs neither workspace-specific workaround.

The host must permit Bubblewrap user namespaces. Nested tool sandboxes can block
that setup; local tests used approved execution outside the tool sandbox while
retaining the renderer's own isolation. Failure to start isolation is an error;
the viewer never runs the renderer unconfined.

## Controls

- Open PDF or Ctrl+O selects a local document.
- Scroll the main view or drag its scrollbars to browse pages.
- Plus/minus or Ctrl++/Ctrl+- adjusts zoom; Fit page or Ctrl+0 restores fitting.
- Type a page number and press Enter, use Previous/Next, or click a thumbnail.
- Close or Ctrl+W releases the document and its renderer process.

Opening a second document clears the old page model, cached images and pending
requests before starting a new renderer. Password-protected documents currently
produce an error; password entry and viewer form editing are later milestones.
Save As or Ctrl+Shift+S saves a new PDF; see [SAVING](SAVING.md).
With additions present, open and close require confirmation before discarding.

## Architecture and limits

`PdfDocument` is the application-facing C++ object exposed to QML. It owns
metadata, navigation, zoom, an asynchronous request queue and a 64 MiB image
cache. `PdfPageItem` paints received images and cancels obsolete requests;
ListView delegates keep only nearby pages and thumbnails instantiated. Resizing
and zooming debounce new raster requests while displaying the existing image.
QML contains no PDFium headers, handles or API calls.

A persistent `pdf-render-worker` owns all PDFium resources on one thread. It
opens only `/input.pdf`, initializes XFA when necessary, reads page geometry,
and replies to JSON-line render requests with bounded PNG images. Native pages stay alive for form interaction; bitmaps are released after each
render, and document/form/page/library handles close on
normal worker exit. Closing during a render has a short grace period before
killing the isolated process, which releases its resources at the OS boundary.

The worker runs in Bubblewrap namespaces with a cleared environment, no
capabilities, read-only system runtime/fonts, its executable/library and the
selected input PDF. It has no writable host output directory. The shared
PDFium host callbacks deny external document actions and timers. Startup and
render requests have 15- and 10-second deadlines; the worker has a cumulative
300-second CPU limit, a 768 MiB writable-data limit, no core dumps and a
128-descriptor limit. A fail-closed kernel syscall policy also blocks execution,
process creation, sockets and writable file opens. See [SAFETY](SAFETY.md).

Current development limits are 64 MiB input, 2000 pages, page dimensions of
1–14400 points, raster widths of 96–2400 pixels and at most 16 million pixels per
image. Very unusual aspect ratios or oversized raster requests return an error.
The probe's broader security work, timers, passwords, comprehensive Unicode input
and real-world XFA compatibility remain later milestones. See [FORMS](FORMS.md)
for native form input and its current compatibility limits.

## Validation

`viewer-integration` uses QtTest and the actual QML scene on Qt's offscreen,
software-rendered platform. The original six integration cases verify:

- All three mixed-size fixture pages render at the correct dimensions and
  colors; full-size and thumbnail center pixels match for each page.
- Single-page replacement and explicit close release the previous process.
- Invalid input returns a controlled error and the XFA fixture renders.
- Closing during an active render releases the worker; missing Bubblewrap fails
  closed; file-dialog acceptance, zoom, fit, typed page navigation, thumbnail
  clicks and scrolling work through the UI.

Two additional cases validate the added-content model, rendering, file pickers,
placement, dragging, resizing, deletion, and discard protection; see
[ADDED-CONTENT](ADDED-CONTENT.md).

Five additional save cases cover export, reopen, rotated/cropped coordinates,
save failures, revision tracking, existing forms, and the Save As file dialog.
The combined build invokes PDF.js and the native XFA probe on saved outputs.

Four form data cases validate native control interaction and actual QML keyboard/
mouse input for both AcroForm and dynamic XFA. Independent PDF.js checks cover
every persisted control value; see [FORMS](FORMS.md).

Step 7 adds dedicated controller/native safety gates for malformed input, large
documents, memory limits, blocked host access, script exceptions and real
startup/form/save deadlines; see [SAFETY](SAFETY.md).

The screenshot was visually inspected. Source fixtures are original synthetic
GPL-3.0-only documents, not evidence of arbitrary PDF compatibility. The complete
local CTest suite also retains step 2's exact save/reopen and independent PDF.js
checks. CI is configured to run the combined suite; no remote CI run is claimed.

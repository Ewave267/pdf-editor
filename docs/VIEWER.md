# Step 3 — Minimal viewer

The Linux Qt/QML viewer opens local PDFs, renders pages, scrolls vertically and
horizontally, zooms, fits pages to the viewport, and navigates by page number,
Previous/Next, or thumbnails. The sidebar uses the same renderer as the main
pages. The default dependency-free foundation build remains available.

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
produce an error; password entry, form editing and saving are later milestones.

## Architecture and limits

`PdfDocument` is the application-facing C++ object exposed to QML. It owns
metadata, navigation, zoom, an asynchronous request queue and a 64 MiB image
cache. `PdfPageItem` paints received images and cancels obsolete requests;
ListView delegates keep only nearby pages and thumbnails instantiated. Resizing
and zooming debounce new raster requests while displaying the existing image.
QML contains no PDFium headers, handles or API calls.

A persistent `pdf-render-worker` owns all PDFium resources on one thread. It
opens only `/input.pdf`, initializes XFA when necessary, reads page geometry,
and replies to JSON-line render requests with bounded PNG images. Pages and
bitmaps are released after each render; document/form/library handles close on
normal worker exit. Closing during a render has a short grace period before
killing the isolated process, which releases its resources at the OS boundary.

The worker runs in Bubblewrap namespaces with a cleared environment, no
capabilities, read-only system runtime/fonts, its executable/library and the
selected input PDF. It has no writable host output directory. The shared
PDFium host callbacks deny external document actions and timers. Startup and
render requests have 15- and 10-second deadlines; the worker has a cumulative
300-second CPU limit, no core dumps and a 128-descriptor limit.

Current development limits are 64 MiB input, 2000 pages, page dimensions up to
14400 points, raster widths of 96–2400 pixels and at most 16 million pixels per
image. Very unusual aspect ratios or oversized raster requests return an error.
The probe's broader security work, timers, passwords, Unicode form interaction
and real-world XFA compatibility remain later milestones.

## Validation

`viewer-integration` uses QtTest and the actual QML scene on Qt's offscreen,
software-rendered platform. Four integration cases verify:

- All three mixed-size fixture pages render at the correct dimensions and
  colors; full-size and thumbnail center pixels match for each page.
- Single-page replacement and explicit close release the previous process.
- Invalid input returns a controlled error and the XFA fixture renders.
- Closing during an active render releases the worker; missing Bubblewrap fails
  closed; file-dialog acceptance, zoom, fit, typed page navigation, thumbnail
  clicks and scrolling work through the UI.

The screenshot was visually inspected. Source fixtures are original synthetic
GPL-3.0-only documents, not evidence of arbitrary PDF compatibility. The complete
local CTest suite also retains step 2's exact save/reopen and independent PDF.js
checks. CI is configured to run the combined suite; no remote CI run is claimed.

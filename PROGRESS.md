# MVP Progress

Last updated: 2026-10-07

## Current state

Steps 1, 2, 3, 4, 5 and 7 are implemented and verified locally. Step 2's isolated PDFium
probe preserves exact XFA input and calculated values across two save/reopen
cycles for both packet-array and single-stream forms. Pinned PDF.js independently
opens and lays out both saved generations and verifies the values.
The Linux Qt/QML viewer opens and scrolls PDFs, zooms, fits pages, navigates by
page number, and shows thumbnails. Rendering runs in an isolated asynchronous
worker behind a C++ interface. Added text, images, and image-based signatures
can be placed, selected, moved, resized, edited, and deleted. Native form input
supports text, checkboxes, radios, dropdowns and keyboard traversal. Save As
captures live form edits and embeds additions in new PDFs. Step 6 still has gaps
for full AcroForm script behavior, dynamic XFA additions and foreground XFA;
see [FORMS](docs/FORMS.md) and [SAVING](docs/SAVING.md). Step 7 adds kernel
restrictions, allocation limits, hostile-document tests and controlled worker
failure; see [SAFETY](docs/SAFETY.md).

## Compatibility fix — XFA radios and clipped captions

The reported nine-page maternity/parental leave form reproduced two PDFium
compatibility bugs. Empty calculate placeholders prevented radio selections in
section 6; missing Arial was substituted with fonts whose wider metrics wrapped
and clipped the waiting-period caption. Native patchset 2 permits empty
calculations to be edited and prefers Liberation Sans for Linux Arial fallback.
Actual calculations and form-authored read-only conditions remain enforced.

The original local form was validated without changing its bytes: all three
section 3 squares work after opting into benefits; both section 6 province
choices, waiting-period choices, every Quebec rate group and the dependent
squares work. Saved datasets contain the expected values, and reopening retains
selections. The entire waiting-period sentence now fits on one line. The form
and derived local artifacts were not added to the repository.

Added original empty-calculation and Arial-caption fixtures, automated native
and mouse/keyboard persistence cases, and a pixel comparison with an explicit
Liberation Sans caption. The pinned native rebuild passed all 69 upstream XML
unit tests. The complete 6/6 CTest suite passed in 110 seconds, including all
20 viewer results, native security checks and independent-reader round trips.
The rebuilt Release archive also passed all 20 relocated-package viewer results.
Rebuild instructions and runtime font dependencies are documented
in [XFA-PROBE](docs/XFA-PROBE.md). Existing step 6 release gaps remain.

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

## Step 4 — Forms

- [x] Keep native form pages alive in the isolated PDFium worker.
- [x] Forward mouse, keyboard and committed text input from Qt/QML.
- [x] Read/edit text fields and redraw pages/thumbnails after input.
- [x] Check/uncheck checkboxes, switch radio choices and select dropdown values.
- [x] Traverse focus with Tab and Shift+Tab; select, copy, paste and delete text.
- [x] Preserve required XFA exit events and calculated values after editing.
- [x] Save the live native form state, including an uncommitted focused field.
- [x] Reopen edited AcroForm and dynamic XFA with exact values.
- [x] Verify all control states with independent PDF.js, including unchecked states.
- [x] Retain form edits and additions after destination write failure.
- [x] Store original, reproducible GPL-3.0-only control fixtures.
- [x] Exercise actual QML mouse/key input and inspect the refreshed interface.

Validated against the committed synthetic single-page fixtures. Full XFA additions,
dynamic page relayout, foreground/static XFA, pointer-drag text selection, complex
input methods and broader production compatibility remain outstanding. The pinned
AcroForm keystroke script's uppercase conversion is not applied by PDFium's native
path; actions remain present in saved output. See [FORMS](docs/FORMS.md).

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
signatures; drawing signatures and recovery are not implemented. Saving was
subsequently added in step 6.
The two new viewer cases pass alongside the existing four viewer cases.
The combined CTest suite passes 4/4 entries, including eleven XFA regressions
and four independent-reader opens. Formatting and whitespace checks pass.
No PDFium rebuild was needed. GPL-3.0-only licensing remains unchanged.

## Step 6 — Save As

- [x] Add Save As and Ctrl+Shift+S through the QML file dialog.
- [x] Preserve a private source snapshot and reject original-file save targets.
- [x] Export text with embedded fonts and lossless images/signatures.
- [x] Insert additions in an isolated worker; atomically commit a new PDF.
- [x] Verify additions after reopen on mixed-size, rotated, and cropped pages.
- [x] Keep document content and source PDF bytes intact.
- [x] Verify existing AcroForm values, editable widgets, defaults, and script actions.
- [x] Verify native XFA values and calculation/round trips after saving a copy.
- [x] Independently verify added content, AcroForm, and XFA output with PDF.js.
- [x] Retain changes on failed save; track edits made while saving separately.
- [x] Validate newly edited forms through the viewer and independent PDF.js.
- [x] Validate native AcroForm widget editing after saving.
- [ ] Fix the pinned AcroForm keystroke script conversion path.
- [ ] Support additions on dynamic full XFA while preserving its behavior.
- [ ] Validate representative foreground/static XFA documents with additions.

Implementation and limits are in [SAVING](docs/SAVING.md). Step 6 is implemented
for ordinary PDFs and existing AcroForm content, with the validation gaps above.
The combined suite now passes 6/6 CTest entries, including the step 7 safety gates,
fifteen viewer cases, eleven XFA regressions, and independent PDF.js output checks
for additions and every form control. The final
interface screenshot, C++ formatting, Python/JavaScript syntax, and whitespace
checks pass. CI is configured but was not run remotely.

Dynamic XFA copies without additions are supported; requests with additions fail
with an error and retain the additions. Saved additions become PDF content on
reopen. No PDFium rebuild was needed, and GPL-3.0-only remains unchanged.

## Step 7 — Safety

- [x] Reject invalid/corrupted PDFs and malformed XFA with controlled errors.
- [x] Render a cyclic malformed AcroForm without a native/application crash.
- [x] Open a 1000-page corpus document and a generated 20 MiB input within RSS budgets.
- [x] Release the complete worker process tree and raster cache when closing.
- [x] Deny OS command execution and process creation through a kernel syscall filter.
- [x] Deny sockets and external host actions; verify no host listener connection.
- [x] Keep private host files/environment inaccessible; deny renderer file writes.
- [x] Apply 768 MiB writable-data, descriptor, file-size, CPU and tmpfs bounds.
- [x] Verify normal V8 threads inherit restrictions and normal form scripts still work.
- [x] Reject 1 GiB allocations and reserved-memory commitment in policy/script tests.
- [x] Enforce real deadlines for startup, field-event and save-event infinite loops.
- [x] Keep the host event loop responsive and recover after worker failure.
- [x] Retain additions after a crash; explicitly report unrecoverable native form edits.
- [x] Bound source copying, command lines, pending form events and raster conversion.
- [x] Store and reproduce GPL-3.0-only adversarial/malformed regression fixtures.
- [x] Run the existing forms, save, XFA persistence and independent-reader gates.

On 2026-10-07 the combined suite passed 6/6 CTest entries in 100 seconds. This
includes eleven controller safety data cases, seven native safety tests, fifteen
viewer cases, eleven prior XFA regressions and the strict independent-reader
round-trip gate. The first 1000-page safety run measured 41 MiB peak worker RSS;
every run enforces the documented host/worker budgets. No PDFium rebuild or new
external dependency was needed. A fresh default foundation configure/build and
smoke test also pass. See [SAFETY](docs/SAFETY.md) and
[ADR 0006](docs/adr/0006-worker-safety-limits.md).

This is a synthetic Linux x86-64 MVP safety gate, not a security certification.
Broader fuzzing, dependency monitoring, kernel/architecture coverage and native
form crash recovery remain release work. Remote CI was not run. GPL-3.0-only
remains unchanged; no commit or push was performed.

## Step 8 — Linux Preview Packaging

- [x] Add CMake install rules and a relocatable CPack TGZ archive.
- [x] Package the GUI, renderer, patched PDFium, desktop entry, icon and notices.
- [x] Remove development runtime paths from installed binaries and resolve packaged PDFium.
- [x] Build a separate Linux x86-64 Release artifact using system Qt runtime libraries.
- [x] Add repeatable relocated-package validation and Ubuntu preview artifact CI.
- [ ] Verify a fresh desktop installation with the complete MVP workflow.
- [ ] Resolve step 6 compatibility gaps and prepare complete corresponding source before publication.

Relocated package validation passed all 17 QtTest results, including native
AcroForm/XFA edits, text/signatures, saving/reopening and independent readers.
Installed GUI startup and host-runtime linkage checks passed. The complete
6/6 CTest suite passed again in 101 seconds, including security regressions.
Python syntax, C++ formatting, workflow YAML and whitespace checks passed.

The local archive is `dist/pdf-form-editor-0.1.0-Linux-x86_64.tar.gz`, built on
Fedora 42 against Qt 6.10.2. See [RELEASE](docs/RELEASE.md) for dependencies,
installation and the limited distribution compatibility. The package does not
include a compiler, SDK, Python, Node.js, PDF.js or test binaries. Remote CI and
public publication have not been performed. The final release remains blocked
by full XFA additions, AcroForm script conversion and foreground XFA validation.

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

## Native build helper — compile.sh

Added an executable Fedora/RHEL-family Linux x86-64 build helper. It detects
missing dependencies, confirms DNF installation (or accepts `--yes`), honors
`--no-install`, and checks Qt 6.4+, CMake/C++20 and Bubblewrap before native work.
It validates the cached PDFium manifest, patch/library hashes and runtime library
compatibility; matching builds reuse PDFium. `--rebuild-pdfium` forces a rebuild.
The script builds the Release desktop viewer and optionally creates the existing
portable archive with `--package`. Repositories and security settings are not
modified. RHEL package availability and modern toolchain requirements are reported
as errors rather than assumed; no clean RHEL-machine test has been performed.

Validation: Bash syntax and invalid-argument checks passed. Mocked dependency
checks verified refusal in noninteractive mode, `--no-install`, and the explicit
`--yes` DNF command/failure path without installing system packages. Local Fedora
`--no-install --package` built the application and produced the approximately
14 MiB archive. A subsequent `--no-install` run completed successfully and
reported PDFium reuse. Fresh PDFium rebuilding was not repeated for this helper.

## Docker host filesystem and initial file-picker folder

Prepared a writable bind mount of the local host root at
`/home/pdfeditor/host_fs`. Startup identifies the configured UID's host home from
host `/etc/passwd` (with a unique owned `/home` directory fallback), without a
`HOST_HOME` setting, and initializes Open, Save As and image pickers there.
Native desktop startup defaults to the user's ordinary home. Host UID/GID still
need to match the existing Compose configuration.

The prepared Compose configuration disables SELinux labeling for this container
instead of relabeling the host root. This gives the trusted GUI broad host-file
access under its UID; PDF workers retain their own isolated filesystems and
syscall restrictions. The Docker-compatible empty `/proc` and essential-device
configuration is applied consistently to workers and the development probe.

Host-home detection checks passed (spaces, invalid paths, directory-service
fallback and ambiguous accounts); the file-picker regression passed. Native and
Ubuntu Docker application builds succeeded; the full host CTest suite passed
6/6 in 109.73 seconds. Automatic approval review rejected
starting the writable host-root/SELinux-disabled deployment due to broad host
exposure; explicit approval has been requested. At that point the existing container had not
been replaced. A later user-launched deployment now has the requested root mount
and was verified by read-only Docker inspection; browser scaling checks passed.

## Docker container name

Compose now names the container `pdf-editor`, allowing `docker start pdf-editor`
and `docker stop pdf-editor`. The existing running container was renamed without
recreation, preserving its mounts and security configuration. A later user-launched
recreation applied the host-root mount.

## Docker browser scaling

Changed the noVNC landing-page default from remote resizing to local scaling.
The fixed Xvfb desktop now fits the browser viewport while preserving its aspect
ratio. Existing deployments can use
`http://localhost:8080/vnc.html?autoconnect=true&resize=scale` immediately without
recreation. Double-clicking the app title bar maximizes the Qt window, and the
noVNC fullscreen button / Firefox F11 removes browser chrome.

Headless browser checks passed against the running container at 1280x720 and
1920x1080: the rendered desktop fits without overflow and adjusts when the
browser viewport changes. The updated image uses this setting by default on
future creation; the running container was not replaced for this change.

## Docker lifecycle helper — docker.sh

Added executable start/stop/rebuild commands, plus build/status/logs/url and help.
The helper works from any directory, prints the scaling-enabled URL with the
published port, preserves existing configuration on start, and waits for health.
Rebuild validates cached native package hashes/version, builds the configured
image, then recreates the desktop session. A source-build option is available.
Build alone keeps the running session intact. Host files persist, but unsaved
edits must be saved before rebuilding.

Validation: Bash syntax and mocked lifecycle/error checks passed, including
first creation, stop, cached/source rebuild selection, cache integrity mismatch,
paths with spaces, custom ports, unhealthy startup and missing Docker access.
Live status and already-running start passed without disrupting the user session.
The real `build` path also passed, reused the native package, built the image and
printed the expected browser URL. No live stop/recreation was performed for this
helper validation, preserving the user's active document session.

## Fullscreen application startup

The desktop viewer now calls `showFullScreen()` on startup, filling the virtual
Docker desktop automatically. noVNC continues scaling the desktop to the browser;
Firefox F11 or the noVNC fullscreen button can also fill the physical screen.
The browser walkthrough now maps clicks through the fullscreen canvas and fitted
PDF page instead of the old window's fixed position.

Validation: the Ubuntu image compiled successfully and an isolated test container
showed a 1600x1000 application at desktop origin (0,0). The complete browser
walkthrough passed for AcroForm and XFA editing, checkbox/radio/dropdown selection,
clipboard paste, Save As and reopening; four outputs passed independent PDF.js
checks. Syntax/formatting checks passed. The user's live PDF session was not
recreated; save edits and use `./docker.sh rebuild` to apply the updated image.

## Remaining MVP steps

| Step | Status | Next milestone |
| --- | --- | --- |
| 2 — Prove XFA First | Complete for committed fixtures | Expand compatibility during forms work |
| 3 — Minimal Viewer | Complete for committed fixtures | Expand production-document compatibility |
| 4 — Forms | Complete for synthetic fixtures | Expand production-form and script compatibility |
| 5 — Added Content | Complete for synthetic fixtures | Expand combined XFA addition/save coverage |
| 6 — Save | Implemented; validation partial | AcroForm script fix, dynamic XFA additions and foreground XFA |
| 7 — Safety | Complete for synthetic Linux safety corpus | Broader fuzzing, compatibility and recovery |
| 8 — MVP Release | Linux preview packaging implemented; release gate partial | Clean desktop walkthrough, step 6 compatibility and source distribution |

Update this file as milestones land, including checks actually run and remaining
limitations. Do not infer broad PDF compatibility from the synthetic probe.

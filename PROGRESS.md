# MVP Progress

Last updated: 2026-10-08

## Roadmap Phase 1 — first reliability batch

- Opening preflight now prepares and validates the replacement snapshot before
  closing the current document. Missing/nonlocal/unreadable/oversized input or
  snapshot failure reports an error without discarding existing edits.
- Cached render deliveries honour cancellation, document replacement and native
  form cache invalidation.
- Added regressions for retained additions and AcroForm text, subsequent
  rendering/save/reopen, and cancellation/close before a cache-hit delivery.
- Local combined CTest: all six suites passed (105 seconds), including security,
  persistence and independent PDF.js checks. The 1000-page test measured 41 MiB
  peak worker RSS. Expanded AcroForm retention coverage also passed separately.
- Fresh local tarball and AppImage previews each passed all 24 QtTest results
  after relocation to a path containing spaces, including saved-output reader
  checks. These Fedora-built previews require glibc 2.39; they do not establish
  the RHEL 9 release baseline or validate Windows/macOS desktop behaviour.
- Roadmap Phase 1 remains open; broader compatibility, the AcroForm JavaScript
  dependency fix and the reported Windows desktop startup exit remain pending.

This batch reuses the installed patched PDFium dependency and is validated
locally before any further native artifact workflow is requested.

Desktop startup now opens a normal window instead of full screen. The window
explicitly requests the native title bar, minimize/maximize and close controls;
closing continues to use the existing unsaved-change confirmation.

Windows desktop follow-up: a user reports that Explorer launch exits while
PowerShell launch works. Added early Qt startup logging under
`%LOCALAPPDATA%\PDF Editor` and desktop error dialogs for Qt fatal/interface
load failures. The cause and double-click fix remain unverified; CI's offscreen
startup check does not reproduce this user's desktop environment.

## Current state

The distribution goal is
a Windows executable ZIP, Linux portable tarball/AppImage, and macOS app bundle.
Linux targets are RHEL 9, Fedora and Ubuntu 22.04/24.04+ on x86_64 and aarch64,
using a glibc 2.34 build baseline. ARM is newly added and its first native CI
validation is pending. Clean-desktop compatibility walkthroughs remain pending.

Linux native bundling includes Qt libraries, QML/plugins, patched PDFium,
Bubblewrap and Liberation fonts. Both formats build locally. Installed workers
explicitly mount bundled dependency libraries and fonts read-only inside their
existing sandbox, including Save As workers.
All six existing CTest suites passed. Both original tarball/AppImage payloads
passed relocation under paths containing spaces and all 21 viewer integration
checks, including form saves and independent reopening. Tests require running
outside the agent's outer sandbox, which blocks Bubblewrap namespace sockets.

The local Fedora previews in `dist/native-local/` require glibc 2.39 and cannot
run on RHEL 9. Package generation defaults to refusing dependencies newer than
glibc 2.34. The GitHub-hosted `native.yml` workflow now builds Linux inside a
Rocky Linux 9 environment and validates the tarball and AppImage outside the
build environment. Its Python 3.12, Qt 6.6.2,
fonts and glibc 2.34 were checked locally. The full baseline build passed all
69 upstream XML tests. Its tarball/AppImage passed the strict glibc 2.34 gate,
relocated GUI startup, normal-text and AcroForm/XFA edit/save/reopen checks,
malformed-document recovery, and independent PDF.js verification of both saved
generations. The GUI also starts with its bundled fonts in a minimal Ubuntu
22.04 environment with no Qt installed; PDF workers were tested on Fedora. Clean RHEL/Fedora/Ubuntu desktop walkthroughs remain pending.

Windows and macOS native worker launch paths, input handling and patched PDFium
recipes are implemented. Windows uses a zero-capability AppContainer and a
bounded Job Object; macOS uses a deny-default Seatbelt profile and resource
limits. Native GitHub jobs deploy Qt, run the real GUI offscreen, edit/save/reopen
normal PDFs and AcroForm/XFA fixtures twice, independently check saved fields
with PDF.js, and upload ZIPs only after these checks pass. Windows/macOS execution
is not locally verified. Mac Intel and Apple Silicon passed native CI in run
37781063288. Windows passed native CI in run 37783202566, including isolated
worker startup and independent verification of both saved generations. Mac candidates are
ad-hoc signed, not notarized; Windows candidates are unsigned. No GitHub release is published automatically. See [NATIVE-RELEASES](docs/NATIVE-RELEASES.md).

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

## GitHub native candidates — 2026-10-07

- [x] Replace the self-hosted Linux workflow with GitHub-hosted Linux, Windows
  x64, Intel Mac and Apple Silicon Mac jobs.
- [x] Build Linux against glibc 2.34, bundle Qt/QML/fonts and enforce the ELF baseline.
- [x] Build pinned Bubblewrap 0.11.0 against the baseline; verify its 64 MiB tmpfs
  on the host. RHEL 9's Bubblewrap 0.6.3 lacks the required `--size` option.
- [x] Port worker launch, input snapshots and source identity checks to Windows/macOS.
- [x] Deploy app-local MSVC runtime DLLs and Qt dependencies for the Windows ZIP.
- [x] Deploy Mac frameworks, normalize library paths, and ad-hoc sign native bundles.
- [x] Add portable edit/save/reopen checks and independent PDF.js checks for added
  text and native controls; validate both Fedora preview formats after relocation.
- [x] Pass all six Linux regression suites and Actionlint workflow validation.
- [x] Compile Windows broker/policy against MinGW Windows headers.
- [x] Build and validate local glibc 2.34 baseline artifacts; include a signed,
  checksum-pinned Rocky 9.0 GCC 11 unwinder to avoid newer vendor-backported symbols.
- [ ] Run the first Windows/MSVC and macOS GitHub jobs and clean-desktop walkthroughs.
- [ ] Finish production signing/notarization and complete corresponding-source packaging.

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

## Repository cleanup — 2026-10-07

Removed obsolete deployment files, launcher sources, browser tests and their
workflows. Native artifact builds use a Rocky Linux 9 GitHub Actions build job
and a separate Ubuntu validation job. Editor code, native packaging, dependency
pins, regression tests and development documentation remain. The file picker
starts in the current user's home directory. The editor rebuild, Actionlint
checks for all three workflows, shell syntax and whitespace checks passed.
The updated workflow awaits its first GitHub run.

## Windows dependency sync fix — 2026-10-07

The first Windows job failed before compilation because pinned depot_tools
invokes `git.bat`, while the runner supplies `git.exe`. Disabling depot_tools
auto-updates skips its normal Git wrapper bootstrap. The build recipe now
generates a quoted wrapper under `.deps/windows-tools`, adds it to the build
PATH, and runs a logged Git preflight before dependency sync. The pinned
upstream checkout is unchanged. Local checks passed Python compilation, wrapper
resolution through the pinned depot_tools parser (including a Git path with
spaces), missing-Git rejection, and whitespace validation. Windows execution
awaits the next GitHub run.

## CI runtime warning cleanup — 2026-10-07

Updated checkout, cache, Node/Python setup and artifact actions to verified
Node 24 versions. Replaced the Node 20 MSVC action with the installed Microsoft
developer-shell script and explicit environment export. Qt installation uses
Python 3.14; PDFium and packaging retain Python 3.12. The foundation job now
pins Ubuntu 24.04 instead of following the changing latest label. Workflow
lint and whitespace checks passed; native MSVC setup awaits the next Windows
GitHub run.

## Native candidate startup diagnostics — 2026-10-07

First macOS ARM and Windows jobs reach packaging but fail starting the isolated
worker; the supplied excerpts do not identify the underlying loader/sandbox
error. Worker failures now drain stderr before teardown and log process status,
exit codes and Windows hexadecimal codes in the validation transcript. Native
workers always report startup exceptions to stderr. Packaging distinguishes
intentional unsandboxed-worker rejection from a DLL/dyld loader failure and
preserves `worker-loader.log` and `native-smoke.log` in failure artifacts.

Mac deployment now selects only platform, image, icon and style plugins,
excluding unused SQL drivers with unavailable database libraries. A Windows
packaging variable collision that changed the ZIP name was corrected. Desktop
jobs now allow six hours; Intel Mac builds use four compiler jobs. XML-tested
PDFium is cached immediately, even if later desktop deployment fails. Mac ARM
queue capacity is controlled by GitHub. Native worker failures remain unconfirmed
until the improved logs from the next platform runs are available.

Validation: the editor rebuild and all six Linux regression suites passed.
Python helper checks verified intentional policy rejection, rejection of loader
failures, and preservation of failing smoke transcripts. Python compilation,
Actionlint and whitespace checks passed. Windows/macOS native execution remains
pending; no sandbox permissions were broadened based on these incomplete logs.

## Native diagnostics and Ubuntu CI namespace fix — 2026-10-08

Read all three supplied diagnostic archives and the integration test output.
Both Mac worker loader checks pass, followed by SIGABRT (signal 6) during
sandboxed startup. This does not yet identify the aborting library. Windows
also passes the loader check but exits without a broker/worker error trace.
The Ubuntu 24.04 integration job cannot configure Bubblewrap loopback and
reports `RTM_NEWADDR: Operation not permitted`; the subsequent test timeouts
are consequences of this failed sandbox startup.

Mac workers now apply the same Seatbelt profile internally after trusted
dynamic loading, before Qt/PDFium initialization and any document reads.
The sandbox is still verified and missing/invalid isolation fails closed.
This avoids imposing the document policy on trusted loader initializers;
confirmation of the SIGABRT fix still requires the next native run. Startup
phase diagnostics, Darwin crash-report capture and a separate trusted Windows
broker trace now survive failure.

The integration workflow permits unprivileged namespaces on its disposable
Ubuntu runner by changing the runner's AppArmor user-namespace sysctl, then
checks an isolated Bubblewrap launch before running tests. No installed-user
system policy is changed; worker namespaces, seccomp and read-only mounts
remain enforced.

Validation: local editor rebuild and all six Linux regression suites passed.
Actionlint, Python compilation, whitespace checks and a filtered crash-report
collection check passed. The Mac policy branch also passed a portable POSIX
compile check; this is not a macOS SDK/runtime test. Native platform confirmation
and the Windows broker cause still depend on the next GitHub run.

## Native startup fixes from the second diagnostics — 2026-10-08

The new Mac logs progress past loader startup and fail setting resource limits
inside Seatbelt. Resource limits are now installed before sandbox application
and verified afterward; no resource-control permission is added to the profile.
A POSIX test with stubbed Darwin sandbox APIs confirmed ordering, refusal outside
the sandbox, and rejection of an excessive CPU limit. This is not native Mac
runtime confirmation.

The Windows broker log identifies CreateProcessW error 203 (missing environment
variable). Its custom environment supplied only system-library paths; it now
also supplies LOCALAPPDATA, USERPROFILE and SystemDrive, needed for Windows
profile setup and AppContainer environment redirection. Qt/tool/plugin settings
and unrelated host environment variables remain excluded. The zero-capability
AppContainer, read-only input/runtime ACLs and bounded job remain unchanged.
Native Windows/Mac runs are required to confirm both fixes. PDFium source and
its build recipe are unchanged, so existing dependency caches remain reusable.

## GitHub monitoring and Darwin data-limit fix — 2026-10-08

GitHub CLI authentication is available; current jobs and completed-job logs can
be read directly. Run 37777568560 identified Darwin resource 2 (RLIMIT_DATA)
returning EINVAL. Apple XNU rejects a data ceiling below the existing VM map,
which includes trusted loader/shared-cache mappings. Mac policy now measures
that startup map and installs a hard ceiling with 768 MiB growth headroom,
verified after sandbox entry. This does not claim a 768 MiB resident-memory
limit. Native CI confirmation is pending.

## Windows private desktop startup — 2026-10-08

Latest broker trace confirms process creation and bounded-job assignment now
succeed, then the worker exits with 0xC0000142 (DLL initialization failure).
The worker links Windows graphics libraries whose initialization requires an
accessible window station/desktop. The broker now creates a separate private
station and desktop with ACLs for the current user and worker AppContainer SID,
and a low integrity label. No host desktop/clipboard ACLs are changed. Handles
remain alive until the worker exits. This is a candidate fix pending native CI.
The Linux build and relocated package validation jobs passed in run 37777568560.

### Native CI follow-up: trusted startup reservations

Run 37779644823 passed both Linux jobs. Windows compilation exposed an undefined
`DESKTOP_ALL_ACCESS` macro; the broker now uses the explicit documented desktop
rights mask. Mac ARM passed sandbox/resource verification but V8 aborted during
its empty Oilpan heap reservation. The Mac worker now initializes the trusted
PDFium runtime before measuring its VM baseline and applying Seatbelt; no PDF
bytes are read until policy verification succeeds. Native CI validation pending.

Run 37781063288 passed Mac Intel, Mac ARM, Linux build and Linux package
validation. Integration run 37775905657 passed all regressions and relocated
package checks. Windows still exits before main with `0xC0000142`, despite a
private desktop. Its pipe-based worker now uses the Windows GUI subsystem
with the normal main CRT entry point to avoid console initialization; CI pending.

Integration CI now cancels superseded runs on the same branch and saves the
PDFium dependency cache immediately after its own tests pass. A later application
test failure will no longer force the long dependency compilation on retry.
Workflow syntax validated with actionlint.

Windows run 37782130968 passed the AppContainer worker and all native form
smoke tests after switching the pipe worker to the GUI subsystem. PDF.js then
correctly rejected missing added text: the Windows offscreen Qt backend had no
font directory configured. Packaging now points that CI-only backend at Windows
Fonts, and native smoke explicitly rejects an empty font database. Pending CI.

Windows job in run 37783202566 passed normal-PDF added text, AcroForm and
dynamic XFA edits, both saved generations, malformed-document recovery and
independent PDF.js verification. Downloaded its artifact and verified SHA-256
and ZIP contents (editor EXE, worker, sandbox helper and Qt DLLs).

### All native artifact jobs passed

[Run 37783202566](https://github.com/Ewave267/pdf-editor/actions/runs/37783202566)
completed successfully on every target: Windows x64 ZIP, Mac Intel and Apple
Silicon ZIPs, and Linux x86_64 tarball/AppImage with separate Ubuntu validation.
The matching PDF Integration run 37783202498 also passed. All artifacts are
available in the native run. Clean-desktop user walkthroughs remain pending;
Windows is unsigned and Macs are ad-hoc signed, not notarized.

### Linux architecture expansion

Keep existing `x86_64` artifact names. Added `aarch64` (ARM64) tarball and
AppImage workflow targets, with native ARM Qt builds, PDFium XML tests and
separate Ubuntu ARM package validation. PDFium cross-compiles with the pinned
Chromium x86_64 compiler against a Rocky 9 ARM sysroot; it is cached only after
native ARM XML tests pass. Runtime inputs are separately checksum-pinned and
architecture-specific caches/artifact names prevent mixing payloads. Existing
Linux seccomp already supports AArch64. Python compilation, shell syntax and
actionlint pass. First ARM CI validation pending.

First ARM run 37785849699 reached sysroot provisioning but Rocky mirrorlist
resolution failed because `$rltype` was undefined in the empty install root.
Copy the baseline repository variable directories into the ARM sysroot before
installing packages. Retry pending.

ARM retry 37787302766 compiled 4,802 steps but failed at the final link because
GCC startup objects and link libraries were absent from the sysroot. Install
ARM gcc/gcc-c++ and check the required startup/runtime files before compiling.
A local link test with the pinned compiler produced a valid AArch64 ELF.
Added architecture selection and desktop opt-out for manual workflow runs;
`[linux-aarch64]` commits retry ARM without canceling or repeating other targets.
Mac ARM and integration passed in the preceding run. ARM validation pending.

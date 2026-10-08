# Step 7 — Untrusted document safety

On Linux, all PDF parsing, native form handling, document JavaScript, rendering and native
saving run in Linux Bubblewrap workers. A missing sandbox or failed kernel policy
returns an error; there is no unsandboxed fallback. The Qt/QML application receives
bounded metadata, field text, rendered images and saved PDF bytes.

The policy and validation details below describe Linux. New Windows candidates
use an AppContainer with no capabilities and a bounded Job Object; new macOS
candidates apply a deny-default Seatbelt profile inside the worker after trusted
dynamic loading, before document reads or PDFium initialization, and enforce
resource limits. Both reject
direct worker execution without their sandbox. These ports await native CI
validation and do not claim Linux's tested seccomp/resource-limit coverage.
In particular, Darwin's data limit does not establish the same bound on anonymous
memory mappings. See [NATIVE-RELEASES](NATIVE-RELEASES.md).

## Boundaries

Workers have separate user, PID, network and other namespaces, no capabilities,
a cleared environment, read-only runtime/fonts and a read-only private copy of the
selected PDF. The user's home, arbitrary local files, host environment variables,
network interfaces and destination folder are not exposed. Only fixed sandbox
values (`HOME=/tmp`, `LANG=C.UTF-8`, `LD_LIBRARY_PATH=/pdfium`) enter the environment.
The renderer and save worker have no writable host output mount.

`WorkerPolicy` installs `no_new_privs` and a seccomp filter before Qt/PDFium starts
threads or reads document bytes. The filter is synchronized across any existing
threads, and new threads inherit it. It denies execution, process creation, sockets,
namespace/mount changes, tracing, cross-process memory access, BPF, performance
profiling and io_uring creation. Ordinary V8 threads are permitted and inherit
these restrictions; `clone3` returns `ENOSYS` so glibc can use restricted `clone`.
The filter checks the syscall architecture and rejects the x32 ABI on x86-64.

The renderer/save policy also denies writable file opens and filesystem mutation.
Saved PDFs and PNGs are returned through pipes. PDFium's external-action callbacks
provide no filesystem, process or network capability; URL, file-picker and form
submission requests are denied. Document alerts do not open blocking dialogs,
and timers remain unsupported. Saved document actions remain document data,
not instructions executed by the GUI.

The development XFA probe uses the same process/network/memory restrictions, but
permits file writes to its private `/output` artifact mount. It never receives a
user-selected destination directory or unrestricted host filesystem access.

## Limits and failure handling

| Resource | Enforced limit |
| --- | --- |
| Source and saved PDF | 64 MiB each; source snapshot copying is bounded |
| Added-content PDF | 32 MiB |
| Pages | 1–2000 |
| Page dimensions | 1–14400 points per dimension |
| Raster | Width 96–2400 pixels, height at most 12000, at most 16 million pixels |
| Cached host rasters | 64 MiB |
| Pending form commands | 128; excess input returns a busy error |
| Native command line | 64 KiB, read into a fixed buffer |
| Native field text | 128 KiB; submitted text at most 4096 UTF-16 units |
| Native responses | 96 MiB host receive bound; saved PDF independently checked |
| Worker writable data | 768 MiB hard `RLIMIT_DATA` limit |
| Worker descriptors | 128 |
| Worker file output | 64 MiB per file; renderer/save writable opens are denied |
| Private `/tmp` | 64 MiB tmpfs |
| Worker CPU | 300 seconds cumulatively; probe launcher lowers this to 15 seconds |
| Host deadlines | Startup 15 seconds; render/form request 10 seconds; snapshot/save stage 30 seconds |
| Core dumps | Disabled in native workers |

The data limit bounds writable heap/anonymous mappings, including tested
`mprotect` commitment of a previously reserved range. It permits V8's large
`PROT_NONE` virtual-address reservation. It is not a strict cap on total RSS,
which also includes runtime code and other mappings. Raster dimensions are
checked as finite bounded numbers before conversion or allocation.

Invalid requests return controlled errors. Unresponsive or crashed workers are
terminated without blocking the host event loop. Busy/saving state clears,
source files remain unchanged, and a fresh document can subsequently open.
Application-owned additions survive a renderer failure until the document is
explicitly closed or replaced. Native in-memory form edits cannot be recovered
after that worker dies; the error explicitly reports this loss. Autosave and
crash recovery are not included.

The GUI presents document errors and paths as plain text. Snapshot copying opens
and checks the actual regular-file descriptor, uses nonblocking open to avoid a
replacement FIFO, and stops if a source grows beyond the input bound.

## Reproducible checks

Use the combined build described in [VIEWER](VIEWER.md):

```sh
cmake --build build-viewer --parallel 4
ctest --test-dir build-viewer --output-on-failure
ctest --test-dir build-viewer -L security --output-on-failure
```

`viewer-safety` tests the actual application-facing document controller:

- Reject invalid PDF bytes, a broken catalog and malformed XFA, then recover.
- Reject a sparse oversized source before starting a worker.
- Open 1000 pages, render 40 sampled pages, bound worker and host RSS, clear the
  raster cache and verify the entire worker process tree exits on close.
- Bound the form queue and release it when closing.
- Kill a renderer with unsaved forms/additions; retain additions and report form
  loss rather than silently clearing edits.
- Enforce actual startup deadlines for infinite AcroForm and XFA scripts while
  a host heartbeat timer continues firing.
- Enforce an XFA exit-event deadline and an AcroForm save-event deadline; failed
  saving creates no output, clears saving state and retains additions.

`worker-safety` validates the native policy and adversarial corpus:

- Use the same compiled syscall policy to attempt `execve`, `execveat`, fork,
  process clone, IP/Unix sockets, input writes and file creation. Check inherited
  thread restrictions, host-secret/environment isolation and allocation limits.
- Commit a 1 GiB reserved mapping and allocate a 1 GiB typed array from both
  JavaScript engines; verify denial without disabling normal scripts.
- Execute actual AcroForm/XFA host-access attempts, read their audit field, verify
  Node/process/environment/network globals are absent, preserve host/source
  sentinels, and check that a real host listener receives no connection. The
  supported XFA URL request reaches its denied host callback.
- Verify values set before script exceptions and exact results of 100000-iteration
  scripts; the same documents still render.
- Render a cyclic malformed AcroForm safely; reject tiny geometry and excessive
  raster aspect ratios without overflowing an integer or allocating a huge image.
- Open a generated 20 MiB, 1000-page file within a 384 MiB child RSS test budget;
  reject 2001 pages and oversized native requests.
- Regenerate every committed safety fixture byte-for-byte.

The 2026-10-07 combined suite passed all six CTest entries on this Linux x86-64
workspace. The 1000-page controller test measured a 41 MiB peak worker RSS in
its first run. Its explicit budgets remain checked on every run. The combined
suite takes about 100 seconds because it exercises real deadlines. Existing
forms, saving, XFA persistence, isolation and independent PDF.js gates also pass.
Remote CI is configured to run these tests but was not executed in this session.

## Scope

This establishes reproducible MVP safety controls and regression coverage for
original synthetic fixtures. It is not a security certification or proof against
all PDFium/V8/kernel vulnerabilities. The filter is a capability denylist rather
than a complete syscall allowlist. Read-only system runtime/font files are
intentionally visible inside the sandbox. Broader production documents,
continuous fuzzing, dependency vulnerability monitoring, kernel/architecture
coverage and crash recovery remain release work. Existing full-XFA additions
and AcroForm script-conversion limits are unchanged; see [FORMS](FORMS.md).

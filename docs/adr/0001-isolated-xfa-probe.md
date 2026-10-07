# 0001 — Isolated PDFium XFA probe before the viewer

Date: 2026-10-06

Status: Accepted for the development probe; desktop integration is not yet proven.

## Context

[MVP step 2](../MVP.md) requires proving XFA interaction, JavaScript, and exact
save/reopen behavior before building the UI. Document scripts must not gain
unrestricted access to the host. The repository initially had no PDF library.

## Decision

Build PDFium `157.0.8086.0` from pinned revision
`2fd6cff57d9412cc42ef1a7e4e0a59b13a1e7cec` using
`tools/build_pdfium.py`. Apply the project's XML/XFA persistence patch and pinned
shared-library/V8 initialization patches. Keep sources and the generated
`.deps/pdfium-patched/` package outside version control, retaining license
notices and a manifest of source revisions and hashes. Configuration never
downloads dependencies and rejects an unpatched package.

The original checksum-pinned binary experiment exposed packet-array whitespace
corruption and single-stream edit loss. `cmake/FetchPdfium.cmake` remains available
for historical investigation; that binary cannot satisfy the milestone. The
source patch preserves text without normalization and saves the current XDP
state for single-stream documents. Details are in [XFA-PROBE](../XFA-PROBE.md).

Keep PDFium headers and handles in `src/pdf/XfaProbe.cpp`, behind the small
application-facing `ProbeOptions` / `runXfaProbe` interface. Use one thread and
close pages before the form environment, the document, and the library.

Run the native worker through `tools/run_xfa_probe.py` in Bubblewrap with
separate user, mount, PID, network, IPC, UTS, and cgroup namespaces, no
capabilities, and a cleared environment. Expose read-only system runtime/fonts,
the worker/library, and one input document. Give it a fresh writable staging
directory and private temporary storage. Host callbacks deny document file,
URL, email, submission, and other external actions. Enforce a wall-clock
deadline, CPU limit, file-size limit, descriptor limit, and no core dumps.
Failure to start the sandbox is a failure, never a reason to run unconfined.

Publish result files only after exact value verification succeeds. Refuse an
existing destination directory. The input is mounted read-only. The native
binary is an internal worker, not the supported user entry point.

Use original synthetic GPL-3.0-only fixtures whose XML source and deterministic
PDF generator are committed. Verify both initial and event-driven JavaScript
results rather than merely checking that V8 is present. Require exact
persistence across two save cycles for both XFA encodings. Use pinned PDF.js as an independent reader to verify saved XFA layout and field
values. Run the strict milestone gate together with regression/isolation tests
in CI; PDF.js is a validation dependency, not the application engine.

## Alternatives

- The prebuilt binary makes the initial experiment cheaper, but cannot preserve
  exact XFA values. A pinned source build gives the required control over fixes
  at the cost of a substantial first-build toolchain and compile time.
- A PDFium package without XFA/V8 cannot meet the current requirement.
- In-process scripting in a future Qt application would expose application
  state and increase crash impact. This experiment runs in a separate process.
- Trimming saved field values or flattening the form would obscure data loss or
  change document behavior; neither qualifies as an exact round trip.

## Consequences

The probe currently runs only on Linux x64 with Bubblewrap and unprivileged
user namespaces available. Other desktop platforms need their own isolation
design. The fixture has one page, known coordinates, and printable ASCII input;
this is not a general PDF editor or evidence of real-world XFA compatibility.
Timers, multipage relayout, passwords, and advanced script interactions are
outside this probe. Namespace isolation does not replace step 7's future
fuzzing, memory limits, seccomp policy, and security review.

The strict milestone gate passes for the committed synthetic fixtures. This
permits starting the minimal viewer while expanding compatibility coverage in
the forms milestone. It does not establish arbitrary XFA compatibility or
automatic dependency recalculation beyond the verified field-exit event.

## References

- [PDFium form-fill API](https://pdfium.googlesource.com/pdfium/+/main/public/fpdf_formfill.h)
- [Upstream V8 initialization example](https://pdfium.googlesource.com/pdfium/+/main/samples/simple_with_v8.cc)
- [Pinned binary release](https://github.com/bblanchon/pdfium-binaries/releases/tag/chromium%2F8086)

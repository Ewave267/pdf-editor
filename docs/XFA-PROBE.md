# Step 2 — XFA probe and persistence fixes

Date: 2026-10-06

Step 2 passes for the committed synthetic dynamic XFA fixtures. The probe opens,
renders, edits, executes initial/field-exit JavaScript, and preserves exact input
and calculated values across two save/close/reopen cycles. An independent,
pinned PDF.js reader opens and lays out both saved generations for both XFA
encodings and checks their field values. No editor UI has been started.

## Reproduce

Requirements: Linux x64, a C++20 compiler, CMake, Ninja, Git, Python 3.9+,
Node.js 24+, npm, Bubblewrap, and permission to create unprivileged user
namespaces. The first PDFium source build requires network access, substantial
free disk space, and time to download Chromium's toolchain and compile V8.
Nothing is installed system-wide; build logs are in `.deps/logs/`.

```sh
python3 tools/build_pdfium.py --jobs 4
npm ci --prefix tests/reader --ignore-scripts
cmake -S . -B build-xfa -DCMAKE_BUILD_TYPE=Debug \
  -DPDF_EDITOR_BUILD_XFA_PROBE=ON -DPDFium_DIR="$PWD/.deps/pdfium-patched"
cmake --build build-xfa --parallel
ctest --test-dir build-xfa --output-on-failure
```

The source build runs upstream XML-element unit tests before producing the
patched package. CTest runs the foundation smoke check, eleven Python regression
and isolation tests, and the strict independent-reader milestone gate. CI runs
all these gates on pushes and pull requests and caches the patched package by
build recipe/patch hashes. Remote CI execution has not been verified here.

Run the supported entry point directly:

```sh
python3 tools/run_xfa_probe.py \
  --worker build-xfa/xfa-probe-worker --pdfium .deps/pdfium-patched \
  --input tests/pdfs/xfa-javascript/calculation.pdf \
  --output build-xfa/probe-results --value saved-value
```

A fresh output directory contains `saved.pdf`, `resaved.pdf`, and three PPM
renders (before edit, edited, and first reopen). The second saved PDF contains
`VALUE-again`. Existing output directories are rejected. Failed verification
publishes no files. Do not invoke `xfa-probe-worker` outside the sandbox.

In this workspace, add `-DCMAKE_CXX_COMPILER=/usr/bin/c++` to bypass the
read-only default ccache directory. Bubblewrap namespace setup requires approved
execution outside the nested tool sandbox here; the worker retains its own
Bubblewrap isolation. There is no unconfined fallback.

## Validation and scope

Both encodings initialize PDFium, report `XFA_FULL`, load the XFA environment,
read `original` and `JS:original`, and render a nonblank 612 × 792 page including
form widgets. Editing fires the input's exit script and updates the calculated
field. Every original document, page, and form handle is closed before reopening.
Both generations retain the exact edited input and `JS:` calculated result.

| XFA encoding | First reopened input | Second reopened input | Independent reader |
| --- | --- | --- | --- |
| Packet array (`calculation.pdf`) | `saved-value` | `saved-value-again` | PDF.js exact values and page layout pass |
| Single XDP stream (`single-stream.pdf`) | `saved-value` | `saved-value-again` | PDF.js exact values and page layout pass |

Additional native regressions verify leading/trailing spaces, quotes, `<`, `>`,
and `&` across both save cycles and encodings. The upstream XML unit test verifies
embedded newlines and tabs remain unchanged. Changing the fixture's JavaScript
prefix to `NO:` makes the probe fail, so the event check observes executed
JavaScript rather than a hardcoded success marker. Input fixture hashes remain
unchanged during successful saves.

The fixture's scripted URL request is denied. Tests also verify hidden host
files and environment, read-only input, denied host network access, fail-closed
sandbox startup, and deadline enforcement.

Current coverage is one-page synthetic dynamic XFA and printable ASCII editing
at known widget coordinates. PDF.js independently parses and lays out the saved
XFA controls; it does not execute the form's JavaScript. Adobe Acrobat, arbitrary
real-world/static XFA, timers, Unicode editing, and multipage relayout have not
been verified. The explicit field-exit handler is proven; automatic dependency
recalculation requires separate coverage. These limitations belong to expanded
forms support and do not prevent starting the minimal viewer in step 3.

## Persistence fixes

The initial checksum-pinned binary experiment exposed two defects. They are
fixed in [the source patch](../third_party/pdfium/patches/0001-xfa-persistence.patch),
which is applied to pinned PDFium revision
`2fd6cff57d9412cc42ef1a7e4e0a59b13a1e7cec`.

**Packet-array corruption:** PDFium inserted formatting newlines in XML elements
and regenerated XFA form text. The saved input became `\nsaved-value`. The patch
removes serializer-generated formatting whitespace while retaining original
text nodes, spaces, and XML entity encoding. Values are never trimmed to pass.

**Single-stream edit loss:** PDFium's XFA save path handled packet arrays only.
It returned failure for an XDP stream while the caller still saved the original
XML. The patch resolves indirect `/XFA` objects, exports current complete XDP
state for streams, and propagates export failures instead of reporting a stale
save as successful. It retains dynamic XFA rather than flattening the document.

The build recipe uses pinned shared-library/V8 initialization tooling plus a
small V8 TLS patch adjusted for this revision. It produces an explicit
`PDF_EDITOR_PDFIUM_PATCHSET=1` package with source/patch/library hashes and
upstream/dependency license notices. CMake rejects the old unpatched package.
See [the integration decision](adr/0001-isolated-xfa-probe.md) and
[third-party build notes](../third_party/pdfium/README.md).

# PDFium persistence patch

`patches/0001-xfa-persistence.patch` applies to PDFium revision
`2fd6cff57d9412cc42ef1a7e4e0a59b13a1e7cec` (the pinned `chromium/8086` source).

It removes generated formatting whitespace from XML datasets and XFA form
serialization while retaining the original text nodes. It also serializes the
current complete XDP state for single-stream XFA, resolves indirect `/XFA`
entries, and returns save errors instead of silently exporting stale data.

The patch updates the upstream XML serializer expectations and adds an exact
whitespace/entity-preservation test. Application integration tests verify both
encodings, exact field values, escaped characters/spaces, and repeated saves.

`patches/0002-v8-shared-library-tls.patch` adapts the distributor's Linux TLS
definition to the pinned V8 revision.

`tools/build_pdfium.py` downloads pinned depot_tools and binary-distributor
build tooling, syncs the pinned upstream dependencies, applies the shared
library/V8 initialization patches and this patch, and builds the library and
upstream XML unit tests. It installs nothing system-wide. Generated files and
build logs live under `.deps/`, and the usable package is `.deps/pdfium-patched/`.

The patched package includes a build manifest with revisions and SHA-256 hashes
and retains upstream/dependency notices together with the project and
distributor licenses. Project modifications use the repository's GPL-3.0-only
license. This repository does not vendor the complete PDFium or V8 source tree.

# Release source distribution

`tools/package_dependency_sources.py` prepares a local source archive for the
application and its pinned PDFium/V8 build. Run it after committing the application
changes and building/testing the pinned dependency:

```sh
python3 tools/package_dependency_sources.py
```

The default output is `dist/sources/pdfium-source.tar.gz` plus a SHA-256 sidecar.
`SOURCE-MANIFEST.json` in the archive records every included file's hash, each Git
checkout's revision and modified status, the compiled dependency manifest and all
pinned gclient retrieval inputs. Git metadata, credentials, caches and compiled
outputs are omitted. Modified dependency source files are included as built;
application changes must be committed so new source files cannot be omitted.
Source checkouts must match the recorded PDFium, V8, distributor and depot pins.
Escaping source paths/symlinks, missing tracked files and overlapping checkouts
fail rather than producing an apparently complete source package.

The archive contains the application, modified PDFium/V8, all checked-out Git
dependencies, the distributor patches and depot/build scripts. The original
`tools/build_pdfium.py`, `.gclient`, `.gclient_entries`, DEPS, patches and GN args
record the pinned rebuild procedure. Git history is omitted. For a network rebuild, copy the application tree without
its archived `.deps` folder to a clean directory, then run the normal bootstrap
to fetch pinned checkouts/tool binaries and apply the included patches. Keep the
archived modified dependency files as the reference source snapshot. Running
Git bootstrap directly inside the populated metadata-free `.deps` snapshot will
fail because it is not a set of Git repositories.
This is a source-distribution artifact, not an offline compiler/toolchain bundle.
Native Windows/macOS SDKs and cross-build sysroots remain platform prerequisites.
A package created on one host does not attest that another platform was built or
tested successfully.

## Coverage required for each binary release

| Component | Source evidence | Remaining action |
| --- | --- | --- |
| Application | Committed application files in the archive, plus the preview's exact-commit source ZIP | Match the binary build commit. |
| PDFium, V8 and source dependencies | Local archive with all checked-out Git sources, modified files and rebuild scripts | Attach an archive matching each binary's dependency manifest; retain all platform-specific patches. |
| Windows/macOS Qt | Existing native jobs collect the five matching upstream module archives | Attach all matching module sources with the binary release. |
| Linux Qt and bundled distribution libraries/fonts | Binary packages/notices and source URLs are recorded by existing packaging | Obtain exact corresponding source RPMs and distribution patches/build inputs for the actual RHEL baseline package closure; upstream Qt archives alone do not cover it. |
| Bubblewrap, AppImage runtime and pinned GCC unwinder | Existing source pins/rebuild scripts and runtime notices | Include the exact corresponding source/build inputs alongside the binary release and check the complete runtime dependency closure. |
| Platform SDKs/tool binaries | Pinned gclient/CIPD retrieval inputs, toolchain/build manifests | Document prerequisites and apply each component's redistribution terms. |

A complete native source distribution remains a release gate until the matching
Qt/distribution/runtime sources are assembled and checked. A source URL, notices
file or manifest alone is not the corresponding source itself. The local archive
deliberately states its narrower scope; it must not be advertised as satisfying
all source obligations for every native artifact.

## Local candidate evidence — 2026-10-20

`dist/sources/pdfium-source.tar.gz` contains 85,616 files from 37 source checkouts
for application commit `4c3719d`, matching the locally packaged candidate. Its
SHA-256 sidecar and every file hash in SOURCE-MANIFEST.json were checked. Five
matching Qt 6.11.2 module source archives and SHA256SUMS are in
`.deps/release-inputs/qt-sources/`. These local files have not been published.
The runtime/distribution source rows above remain incomplete.

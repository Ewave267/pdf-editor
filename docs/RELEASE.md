# Releases

See [NATIVE-RELEASES](NATIVE-RELEASES.md) for the GitHub Actions workflow,
download instructions, compatibility baseline and platform limitations.

The workflow produces Windows x64 ZIPs, Linux x86_64 portable tarballs and
AppImages, and separate Intel and Apple Silicon macOS app ZIPs. Packages include
the editor and its runtime dependencies. No release is published automatically.

Before a public release, complete clean-desktop testing and corresponding-source
distribution for the application and bundled dependencies. Preserve all license
notices. The application is GPL-3.0-only. Existing compatibility gaps in
[FORMS](FORMS.md) and [SAVING](SAVING.md) remain release gates.

## Roadmap implementation gates (2026-10-10)

`VERSION` is the single candidate-version source for CMake, the executable's
version output and native archive names. Native validation writes
`performance.json` with platform/Qt details and open/render timings; compare
results on equivalent machines and documents, rather than treating CI timings
as an interactive-performance guarantee.

The integration workflow runs malformed-input mutation cases under the kernel
sandbox and a separate ASan/UBSan gate for host document/UI code. The prebuilt
Qt libraries and PDFium/V8 are not instrumented by the host sanitizer option.
Dependabot tracks Actions and the independent npm reader. The scheduled
Dependency security workflow queries OSV for PDFium, desktop Qt and the reader;
it also queries the explicitly tracked V8 revision. Builds verify that the
actual V8 checkout matches this pin. Network failures
and advisory findings fail the check. OSV coverage is incomplete: review Qt and Linux distribution
security notices and bundled runtime dependencies separately.

The local advisory check found `V8-FRESHNESS`, an upstream update-policy finding
for the pinned V8 revision. This requires review/update before a production
release; it is not a confirmed application exploit. Candidate artifacts remain
unsigned and must not be labelled production-ready while this gate, signing/
notarization or clean-desktop validation is outstanding.

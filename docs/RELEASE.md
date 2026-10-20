# Releases

See [NATIVE-RELEASES](NATIVE-RELEASES.md) for the GitHub Actions workflow,
download instructions, compatibility baseline and platform limitations.

The workflow produces Windows x64 ZIPs, Linux x86_64 portable tarballs and
AppImages, and separate Intel and Apple Silicon macOS app ZIPs. Packages include
the editor and its runtime dependencies. Successful complete native builds
automatically publish a **Development preview** GitHub prerelease. These are
explicitly unstable and never marked as GitHub's **Latest** stable release.
See [preview publishing](NATIVE-RELEASES.md#automatic-development-previews).

Before a stable production release, complete clean-desktop testing and corresponding-source
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
and advisory findings fail the check, except for the reviewed policy notice below.
OSV coverage is incomplete: review Qt and Linux distribution
security notices and bundled runtime dependencies separately.

The local advisory check found `V8-FRESHNESS`, an upstream update-policy finding
for the pinned V8 revision. This requires review/update before a production
release; it is not a confirmed application exploit. On 2026-10-19, both upstream
PDFium main and the latest published `chromium/8111` branch still selected V8
`0d546234ecf0af2032e958300ef8aeb14bc716c9`. For this exact pin only,
the dependency CI check reports `V8-FRESHNESS` as a visible warning, retains the
complete finding in its report, and marks it under `nonblocking_policy_notices`.
All other findings and scan failures still fail CI. Any V8 pin change requires
reviewing this exception again. This CI exception does not clear the production
release gate.

Candidate artifacts remain
unsigned and must not be labelled production-ready while this gate, signing/
notarization or clean-desktop validation is outstanding.

## Local reliability candidate — 2026-10-20

See [SECURITY-REVIEW](SECURITY-REVIEW.md), [SOURCE-DISTRIBUTION](SOURCE-DISTRIBUTION.md)
and [PERFORMANCE](PERFORMANCE.md) for the current evidence and remaining gates.
Desktop Qt is pinned to 6.11.2, with matching module source archives collected
locally. The relocated Linux host-validation package requires glibc 2.39; it does
not replace the RHEL 9 baseline builds. Windows/macOS/ARM native workflows were
not run because the user requested local commits only.

The AppImage continuous input changed and correctly failed its checksum check.
Reviewed the single upstream change (temporary-path mapping for reproducible
builds), matched the successful official build's head revision and release-asset
digests, and updated both architecture pins. Hash enforcement remains enabled;
ARM execution remains an external gate. Cryptographic PDF signing/validation is
excluded from this implementation at the user's request.

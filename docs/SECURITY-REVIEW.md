# Dependency review — 2026-10-19

The OSV check completed for pinned PDFium/V8, desktop Qt base/declarative and the
independent reader. It returned only the reviewed V8 freshness policy notice;
`npm audit --prefix tests/reader --ignore-scripts --json` reported zero advisories.
These results do not clear the production gate or replace vendor notice review.

## Qt vendor notices

The desktop workflow now pins Qt 6.11.2 after a relocated Linux package smoke
check. The earlier 6.8.3 pin was affected by vendor notices which OSV's query did
not report. Relevant reviewed notices include:

- [CVE-2026-6210](https://www.qt.io/blog/security-advisory-type-confusion-and-heap-buffer-overflow-vulnerability-in-qt-svg-marker-handling): high-severity SVG marker/mask type confusion and recursion. The application offers raster image import only; the tested raster decoder boundary rejects disguised SVG before selecting any Qt image plugin. Packaged SVG libraries alone do not establish reachability or safety.
- [CVE-2025-5992](https://www.qt.io/blog/security-advisory-recently-reported-denial-of-service-issue-in-qcolortransfergenericfunction-impacts-qt): crafted ICC profiles can cause denial of service in affected Qt 6.8.3. Raster image metadata requires review/update even when SVG import is blocked.
- [CVE-2026-78253](https://www.qt.io/blog/security-advisory-cve-2026-78253) and [CVE-2026-79616](https://www.qt.io/blog/security-advisory-cve-2026-79616): XML stream recursion and Qt Quick SVG-path parsing. Document XML stays in native workers; the interface must not interpret field labels or values as markup/path strings. Existing native form/choice/validation lists explicitly use plain text.
- [CVE-2025-3512](https://www.qt.io/blog/security-advisory-qtextmarkdownimporter-hbo) and [CVE-2025-12385](https://www.qt.io/blog/security-advisory-improper-validation-of-img-tag-size-in-text-component-parser): untrusted Markdown and rich-text image dimensions. The editor does not import Markdown; PDF-derived text must remain plain text.

The complete [Qt security notice index](https://www.qt.io/blog/tag/security/page/0)
and every bundled module/runtime dependency need matching-version review. Qt
6.11.2 fixes several notices and preserves the current macOS 13 baseline; Qt 6.12
requires macOS 14.4. Do not silently drop the supported platform baseline or label
an untested replacement runtime production-ready. Platform builds remain local-only
per the user's request; native Windows/macOS runtime validation is outstanding.

## Distribution libraries and source coverage

The local host Qt 6.10.2 measurement/build is separate from the proposed Windows/macOS 6.11.2 runtime and
the RHEL-baseline Linux Qt packages. A passing host test says nothing about vendor
backports in another platform's library closure. Check the exact package manifest
against the matching distribution advisories, include source RPMs/patches and
rerun native validation after remediation. See SOURCE-DISTRIBUTION.md for source
archive coverage and explicit omissions.

Production dependency acceptance remains open until reachable issues are fixed
or a reviewed, evidence-based mitigation covers the actual shipped artifacts.

Local Qt 6.11.2 compilation and relocated native edit/save/reopen, page export,
merge, rotation, flattening and malformed-input smoke checks passed. This local
package requires glibc 2.39 and is a host validation artifact, not the RHEL 9
production artifact. Direct build-tree execution cannot use the SDK outside the
worker mount; relocated packages provide the required private runtime closure.
Windows/macOS builds and exact shipped-module advisory review remain open.

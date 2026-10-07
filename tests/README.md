# Tests

The foundation has a CTest smoke check that verifies the executable starts and
prints its project identity. Run it with:

```sh
ctest --test-dir build --output-on-failure
```

The PDF corpus lives in `pdfs/`, grouped into normal, AcroForm, static XFA,
dynamic XFA, XFA JavaScript, and malformed documents. Original synthetic
fixtures are described in [the corpus manifest](pdfs/README.md). No PDF
compatibility is claimed by the foundation smoke check.

With the opt-in XFA build, CTest also provides:

- `xfa-regressions`: eleven tests covering exact persistence in both XFA
  encodings, repeated saves, spaces and XML-sensitive characters, script result
  verification, invalid input, preservation of existing files, sandbox isolation,
  deadlines, and deterministic fixtures.
- `xfa-roundtrip`: the strict step 2 gate. Both fixtures must survive two
  edit/save/close/reopen cycles with exact input and calculated values. Pinned
  PDF.js independently opens and lays out all four saved PDFs and checks values.

Run all checks with `ctest --test-dir build-xfa --output-on-failure`.
See [XFA-PROBE](../docs/XFA-PROBE.md) for complete setup. Sandbox startup failure
fails the suite; there is no unsandboxed fallback. The XML serializer's upstream
unit tests are also run by the PDFium build recipe.

With `PDF_EDITOR_BUILD_VIEWER=ON`, `viewer-integration` also runs four Qt/QML
integration cases for rendering/thumbnail identity, document replacement and
close, malformed/XFA input, sandbox failure, close during rendering, file dialog,
zoom/fit, typed page navigation, thumbnail clicks and scrolling. QtTest uses
`QT_QPA_PLATFORM=offscreen` and `QT_QUICK_BACKEND=software` automatically under
CTest. See [VIEWER](../docs/VIEWER.md) for setup and limits.

For each future fixture, record its source, redistribution license, expected
behavior, and any privacy review. Add documents that expose bugs as regression
fixtures when licensing and privacy permit.

# United States production XFA corpus

`irs-f14653.pdf` is the unchanged IRS Form 14653, revision March 2025, downloaded
from the [official IRS PDF](https://www.irs.gov/pub/irs-pdf/f14653.pdf).
[SOURCE.json](SOURCE.json) pins provenance, bytes, SHA-256, admission expectations
and operations that have not been checked. This is an unsubmitted blank template;
no taxpayer data has been added.

The [IRS content policy](https://www.irs.gov/about-irs/use-of-content-from-irsgov)
permits copying federal-employee content without copyright restriction and asks
for credit. Source: Internal Revenue Service, United States Department of the
Treasury. The original government form is included solely as a compatibility
test fixture. The IRS does not endorse, sponsor or authorize this project.
The fixture's public-domain status is separate from the project's code license.
No third-party copyright notice was found in the extracted XFA layout text.
Official names are retained in the unchanged form and are not product branding.

## Admission regression

`WorkerSafetyTests.test_irs_production_xfa_corpus` runs in the existing
`worker-safety` CTest/CI gate without network access. It checks source size/hash,
native form type, all three letter-size pages, PNG rendering of every page and
source-byte preservation. Pinned PDF.js 6.4.299 independently verifies dynamic
XFA layout, dimensions, per-page control counts (24/10/22), selected field names
and empty/unchecked control values. Node/PDF.js setup follows [tests](../../README.md).

```sh
python3 tests/test_worker_safety.py build-viewer/pdf-render-worker \
  build-viewer/sandbox-policy-worker .deps/pdfium-patched \
  WorkerSafetyTests.test_irs_production_xfa_corpus
```

The fallback PDF reports one page; the actual XFA layout has three. Native full
XFA field enumeration is unavailable even when the worker reports
`fieldsComplete`; an empty metadata list does not establish an empty form.
`WorkerSafetyTests.test_irs_production_xfa_round_trips` additionally edits the
page-one taxpayer-name control with two accented placeholder values, explicitly
blurs/commits and saves each generation, then reopens in a fresh isolated worker.
Native readback checks exact text, form type, page dimensions and every page's
PNG output. PDF.js compares all 56 control names/types/values and checkbox state
against the original, with the edited name and six script-committed zero totals
as the only expected differences. The manifest pins these differences explicitly;
a deliberately incorrect saved value must fail. All edits use temporary copies
and source bytes remain unchanged.

`WorkerSafetyTests.test_irs_production_xfa_calculation_round_trips` fills all
three tax/interest rows, verifies alphabetic input is rejected without changing
the numeric values, and checks the first year field's four-character limit.
Two successive save/reopen generations replace earlier values, including zero
and fractional amounts. Fresh native workers read back every edited input and
render every page; PDF.js verifies all 56 controls, including the three row
totals, two column totals and grand total. Deliberately incorrect input and
calculated-total expectations must fail. Native input retains trailing zeros
(such as `2.50`), while PDF.js exposes `2.5`; the manifest pins both expectations.
The unchanged template has no custom validation scripts. Numeric rejection and
length limits produce no validation message; these are input constraints, not
evidence of custom script-validation coverage.

`WorkerSafetyTests.test_irs_production_xfa_multiline_round_trips` saves an
80-line accented explanation containing ampersands and XML-sensitive characters,
then replaces it with two lines and saves again. Fresh native workers verify
exact text and render every opening page; PDF.js compares all 56 controls,
including moved fields, preserved line breaks and the six committed zero totals.
An incorrect saved-text expectation must fail. The source stays unchanged.

The long explanation opens as five pages in PDFium and four in PDF.js; the short
replacement opens as three in both. Focusing the expanded field triggers a
four-page native live layout. The worker revalidates cached page views after
relayout and completes text-field focus when the first click rebuilds its view.
Native rendering and direct text readback now use the same worker, without
focusing another field first.

`ViewerTests.xfaProductionRelayout` verifies the app's live page list, removal
of queued renders for deleted pages, continued native editing after cached
renders, shorter replacement and saving/reopening. It also clicks the expanded
field in QML and types after relayout to verify restored keyboard focus.

These checks do not establish pixel/layout equivalence, font fidelity, other
calculations/validation, layout rules beyond this scenario or other input/save scenarios.
Signature-labelled controls do not establish cryptographic signing support.
Production static XFA and signed fixtures remain outstanding.

## Repeated production XFA rows

`irs-f14234c.pdf` is unchanged IRS Form 14234-C, Taxpayer Initial Issues List,
revision May 2025, from the [official PDF](https://www.irs.gov/pub/irs-pdf/f14234c.pdf).
[f14234c-SOURCE.json](f14234c-SOURCE.json) pins its bytes, hash, provenance and
scenario expectations. The same IRS public-domain content policy, attribution
and non-endorsement statement above apply. No third-party copyright notice was
found in the extracted XFA template. All 12 opening controls are empty; only
unsubmitted temporary test copies contain placeholder responses.

`WorkerSafetyTests.test_irs_production_xfa_repeated_rows` runs in `worker-safety`
without network access. Native opening and rendering and pinned PDF.js admit
one landscape letter-size page. The template's exit script uses
`issues.Row1.instanceManager.setInstances()` when Number of issues changes.
The first generation changes one row to three and fills the first identified
issue with accented/XML-sensitive text. Fresh native readback checks all three
rows, including the two empty neighbors. The second generation replaces the
first issue, fills the second and third, then reduces the count to two. Native
readback and PDF.js verify that the first two values survive and the populated
third row is removed. All pages render; PDF.js compares every control's name,
type, page, value and checkbox state. Deliberately wrong values and missing-row
expectations fail. Source bytes remain unchanged.

The overflow-binding cleanup patch prevents discarded layout headings from
leaving data groups behind. Live native row positions and values are checked
before saving; twelve save/reopen generations cover one-to-three-to-two rows,
20/30-row multi-page growth, no-op saves and shrinking. Native pages remain stable
within each scenario and PDF.js checks per-page headings and every control/value.
The 30-row case opens as three native pages versus two PDF.js pages; PDF.js omits
continuation headings. Live row positions immediately after shrinking can differ
from reopened positions, so each is pinned and checked separately. These reader
differences do not establish arbitrary XFA layout compatibility or production readiness.
Custom validation is not present in this template.

```sh
python3 tests/test_worker_safety.py build-viewer/pdf-render-worker \
  build-viewer/sandbox-policy-worker .deps/pdfium-patched \
  WorkerSafetyTests.test_irs_production_xfa_repeated_rows
```

## Production static XFA

`irs-f8822.pdf` is the unchanged official IRS Form 8822, revision February 2021,
downloaded October 19, 2026. [f8822-SOURCE.json](f8822-SOURCE.json) pins the source,
rights, blank-template/privacy review, byte count and SHA-256. The IRS attribution
and public-domain policy above apply. Both source pages were visually inspected;
all 22 text widgets are empty and all three checkboxes are unchecked.

`test_irs_production_static_xfa_round_trips` checks native static-XFA admission,
two Unicode name replacements through focused input, two saved/reopened generations,
both rendered pages and unchanged source bytes. Pinned PDF.js independently checks
the blank source's 25 widgets and every saved widget identity, type, page and value.
This adds production static-XFA coverage alongside the upstream password/rotation
fixture; it does not establish pixel equivalence, all-script support or signing.

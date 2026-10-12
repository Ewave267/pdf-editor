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

These checks do not establish pixel/layout equivalence, font fidelity, other
calculations/validation, pagination after edits or other input/save scenarios.
Signature-labelled controls do not establish cryptographic signing support.
Production static XFA and signed fixtures remain outstanding.

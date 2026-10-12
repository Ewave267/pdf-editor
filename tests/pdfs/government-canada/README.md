# Government of Canada production-form corpus

Four unchanged public Statistics Canada templates start the licensed real-world
corpus. They use the [Statistics Canada Open Licence](LICENSE.md), separately
from the project's code licence. [SOURCE.json](SOURCE.json) records download
URLs, retrieval date, SHA-256 hashes, sizes, privacy/symbol review and expected
opening state. The version is the June 2026 official URL plus its exact hash;
the government may replace files at those URLs.

| Fixture | Pages | Widgets | Coverage |
| --- | ---: | ---: | --- |
| `statcan-applications.pdf` | 3 | 29 | Text, four dropdowns, multiple pages, authored choice placeholders |
| `statcan-proposal.pdf` | 13 | 28 | Long text areas, fields across pages, instruction-only first page |
| `statcan-reference.pdf` | 1 | 6 | Inline fields, date-formatting/keystroke actions and placeholders |
| `statcan-acknowledgement.pdf` | 1 | 6 | Repeated widget for one logical field, date actions and placeholders |

These are AcroForms, not XFA. All 18 pages were rendered with Poppler for source
review; no official graphics or third-party logos were observed. Field dictionaries
were inspected with locally installed pikepdf; this adds no test dependency.
Public staff authorship metadata and generic placeholders are retained to preserve
source bytes. No applicant responses have been added.

## Regression gate

`WorkerSafetyTests.test_canadian_government_corpus` checks exact hashes, opens
each PDF in the existing isolated worker, verifies page/widget counts and selected
initial values (including each repeated widget), renders every page and checks source-byte preservation. It runs
in the existing `worker-safety` CTest/CI gate without network access.

```sh
python3 tests/test_worker_safety.py build-viewer/pdf-render-worker \
  build-viewer/sandbox-policy-worker .deps/pdfium-patched \
  WorkerSafetyTests.test_canadian_government_corpus
```

Adjust build and PDFium paths for the local build. Rendering checks verify PNG
output, not pixel/layout equivalence. Local admission also independently checked
page/widget counts and recorded initial values using the project's pinned PDF.js
6.4.299. Its field-object results include non-widget parent records; count only
page-bearing widgets when comparing with PDFium. That check is not a new CI gate.
Editing, date-script behavior, calculations, Unicode entry and save/reopen remain
unverified for these fixtures.

## Remaining admissions

Keep seeking Canadian static/dynamic XFA, French/Unicode and signed documents.
Public download alone does not establish permission to bundle an original PDF.
The [general Canada.ca terms](https://www.canada.ca/en/transparency/terms.html)
restrict commercial redistribution and official symbols, so they are not an
unrestricted licence for this corpus. No IRCC or other restricted forms are
bundled, and no permission request has been sent. Admit further fixtures only
when their complete redistribution rights are established.

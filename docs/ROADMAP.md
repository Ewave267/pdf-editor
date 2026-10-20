# Product Roadmap

This roadmap describes the direction after the MVP proves the core architecture.

It is intentionally ordered by dependency rather than release date.

## Next steps — Form and signature reliability (2026-10-12)

Implementation status is recorded below; external acceptance gates remain open. Extend the existing PDFium renderer,
isolated workers and save/reopen tests; retain PDF as the file format. No new
rendering engine or file format is planned. Existing production release gates
remain open. [ADR 0008](adr/0008-form-and-signature-reliability.md) promotes
cryptographic signatures from deferred evaluation to planned work.

### 1. Build a redistributable real-world compatibility corpus

- [x] Seed the corpus with four unchanged Statistics Canada production AcroForms
  under its Open Licence; pin provenance/hashes and gate native opening, initial
  field state and all-page render output. See the
  [Canadian corpus](../tests/pdfs/government-canada/README.md). Broader form types
  and edit/save/reopen expectations below remain open.
- [x] Verify two text-edit/save/reopen generations for each seeded Canadian
  form with French accented values, all-widget preservation, all-page rendering
  and independent PDF.js checks that reject deliberately incorrect values.
  Date/choice/script scenarios and broader fixture types remain open.
- [x] Admit unchanged production IRS dynamic XFA Form 14653 with public-domain
  provenance, native three-page opening/rendering and independent PDF.js blank
  control/layout checks in CI. See the [US corpus](../tests/pdfs/government-us/README.md).
- [x] Verify two accented taxpayer-name edit/blur/save/reopen generations for
  IRS Form 14653 with fresh native text readback, all-page rendering and
  independent comparison of all 56 controls. Pin the six script-committed zero
  totals as expected reader differences and reject incorrect saved values.
  Other input, calculation/validation and pagination scenarios remain open.
- [x] Verify two IRS Form 14653 numeric edit/blur/save/reopen generations across
  all three tax/interest rows, six calculated totals, rejected alphabetic numeric
  input and the first year control's four-character limit. Fresh native readback,
  all-page rendering and independent PDF.js checks preserve all 56 controls;
  incorrect input/total expectations fail. This template has no custom validation
  scripts; other validation and pagination scenarios remain open.
- [x] Verify long-to-short multiline explanation save/reopen generations for
  IRS Form 14653, with accented/XML-sensitive text, all-page native rendering
  and independent preservation of all 56 controls. Pin the opening-layout
  difference (five native pages versus four PDF.js pages) and the return to three
  pages after replacement. Repair direct split-field focus and cached-page
  handling after relayout; verify live page-list changes, cancelled removed-page
  renders and continued QML keyboard input. Other layout scenarios remain open.
- [x] Admit unchanged IRS Form 14234-C with pinned public-domain provenance.
  Verify one-to-three-to-two repeated issue rows over two edit/save/reopen
  generations, accented/XML-sensitive values, preservation of neighboring rows
  and removal of a populated last row. Fresh native readback/all-page rendering
  and PDF.js compare every control; wrong values and missing-row expectations
  fail. The initial regression exposed accumulating table headings, repaired below.
- [x] Fix accumulating `HeaderRow` groups/table headings in Form 14234-C native
  save/reopen. Overflow binding cleanup now preserves one heading and one page,
  live row positions and neighboring values across five saved generations.
- [x] Verify twelve repeated-row generations including 20/30-row multi-page
  growth, no-op saves and shrinking. Preserve native live/reopened values and
  PDF.js controls/headings. Native and PDF.js pagination differ; equivalent
  layout across readers remains unsupported.
- [x] Extend production AcroForms with date validation and choices, admit blank
  IRS Form 8822 static XFA with two Unicode round trips, and preserve upstream
  static-password field values. Invalid signature/certification markers exercise
  warnings; genuinely signed interoperability remains an external acceptance gate.
- [ ] Continue corpus growth with further Unicode/font/layout and signed PDFs. Record each fixture's source,
  version, checksum and explicit redistribution license or permission; exclude
  personal data and documents without established redistribution rights.
- [x] Define expected open/render/input/save/reopen results per fixture, including
  unsupported behavior and known reader differences. Keep existing synthetic and
  upstream regression samples alongside the real-world corpus.
- [x] Wire the admitted corpus into existing native integration/probe and independent
  reader CI checks; local execution reports fixture and operation. Remote workflows
  were not run (local commits requested). Regressions in
  supported behavior block release.

Acceptance: each admitted fixture has provenance and expected results, and the
suite detects deliberately changed saved values or lost form structures.

### 2. Make dynamic XFA filling and saving reliable

Depends on step 1's licensed dynamic XFA fixtures.

- [x] Exercise field exit/commit, calculations, validation, repeated subforms,
  pagination/layout changes and representative Unicode values across repeated
  edit/save/reopen cycles; fix shared input/save paths when regressions appear.
- [x] Verify values, form structure and expected layout after each cycle using
  native probes and independent readers where supported. Track unsupported
  scripts/layout behavior explicitly rather than treating it as a passing check.
- [x] Preserve the source and unsaved edits on save failure; keep unsafe XFA
  addition export and flattening blocked until preservation is demonstrated.

Acceptance: declared supported fixture scenarios survive repeated round trips;
timeouts, worker failures and unsupported operations produce controlled errors
without overwriting the source or silently discarding edits.

### 3. Add a user-facing save compatibility check

Build on steps 1–2 and the existing isolated save/reopen infrastructure.

- [x] Capture expected committed form state for the saved revision, reopen the
  staged saved PDF in a fresh isolated worker and compare field identities,
  types and values, including checkbox/radio/choice state and XFA data where
  available. Verify that supported form structures survived before committing
  the destination atomically.
- [x] Show which checks passed, failed or could not be performed. Use wording
  such as “Form fields and values verified after saving”; unavailable field
  inspection must remain an explicit unchecked result, never a pass.
- [x] On mismatch, reopen failure or verification timeout, retain edits and the
  previous destination, explain the failure and allow retry. Test successful
  checks, missing/changed fields, Unicode values and worker failures.

Acceptance: a deliberate saved-value mismatch prevents a verified-success result
and destination replacement. The report identifies its scope: reopening does
not guarantee every script, layout rule or external reader will behave correctly
and is not a universal compatibility certificate.

### 4. Warn before operations that could invalidate forms or signatures

Deliver form warnings alongside steps 2–3; extend signature warnings with step 5.

- [x] Detect affected forms and signature presence before saving, adding content,
  changing pages or flattening. Explain the specific potential loss of form
  behavior, signature validity or certification permissions before proceeding;
  uncertain impact must be stated clearly.
- [x] Allow cancellation without changing the document. Keep unsafe operations
  blocked where preservation cannot be supported; acknowledgement alone must
  not bypass those restrictions. Distinguish drawn/imported signature images
  from cryptographic signatures in the interface.
- [x] Cover unsigned, signature/certification metadata markers, XFA and AcroForm
  cancellation and unsupported-operation paths. Markers intentionally contain
  invalid signature bytes; validity and trust are not tested.
- [ ] Validate warnings with independently verified signed/certified production PDFs.

Acceptance: every supported risky operation has a relevant pre-operation warning
and a tested cancellation path; unsupported destructive operations remain blocked.

### 5. Add cryptographic PDF signing and validation — excluded by user request (2026-10-19)

Depends on corpus coverage, verified saving and signature-aware operation checks.

- [ ] Record a signing design covering library/license choice, certificate and
  private-key handling, incremental updates, certification permissions, trust
  anchors, timestamps and revocation policy before selecting dependencies.
- [ ] Implement real PDF signatures using protected key access and incremental
  saves that preserve prior signed revisions. Validate existing/new signatures,
  signed byte ranges, later modifications, certificate chains and applicable
  document permissions; explain cryptographic validity separately from trust.
- [ ] Report expired, untrusted, revoked and indeterminate/offline status without
  claiming trust when verification is unavailable. Verify outputs with an
  independent signature validator and test tampering, multiple signatures,
  invalid chains and signing failures without source or key disclosure.

Acceptance: independently validated signed outputs, explicit trust/status
reporting and failure-safe saving. Visual signature placement remains available
as a separate feature and is never described as cryptographic signing.

## Phase 1 — Reliable Core

Make the MVP production-quality.

Improve:

- PDF rendering.
- AcroForm compatibility.
- XFA compatibility.
- JavaScript compatibility.
- Saving.
- Error handling.
- Large-document performance.
- Font handling.

Build a growing regression corpus from real documents.

### First reliability milestone (2026-10-08)

- [x] Preserve the current document, worker and unsaved additions when opening
  another file fails local validation or snapshot creation.
- [x] Show that opening error while keeping the current document usable.
- [x] Honour cancellation of cached render deliveries and invalidate pending
  cached deliveries after native form input.
- [x] Cover rejected opens, continued rendering/saving and render cancellation
  with local integration regressions.
- [x] Diagnose the reported Windows Explorer startup exit using startup logs;
  the user confirmed the sandbox fix opens the document. Formal clean-desktop
  release walkthroughs remain a separate gate.
- [ ] Add redistributable real-document fixtures and broader static/dynamic XFA
  coverage, including layout changes and representative Unicode fonts.
- [x] Fix and verify the documented AcroForm JavaScript keystroke conversion
  limitation in the pinned dependency.
- [x] Establish bounded local 1000-page open/render/cache/input/heartbeat and
  memory baselines; see [PERFORMANCE](PERFORMANCE.md).
- [ ] Complete representative-hardware measurements and clean-desktop walkthroughs
  on each supported operating system.

Phase 1 remains in progress. Passing synthetic tests does not establish arbitrary
PDF compatibility or complete production readiness. Later phases remain ordered
as below; signing, crash recovery and the security release gates must also be
resolved before a production release. Batch locally validated changes before
running the native artifact workflow; reuse the existing patched PDFium build
unless a dependency change requires rebuilding it.

## Phase 2 — Everyday Editing

Implementation complete. Added-content tools now include text with font family,
size, bold, italic and underline; images and signatures; checkmarks, freehand,
translucent highlights, rectangles, ellipses, lines and text stamps.

Everyday editing includes grouped undo/redo, copy/cut/paste, page-local
multi-selection and area selection, six alignment modes, keyboard commands,
eight resize handles and Shift-corner aspect locking. Pages, thumbnails and
PDF export share the rendering implementation. See [ADDED-CONTENT](ADDED-CONTENT.md)
for controls, tests and bounds.

This completes the added-content scope, not the production release roadmap.
Existing PDF text editing, native form undo, recovery and safe dynamic XFA
addition export remain outside this phase. Native-platform release checks are
still required for new builds.

## Phase 3 — Form Experience

Implemented the form experience for supported AcroForms, with native XFA
highlighting, snapshot reset and caret-based scrolling. Field navigation,
required markers/checks, reset dialogs, date/choice helpers and guided visual
signature placement are available. See [FORMS](FORMS.md) for controls, validation
and explicit XFA/public-API limits. Broader production-form compatibility remains
part of the reliability roadmap.

Delivered:

- Tab navigation.
- Clear indication of fillable fields.
- Required-field indication.
- Form validation feedback.
- Reset field/form.
- Automatic scrolling to the next field.
- Better date and choice-field controls.
- Signature placement workflow.

Preserve the behavior of dynamic XFA documents wherever possible.

## Phase 4 — Document Workflow

Implemented through new-file page export and native printing. See
[DOCUMENT-WORKFLOW](DOCUMENT-WORKFLOW.md) for controls and explicit format limits.

Common operations that complement form completion:

- Rotate pages.
- Reorder pages.
- Remove pages.
- Insert pages.
- Merge PDFs.
- Extract pages.
- Print.
- Export.
- Flatten added content.
- Flatten forms where safely supported.

Existing document text editing remains outside the core scope.

## Phase 5 — User Experience

Implemented controls and bounded local recovery. See
[DESKTOP-EXPERIENCE](DESKTOP-EXPERIENCE.md) for behavior, storage and limits.

Delivered:

- Recent documents.
- Drag-and-drop opening.
- Search.
- Page thumbnails.
- Fit page / fit width.
- Full-screen viewing.
- Autosave/recovery.
- Unsaved-change protection.
- Accessible keyboard navigation.
- High-DPI support.
- Dark/light interface support.

The interface should expose PDF complexity only when the user actually needs it.

## Phase 6 — Compatibility and Security

Implemented regression gates, seeded malformed-input mutation cases, host
ASan/UBSan checks, upstream static/dynamic/Arabic/encrypted-rejection samples,
recovery and scheduled dependency monitoring. Password entry remains unsupported;
parser/library sanitizer coverage and larger fuzz campaigns remain further
hardening work. Production release requires advisory review and native desktop
validation; see [RELEASE](RELEASE.md).

Maintain compatibility suites for:

```text
PDF
AcroForm
XFA static
XFA dynamic
XFA JavaScript
Encrypted PDFs
Malformed PDFs
Large PDFs
```

Add:

- Fuzz testing.
- Dependency vulnerability monitoring.
- JavaScript sandbox testing.
- Memory-safety testing.
- Crash recovery.
- Limits for pathological documents.

Security regressions block releases.

## Phase 7 — Cross-Platform Releases

Native candidate workflows build, validate and package Windows, Linux x86_64/
aarch64 and Intel/Apple Silicon macOS. `VERSION` drives executable/archive
versioning; dependency revisions, patches, notices, checksums and timing reports
are tracked. Signing, notarization and final clean-desktop acceptance remain
production release gates rather than completed candidate checks.

Provide reproducible builds for:

```text
Windows
Linux
macOS
```

Automate:

- Builds.
- Tests.
- Packaging.
- Release artifacts.
- Versioning.
- Dependency tracking.

## Phase 8 — Advanced Capabilities

Evaluation complete for the requested scope. Decisions and prerequisites are
recorded in [ADR 0007](adr/0007-advanced-feature-evaluation.md). Cryptographic
signatures are now planned in the next steps above and
[ADR 0008](adr/0008-form-and-signature-reliability.md); the other optional
features remain subject to the earlier decisions and individual designs.

Evaluate separately rather than automatically adding them to the core product:

- OCR for scanned documents.
- Form-field creation.
- Cryptographic signatures.
- Redaction.
- PDF/A workflows.
- Accessibility tools.
- Plugin or extension system.

Each major addition should have a clear user need and an architecture decision before implementation.

## Long-Term Product Principle

The project succeeds when completing a PDF feels like completing a normal document:

1. Open it.
2. Fill what is already there.
3. Add whatever is missing.
4. Sign it.
5. Save it.
6. Send it.

The complexity of PDF, AcroForm, XFA, and JavaScript should remain behind that experience.

## Remaining production acceptance

The core feature implementation does not close every production acceptance item.
Remaining: representative licensed production-form/Unicode/layout corpus,
large-document timing baselines and Windows/macOS/Linux clean-desktop checks;
review of the V8 freshness finding and dependency coverage; platform signing/
notarization and physical-printer/accessibility validation. Keep these open until
there is evidence from the actual platforms. Do not equate offscreen CI with
production acceptance.

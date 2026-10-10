# Product Roadmap

This roadmap describes the direction after the MVP proves the core architecture.

It is intentionally ordered by dependency rather than release date.

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
- [ ] Establish rendering/interaction latency baselines on large documents and
  clean-desktop walkthroughs on each supported operating system.

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
recorded in [ADR 0007](adr/0007-advanced-feature-evaluation.md); these optional
features are deferred pending concrete user needs and individual designs.

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

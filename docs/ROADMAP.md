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
- [ ] Diagnose the reported Windows Explorer startup exit using startup logs;
  verify a native desktop launch, beyond the offscreen CI check.
- [ ] Add redistributable real-document fixtures and broader static/dynamic XFA
  coverage, including layout changes and representative Unicode fonts.
- [ ] Fix and verify the documented AcroForm JavaScript keystroke conversion
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

Expand added-content tools.

Support:

- Text.
- Signatures.
- Images.
- Checkmarks.
- Freehand drawing.
- Highlights.
- Shapes.
- Stamps.

Add:

- Undo / redo.
- Copy / paste.
- Multi-select.
- Alignment.
- Keyboard shortcuts.
- Better selection and resize controls.

## Phase 3 — Form Experience

Make completing forms faster than using traditional PDF software.

Add:

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

Add common operations that complement form completion:

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

Build a polished desktop experience.

Add:

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

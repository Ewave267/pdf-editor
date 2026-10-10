# Step 6 — Save a copy

Use **Save a copy** or **Ctrl+S** (also **Ctrl+Shift+S**) to choose a new local
PDF. Use Command for these shortcuts on macOS. The application preserves the
open document's original content, edited form values and form structures and embeds
its text, image, signature, checkmark, drawing, highlight, shape and stamp
additions in the new PDF. Text uses embedded fonts; paths stay vector graphics,
and images use lossless encoding. Highlights and signatures retain transparency.
Pages, thumbnails and export share the added-content painter. Saving waits until
an active drawing, movement or resize gesture is finished.

The original source filename and its existing file aliases are rejected as
save destinations. Saving to another existing filename uses the file dialog's
overwrite confirmation. The application commits the output atomically through
`QSaveFile`, with direct-write fallback disabled. A failed write leaves form edits,
additions and the current document available, and displays an error.
A crashed or unresponsive renderer is terminated; native form loss is explicitly
reported and application-owned additions are retained. See [SAFETY](SAFETY.md).

The open document uses a private snapshot created on opening. Later external
changes to the source file cannot change the rendering or save baseline.
Each save captures a fresh native snapshot of the live form state before adding
content in a separate worker, so additions do not accumulate duplicate copies.
Form input is paused during saving. Later addition edits during an asynchronous
save remain marked unsaved. A successful save clears the unsaved marker for
the saved revision. Closing the document or window during a save is prevented
in the UI. No autosave or crash recovery is included.

Reopening the new PDF displays additions as ordinary PDF content. They are no
longer separately selectable app objects. Existing forms remain editable forms; see [FORMS](FORMS.md).

## Supported documents and limits

Ordinary PDFs and the synthetic AcroForm fixture support saving additions.
Coordinates account for cropped pages, page rotations, and mixed page sizes.
Compatibility with arbitrary production documents is not yet established.

Dynamic full XFA supports saving a copy **without additions**. PDFium's public
page-object editing API cannot insert ordinary objects into XFA-generated pages;
requests to save additions on these documents return a controlled error and
retain those additions. Flattening XFA would discard form behavior, so this
implementation refuses that operation. Foreground/static XFA insertion has not
been validated with representative fixtures.

Step 4 verifies viewer edits, checked and unchecked states, radio selections,
dropdown values, XFA calculations, and independent saved values in both form
types. AcroForm fields remain editable after saving with additions. AcroForm
JavaScript actions survive structurally; the patched native keystroke path now
applies the conversion fixture, with a mixed-case input regression; see [FORMS](FORMS.md).
The existing native XFA probe also verifies calculation and repeated saving of
a viewer-saved XFA copy.

Input and output PDFs are limited to 64 MiB; the generated addition PDF is
limited to 32 MiB. The save worker has a 30-second deadline. The renderer and
save worker remain isolated by Bubblewrap. The worker has no writable host
output directory; it returns PDF bytes over stdout for the application to commit.

## Architecture

`PdfSave.cpp` creates a transparent, page-sized addition PDF with Qt's
`QPdfWriter`. The renderer first commits focus and returns a live native form
snapshot. A fresh `pdf-render-worker --save` opens that read-only snapshot,
imports each addition page as a PDF Form XObject, maps displayed
page coordinates through `FPDF_DeviceToPage`, and generates/saves page content.
It invokes PDFium's document save actions in the sandbox. All PDFium handles
stay in that worker, and its mutations never reach the live renderer.

`AddedContent` tracks a content revision separately from selection changes.
`PdfDocument` tracks saved and active-save revisions for both forms and additions.
See [ADR 0004](adr/0004-isolated-save-as.md) and its live-form update in
[ADR 0005](adr/0005-native-form-events.md).

## Validation

The save cases extend `viewer-integration` and verify:

- Save and reopen text, local images, and transparent signatures on separate
  mixed-size pages; verify original content pixels and source bytes.
- Save repeatedly and independently inspect text positions, fonts/text content,
  image presence without duplicates, and original text using pinned PDF.js.
- Preserve placement on cropped pages rotated by 0, 90, and 270 degrees.
- Retain the open snapshot after external source replacement, retain edits made
  during saving, and retain changes after destination or worker-start failures.
- Preserve AcroForm values/widgets/actions and dynamic XFA values/layout in
  PDF.js; run the existing native edit/calculation/round-trip probe on the saved
  XFA copy; refuse unsafe dynamic XFA additions without losing them.
- Verify viewer form edits, native keyboard traversal, XFA calculations and
  every control value with PDF.js, including checked/unchecked checkbox states.
- Exercise the actual QML Save a copy file dialog and close after a successful save.

These checks run with the combined viewer/XFA build. The independent-reader and
native XFA probe checks require `PDF_EDITOR_BUILD_XFA_PROBE=ON` and the pinned
reader dependencies. See [VIEWER](VIEWER.md) for build commands. Remote CI has
not been run in this session.

Page export and printing are described in [DOCUMENT-WORKFLOW](DOCUMENT-WORKFLOW.md).
Idle local recovery checkpoints are described in
[DESKTOP-EXPERIENCE](DESKTOP-EXPERIENCE.md). They do not overwrite the source or
replace Save a copy.

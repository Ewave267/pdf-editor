# Step 6 — Save As

Use **Save As** or **Ctrl+Shift+S** to choose a new local PDF. The application
preserves the open document's original content and form structures and embeds
its text, image, and signature additions in the new PDF. Text uses embedded
fonts; images use lossless encoding, including signature transparency.

The original source filename and its existing file aliases are rejected as
save destinations. Saving to another existing filename uses the file dialog's
overwrite confirmation. The application commits the output atomically through
`QSaveFile`, with direct-write fallback disabled. A failed write leaves additions
and the current document available, and displays an error.

The open document uses a private snapshot created on opening. Later external
changes to the source file cannot change the rendering or save baseline.
Repeated saves combine this snapshot with the current additions, so additions
do not accumulate duplicate copies. Editing during an asynchronous save keeps
later changes marked unsaved. A successful save clears the unsaved marker for
the saved revision. Closing the document or window during a save is prevented
in the UI. No autosave or crash recovery is included.

Reopening the new PDF displays additions as ordinary PDF content. They are no
longer separately selectable app objects. Existing forms remain forms; the
viewer form-editing UI is still pending in step 4.

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

Form values already present in a document are preserved. New form edits through
the viewer cannot be tested until step 4 provides that interface. The native XFA
probe does verify editing, calculation, and repeated saving of a viewer-saved
XFA copy. AcroForm field values, editable widgets, defaults, and JavaScript
keystroke actions are checked structurally in PDF.js; interactive AcroForm
behavior remains a validation gap.

Input and output PDFs are limited to 64 MiB; the generated addition PDF is
limited to 32 MiB. The save worker has a 30-second deadline. The renderer and
save worker remain isolated by Bubblewrap. The worker has no writable host
output directory; it returns PDF bytes over stdout for the application to commit.

## Architecture

`PdfSave.cpp` creates a transparent, page-sized addition PDF with Qt's
`QPdfWriter`. A fresh `pdf-render-worker --save` opens the same read-only snapshot
as the renderer, imports each addition page as a PDF Form XObject, maps displayed
page coordinates through `FPDF_DeviceToPage`, and generates/saves page content.
It invokes PDFium's document save actions in the sandbox. All PDFium handles
stay in that worker, and its mutations never reach the live renderer.

`AddedContent` tracks a content revision separately from selection changes.
`PdfDocument` tracks the last saved revision and the revision captured for an
active save. See [ADR 0004](adr/0004-isolated-save-as.md).

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
- Exercise the actual QML Save As file dialog and close after a successful save.

These checks run with the combined viewer/XFA build. The independent-reader and
native XFA probe checks require `PDF_EDITOR_BUILD_XFA_PROBE=ON` and the pinned
reader dependencies. See [VIEWER](VIEWER.md) for build commands. Remote CI has
not been run in this session.

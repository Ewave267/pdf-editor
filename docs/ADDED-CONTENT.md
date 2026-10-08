# Step 5 — Added content

The viewer supports application-owned text, images, and image-based signatures.
Native form editing is also available; see [FORMS](FORMS.md). Build and launch with the existing
[viewer instructions](VIEWER.md); the PDFium dependency can be reused.

## Controls

- **Text** opens a plain-text editor with font family, size (6–144 points),
  bold, italic and underline controls. The editor previews the chosen style.
  Accept, then click a page to place it.
- **Image** selects a local image. Accept, then click a page to place it.
- **Signature** selects a local signature image, including transparent PNGs.
  Accept, then click a page to place it. This is a visual signature, not a
  cryptographic signature; a handwritten drawing tool is not included.
- Click an object to select it. Drag its body to move it and its bottom-right
  corner to resize it. Geometry stays inside its original page.
- **Edit text** changes the selected text object's content and formatting.
  Formatting applies to the whole text box; Cancel keeps its previous settings.
  **Delete** removes
  the selected object. Esc cancels placement and clears selection.
- **Undo / Redo** in the bottom bar reverses added-content creation, deletion,
  moves, resizing, text and font changes. Platform undo/redo shortcuts are also
  available (Ctrl+Z and Ctrl+Shift+Z/Ctrl+Y on Linux/Windows, Command+Z and
  Command+Shift+Z on macOS). Each drag or text-dialog acceptance is one step.
  Shortcuts leave text editors and native form focus alone. Native form edits
  are not included in this history. Undo/redo is disabled while saving.

Thumbnails include additions. Coordinates and sizes use top-left page points,
independent of zoom. Text defaults to 18-point Sans Serif and wraps/clips to its rectangle;
resizing changes that rectangle. Images and signatures stretch to their resized
rectangle. Text is limited to 10,000 characters and imported images to 16 million
pixels. Images are copied into memory, so deleting or changing the original image
file does not change an imported object.

**Use Save As to persist additions in a new PDF.** Opening another PDF,
closing the document, or closing the window asks before discarding unsaved
changes. The source PDF stays unchanged. Reopening saved additions shows them
as ordinary PDF content. Dynamic XFA additions cannot yet be saved safely;
see [SAVING](SAVING.md). Available font families come from Qt's installed/bundled
fonts, with Sans Serif, Serif and Monospace fallback choices. The same style is
used on the page, in thumbnails and in exported PDFs, which embed the resolved
font. Font availability and missing-glyph fallback depend on the platform.
These controls format added text boxes; existing PDF text and native form fonts
are not edited. Individual words within a box cannot have different formatting.
History stays in memory for the open document, with at most 100 steps and a
64 MiB budget for estimated history metadata/text and unique retained image
pixels. Older states are discarded when either bound is exceeded. Undoing back
to the saved addition state clears its unsaved marker; native form changes are
tracked separately. A new edit after undo clears redo. Closing/replacing the
document clears history; reopening an exported PDF does not restore its history
or individually editable addition objects.

Recovery, aspect locking,
and keyboard manipulation are later work.

## Model and rendering

`PdfDocument` owns an `AddedContent` QObject containing tagged objects with a
stable session ID, page index, type, rectangle, and text or decoded image content.
QML receives a narrow creation, selection, hit-test, geometry, editing, and
removal API. Page validation and geometry bounds live in C++, and closing or
replacing the document resets its additions. Object IDs are not reused across
replacements during a document object's lifetime.

`AddedOverlay` paints a transparent layer over the worker's raster, including
selection bounds for the main view. Thumbnail overlays omit selection. The
PDFium worker and its source PDF never receive addition edits in this step.
See [ADR 0003](adr/0003-application-owned-additions.md).

## Validation

Two new cases in `viewer-integration` cover all three types. They verify creation
on different mixed-size pages, hit testing, selection, movement, resizing,
deletion, unchanged text including Unicode and literal markup, image snapshots
after deleting the source image, rendering at 50% and 200% scale, invalid input,
page bounds, document replacement, and byte-for-byte preservation of the PDF.

The actual QML scene also exercises the text dialog, image/signature file
pickers, placement clicks, body dragging, corner resizing, deletion, and cancel
and discard paths when closing. The screenshot is visually inspected. The
existing viewer, XFA regression, and independent-reader gates remain enabled.
Coverage is synthetic fixtures; no broad PDF compatibility claim is made.

The Phase 2 font regression checks validation and revision tracking, changed
preview pixels, underline rendering, dialog creation/edit/cancel, and saving and
reopening. Pinned PDF.js independently checks a 24-point embedded Liberation
Serif Bold Italic font with system-font fallback disabled.

Undo/redo regressions cover grouped creation/formatting and drags/resizing,
deletion, images/signatures after source removal, branch truncation, invalid and
no-op edits, the history step bound, saved-state markers, in-flight saves, close,
and the actual QML buttons and keyboard shortcuts.

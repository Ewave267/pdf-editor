# Step 5 — Added content

The viewer supports application-owned text, images, and image-based signatures.
Step 4's form editing remains pending. Build and launch with the existing
[viewer instructions](VIEWER.md); the PDFium dependency can be reused.

## Controls

- **Text** opens a plain-text editor. Accept, then click a page to place it.
- **Image** selects a local image. Accept, then click a page to place it.
- **Signature** selects a local signature image, including transparent PNGs.
  Accept, then click a page to place it. This is a visual signature, not a
  cryptographic signature; a handwritten drawing tool is not included.
- Click an object to select it. Drag its body to move it and its bottom-right
  corner to resize it. Geometry stays inside its original page.
- **Edit text** changes the selected text object's content. **Delete** removes
  the selected object. Esc cancels placement and clears selection.

Thumbnails include additions. Coordinates and sizes use top-left page points,
independent of zoom. Text uses an 18-point font and wraps/clips to its rectangle;
resizing changes that rectangle. Images and signatures stretch to their resized
rectangle. Text is limited to 10,000 characters and imported images to 16 million
pixels. Images are copied into memory, so deleting or changing the original image
file does not change an imported object.

**Additions are held in memory only.** Opening another PDF, closing the document,
or closing the window asks before discarding them. The source PDF stays
unchanged. Saving and reopening additions belongs to step 6; recovery, undo/redo,
font controls, aspect locking, and keyboard manipulation are later work.

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

# Added content — everyday editing

The viewer supports application-owned text, images, image-based signatures,
checkmarks, freehand strokes, highlights, rectangles, ellipses, lines and stamps.
Native form editing is also available; see [FORMS](FORMS.md). Build and launch
with the [viewer instructions](VIEWER.md); reuse the existing PDFium dependency.

## Tools

- **Add text** opens a plain-text editor with font family, size (6–144 points),
  bold, italic and underline. Accept, then click a page to place the box.
  **Edit text** changes the selected text box or stamp. Formatting applies to
  the whole object; Cancel keeps its previous settings. Selected text also has
  direct bold, italic and underline controls; wider windows show font and size.
- **Image** imports a local image, including transparent PNGs.
  **Sign** offers drawing or importing a signature. Accept, then click or draw on
  the page to place it. Signatures are visual additions, not
  cryptographic signatures. The freehand tool can also draw handwriting.
- **Draw** offers checkmarks, freehand, highlights, rectangles, ellipses, lines
  and text stamps. Choose a color and line thickness (1–12 points). Click to
  place a checkmark or stamp; drag to draw or size the other tools. Highlights
  are translucent colored rectangles, rather than semantic text selection.
- **Style** changes the selected object's color and line thickness. Stamp text
  can also be changed here. Use **Edit text** for stamp font formatting.

Text defaults to 18-point Sans Serif and wraps/clips to its rectangle. Images
and signatures stretch to their resized rectangle. Text is limited to 10,000
characters, imported images to 16 million pixels, and a freehand stroke to
4,096 points. Images are copied into memory; subsequent changes to the source
image do not change an imported object.

## Selection and keyboard controls

Click an object to select it. Shift-click adds/removes an object from the
selection; selections stay on one page. **Select area** lets you drag a rectangle
around objects; hold Shift to add to the selection. **Edit → Select all added
content on this page** selects the
current page's additions. Drag a selected object's body to move the entire
selection. Geometry stays inside its original page.

A single selected object has eight resize handles at its corners and edge
midpoints. Hold Shift while dragging a corner to preserve its aspect ratio.
**Align** aligns a group to its left, horizontal center, right, top, vertical
center or bottom bound. **Delete** removes the selection.

**Edit → Copy added content**, **Cut added content** and **Paste** preserve
addition types, styles and images.
Pasting puts a copy on the current page, offset where space permits. A group
that cannot fit that page is rejected. Plain text and images copied from other
applications can also be pasted. Internal clipboard payloads are validated
before insertion, with limits of 100 objects and 64 MiB of encoded data/decoded
image pixels per paste.

| Action | Linux / Windows | macOS |
| --- | --- | --- |
| Copy / cut / paste | Ctrl+C / X / V | Command+C / X / V |
| Select current page's additions | Ctrl+A | Command+A |
| Undo / redo | Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y | Command+Z / Command+Shift+Z |
| Delete selection | Delete or Backspace | Delete or Backspace |
| Move selection | Arrow keys (1 point), Shift+arrow (10 points) | Same |
| Cancel placement or an active gesture; clear selection | Escape | Escape |

**Undo / Redo** in the bottom bar reverses creation, deletion, movement,
resizing, text, font, color and line changes, drawing, paste and alignment.
Each drag, stroke, paste, group action or dialog acceptance is one step.
Cancelling an active gesture restores its starting state and previous history.
Shortcuts leave text editors and native form focus alone. Native form edits
are tracked separately and are not included in this history. Saving and
undo/redo wait until an active gesture is finished.

History stays in memory for the open document, with at most 100 steps and a
64 MiB budget for estimated metadata/text, stroke points and unique retained
image pixels. Older states are discarded when either bound is exceeded.
Undoing back to the saved addition state clears its unsaved marker. A new edit
after undo clears redo. Closing/replacing the document clears history.

## Saving and rendering

**Use Save a copy to persist additions in a new PDF.** Opening another PDF,
closing the document, or closing the window asks before discarding unsaved
changes. The source PDF stays unchanged. Reopened additions are ordinary PDF
content; the app does not recover their individual editing objects or history.
Dynamic XFA additions cannot yet be saved safely; see [SAVING](SAVING.md).

The same painter draws pages, thumbnails and exported PDFs. Selection outlines
appear only in the main view. Text uses embedded fonts, paths remain vectors,
and highlights and image signatures retain transparency. Generic font choices
prefer the corresponding bundled Liberation family when available, avoiding
synthetic bold text being emitted twice. Font availability and missing-glyph
fallback still depend on the platform. These controls format added text and
stamps; existing PDF text and native form fonts are not edited. Individual words
within a box cannot have different formatting.

`PdfDocument` owns `AddedContent` objects with stable session IDs, page indices,
rectangles, styles and decoded images or normalized stroke points. C++ validates
page bounds and clipboard input. `AddedOverlay` paints above the worker raster;
PDFium receives additions only through the save pipeline. See
[ADR 0003](adr/0003-application-owned-additions.md).

## Validation

The viewer integration suite exercises mixed-size pages, model validation,
clipboard round trips for every addition type, group movement at page bounds,
all six alignment modes, undo/redo, and saved-state tracking. Actual QML tests
exercise tool menus, placement and strokes, area selection, keyboard clipboard
and deletion, eight resize handles and cancelling gestures. Existing text/font,
image/signature, form and safety regressions remain enabled.

Saved graphics are reopened and checked for visible paths, highlight alpha,
image pixels and signature transparency. Pinned PDF.js independently checks
vector drawing operations, embedded text, transparency and absence of duplicate
additions across repeated saves. Local relocated tarball and AppImage tests
exercise the bundled application and worker. These synthetic fixtures do not
establish compatibility with every production PDF or replace native-platform
release validation.

# Desktop controls and recovery

The app starts in a normal window with the operating system's close button.
The layout follows familiar document editors:

- **File** opens PDFs and recent files, saves a copy, exports pages, prints and
  recovers unsaved work. **Edit** contains clipboard commands and Find.
- **View** controls page thumbnails, fitting, dark appearance and full screen.
  **Insert** adds content; **Form** contains field helpers and reset actions.
- The top bar keeps **Open**, Undo/Redo, **Find** and **Save a copy** available.
  It shows the current file and whether changes are unsaved.
- The editing toolbar offers **Select**, **Add text**, **Image**, **Sign** and
  **Draw**. The row below explains where to click or drag; **Cancel** exits a
  placement tool. After selecting added content, formatting, alignment and
  deletion controls appear there. Text has direct bold/italic/underline controls,
  with font family and size on wider windows. **Edit text** always opens the full
  text and font dialog. These controls format added text boxes, not original PDF
  text or native form-field fonts.
- Page thumbnails sit on the left. **Pages** at the bottom toggles them; page
  navigation and zoom sit together at the bottom. Compact windows retain the
  main tools, and form controls wrap onto additional rows instead of requiring
  sideways scrolling. **Fit width** is also available in View on compact windows.
- The welcome screen offers **Open a PDF**, recent files, recovery when available
  and short editing instructions. Drop one local PDF into the window to open it.

**Ctrl+O** opens, **Ctrl+S** or **Ctrl+Shift+S** saves a copy, **Ctrl+F** searches
and **Ctrl+P** prints (use Command for these standard shortcuts on macOS).
**F11** toggles optional full screen. Opening, closing and dropping files with
unsaved edits offer **Save a copy**, **Discard** or **Cancel**. Cancelling or
failing the save keeps the document open; the requested action continues only
after a successful save with no remaining unsaved edits. The original source
cannot be overwritten. Fit page and Fit width follow viewport changes. PDF
paper remains white in dark mode; the theme preference persists locally.

Recent files retain up to ten successfully opened paths. Search is case
insensitive and lists matching pages using PDFium text extraction. It searches
existing PDF text, including previously exported additions, not unsaved
application additions or OCR/image text. Text extraction order can differ from
visual reading order, particularly for overlapping text. Results stop at 200
matching pages or five seconds between pages; each request remains subject to
the worker deadline. Complex-script input and screen-reader use still need
native desktop checks.

## Recovery checkpoints

While the document is dirty and the worker is idle, an automatic checkpoint is
attempted once a minute. It stores a native form-state PDF and validated,
editable application additions in the application's local data `recovery`
folder. The source is never overwritten, and checkpointing does not mark edits
saved. Native form focus is committed when the checkpoint is taken; authored
form validation/save actions may run, as with a normal form snapshot.

After an interrupted session, close any open document and use
**File → Recover unsaved work…**. Checkpoints are listed by time.
Recover or discard one explicitly. Recovery preserves text styling, geometry,
images and drawing points, but not undo history or the previous selection.
The recovered document is dirty and requires Save a copy. Original source-file
protection is retained. Explicit document close/discard and a successful save
remove the current checkpoint. A renderer failure leaves the last checkpoint
available; a process crash does not run cleanup.

Checkpoints contain document content on disk, with owner permissions where the
platform supports them and the user-profile directory's inherited access rules
on Windows. The JSON checkpoint is limited to 64 MiB and 1,000 application
objects. Large documents/additions may exceed that limit. Corrupt checkpoints,
out-of-bounds geometry and oversized images are rejected. XFA layout may change
when saved values reopen; incompatible saved additions produce a recovery error.
Work entered since the last successful checkpoint can be lost. Unsaved changes
before the first checkpoint have no recovery copy.

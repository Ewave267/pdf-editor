# Desktop controls and recovery

The app starts in a normal window with the operating system's close button.
Use **Document…** for recent files, page export, printing, search, recovery,
theme selection and optional full screen. **F11** toggles full screen;
**Ctrl+F** searches; **Ctrl+P** prints. Drop one local PDF into the window to
open it. Opening, closing and dropping files retain the existing unsaved-change
confirmation. Fit page and Fit width follow viewport changes. PDF paper remains
white in dark mode; the theme preference persists locally.

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
**Document… → Recover previous session…**. Checkpoints are listed by time.
Recover or discard one explicitly. Recovery preserves text styling, geometry,
images and drawing points, but not undo history or the previous selection.
The recovered document is dirty and requires Save As. Original source-file
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

# Pages, export and printing

Use **File → Organize / export pages…** to write a new PDF while keeping the open
source and its edits available. Enter page numbers and ranges separated by
commas, in the order you want. `3,1-2` reorders; `1,3` removes page 2;
`2-4` extracts pages; `3-1` reverses a range; `blank` inserts a US Letter page.
Choose a clockwise rotation for the selected pages. **Append PDFs…** appends
other documents in the file dialog's selection order. Appended pages retain
their original rotation.

Export preserves page content and flattens application additions into that
content. Document-level bookmarks, scripts and metadata are not transferred.
AcroForms require **Flatten annotations and forms**; visible values become
permanent and fields cease to be interactive. This also flattens supported
annotations. AcroForm inputs must be opened and exported with flattening before appending:
merging does not regenerate another document’s field appearances or run its
scripts. Dynamic and foreground XFA page exports/merges are rejected.
Use ordinary Save a copy to preserve interactive form behavior instead.

Limits: 2,000 output pages, 16 appended PDFs, 64 MiB combined appended inputs,
32 MiB additions and 64 MiB output. Password-protected inputs needing a password
are rejected. Inputs are bounded regular-file snapshots, sent through the worker
pipe; the isolated worker does not receive additional host filesystem access.
Atomic output commits protect the source and every appended input, including
hard-link aliases. An export does not mark the open source's changes saved.

**File → Print…** or **Ctrl+P** opens the system print dialog. Printing
supports page ranges and includes form values and additions. PDF pages are
rasterized at up to 2,400 pixels wide within the existing raster limits;
application additions are painted over them. This is not a vector-preserving
print export. Choose Export pages or Save a copy for vector PDF output.

**File → Print to PDF…** bypasses printer selection for a new PDF.
Application-controlled print-to-PDF output is staged privately and committed
atomically with source protection. Printer drivers that ask for an output path
manage that destination themselves. Physical printers, drivers and native
print dialogs require desktop validation on each operating system.

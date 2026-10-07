# MVP

## Objective

Prove that the application architecture can reliably handle real PDF forms, especially XFA, before building a large user interface.

The MVP should be small enough that every major component is understood.

## Definition of Done

The MVP can:

- Open a PDF.
- Render every page.
- Navigate and zoom.
- Detect the document's form type.
- Interact with AcroForm fields.
- Interact with representative XFA forms.
- Execute required XFA JavaScript safely.
- Add text to a page.
- Add a handwritten/image signature.
- Save to a new PDF.
- Reopen that PDF with the changes preserved.

A step is considered complete only when its validation criteria can be reproduced.

## Step 1 — Repository Foundation

Create:

```text
README.md
docs/MVP.md
docs/ROADMAP.md
docs/adr/
src/
qml/
tests/
tests/pdfs/
```

Add:

- License.
- `.gitignore`.
- Basic CMake project.
- Formatting configuration.
- Minimal CI build.

Do not introduce dependencies unless they solve a current requirement.

### Validation

- [ ] A clean clone can be configured with CMake.
- [ ] The project builds without manual file changes.
- [ ] The application executable is produced.
- [ ] The application launches successfully.
- [ ] The test command can be executed.
- [ ] CI performs at least the configure and build steps.
- [ ] The project license is present and identified as `GPL-3.0-only`.

## Step 2 — Prove XFA First

Before building the editor UI, create a minimal PDFium test application.

It must:

1. Initialize PDFium.
2. Open a known XFA document.
3. Detect its form type.
4. Load XFA.
5. Render a page.
6. Initialize the form environment.
7. Interact with a field.
8. Execute required form JavaScript.
9. Save the document.
10. Reopen it and verify the value.

This is the first major technical milestone.

If this fails, investigate the PDFium integration before proceeding.

### Validation

Using a known XFA test document:

- [x] PDFium initializes successfully.
- [x] The document opens without errors.
- [x] The document is detected as an XFA form.
- [x] At least one page renders correctly.
- [x] The XFA form environment initializes.
- [x] A known field can be read.
- [x] The field value can be changed.
- [x] Required field events or JavaScript execute.
- [x] The document can be saved to a new file.
- [x] The saved file can be closed and reopened.
- [x] The modified field value remains after reopening.
- [x] The saved document can also be opened by another compatible PDF reader.

## Step 3 — Minimal Viewer

Create the Qt/QML application shell.

Implement:

- Open file.
- Page rendering.
- Scrolling.
- Zoom.
- Page navigation.
- Basic thumbnails.

Keep PDFium behind a C++ abstraction such as:

```text
PdfDocument
PdfPage
PdfForm
PdfRenderer
```

QML should not call PDFium directly.

### Validation

Using representative single-page and multi-page PDFs:

- [x] A PDF can be selected and opened from the UI.
- [x] Every page renders.
- [x] Pages can be scrolled without visual corruption.
- [x] Zoom in works.
- [x] Zoom out works.
- [x] Fit-to-page or equivalent initial sizing is usable.
- [x] The user can navigate directly between pages.
- [x] Thumbnails correspond to the correct pages.
- [x] Opening a second document correctly closes or replaces the first document.
- [x] QML contains no direct PDFium API calls.
- [x] Closing a document releases its resources without crashing.

## Step 4 — Forms

Support:

- Text fields.
- Checkboxes.
- Radio buttons.
- Dropdowns.
- Form keyboard navigation.
- AcroForm.
- XFA.

Add representative documents to the compatibility test corpus.

### Validation

Using representative AcroForm and XFA test documents:

- [x] Text fields can be read and edited.
- [x] Checkboxes can be checked and unchecked.
- [x] Radio button selections work correctly.
- [x] Dropdown values can be selected.
- [x] Keyboard focus can move between fields.
- [x] AcroForm values remain after save and reopen.
- [x] XFA values remain after save and reopen.
- [x] Required XFA calculations or events still occur after editing.
- [x] Editing one form type does not break support for the other.
- [x] Representative test PDFs are stored in the compatibility suite when licensing and privacy allow.
- [x] Form regressions can be reproduced with a specific test document.

Validation covers the original synthetic AcroForm and dynamic XFA control
fixtures; see [FORMS](FORMS.md) for commands and compatibility limits.

## Step 5 — Added Content

Create a simple application-owned object model:

```text
AddedObject
├── Text
├── Signature
└── Image
```

Every object should have at minimum:

```text
page
position
size
type
content
```

Objects must support:

- Create.
- Select.
- Move.
- Resize.
- Delete.

### Validation

For each supported added-object type:

- [x] The object can be created on a selected page.
- [x] The object appears at the expected position.
- [x] The object can be selected.
- [x] The object can be moved.
- [x] The object can be resized where applicable.
- [x] The object can be deleted.
- [x] Added text preserves its content.
- [x] An image can be added from a local file.
- [x] A handwritten or image-based signature can be added.
- [x] Added objects remain associated with the correct page.
- [x] Added objects do not modify unrelated existing PDF content.

## Step 6 — Save

Implement:

```text
Open
  ↓
Edit forms
  ↓
Add content
  ↓
Save As
  ↓
Close
  ↓
Reopen
  ↓
Verify
```

Preserving document behavior is more important than minimizing file size.

Never overwrite the original PDF by default during early development.

### Validation

Using documents containing both form changes and added content:

- [x] `Save As` creates a new PDF.
- [x] The source PDF remains unchanged.
- [x] The saved PDF can be reopened by the application.
- [x] Form values remain correct after reopening.
- [x] Added text remains present and correctly positioned.
- [x] Added signatures remain present and correctly positioned.
- [x] Added images remain present and correctly positioned.
- [x] Existing document content remains intact.
- [ ] Existing AcroForm behavior still works after saving.
- [x] Existing XFA behavior still works after saving where supported.
- [x] The resulting PDF opens correctly in at least one independent PDF reader.
- [x] A save failure produces an error instead of silently losing changes.

Implementation status: Save As works for ordinary PDFs and retains existing
AcroForm values, widgets, and script actions. Dynamic XFA copies preserve
values and pass the native calculation/edit/save probe, but additions on full
XFA are refused safely. Step 4 now verifies live viewer form edits, checked/unchecked states, native
focus traversal and independent saved values for AcroForm and dynamic XFA.
AcroForm widgets remain editable after saving, but the pinned native keystroke
path does not apply script text conversion; full AcroForm script behavior and
representative foreground XFA remain outstanding.
See [SAVING](SAVING.md).

## Step 7 — Safety

Treat every PDF as untrusted.

Test:

- Invalid PDFs.
- Corrupted PDFs.
- JavaScript-heavy PDFs.
- Unexpected XFA behavior.
- Very large documents.

Document JavaScript must remain isolated from unrestricted operating-system functionality.

### Validation

- [ ] Opening an invalid file produces a controlled error.
- [ ] Opening a corrupted PDF does not crash the application.
- [ ] A malformed form does not crash the application.
- [ ] Large documents can be opened without unreasonable memory growth.
- [ ] Closing a large document releases resources.
- [ ] PDF JavaScript cannot arbitrarily execute operating-system commands.
- [ ] PDF JavaScript cannot arbitrarily read local files.
- [ ] PDF JavaScript cannot arbitrarily write local files.
- [ ] PDF JavaScript cannot access environment variables.
- [ ] PDF JavaScript cannot make unrestricted network requests.
- [ ] Failures in document JavaScript do not crash the application.
- [ ] A PDF that causes a crash or security issue becomes a regression test when practical.

## Step 8 — MVP Release

Package the application for one primary desktop platform first.

Only expand platform packaging after the complete open → edit → save → reopen workflow is reliable.

### Validation

On a clean system or clean test environment:

- [ ] The application can be installed or launched from the produced package.
- [ ] No development environment is required to run it.
- [ ] A normal PDF can be opened.
- [ ] An AcroForm PDF can be completed and saved.
- [ ] A representative XFA PDF can be completed and saved.
- [ ] Text can be added to a PDF.
- [ ] A signature can be added to a PDF.
- [ ] The result can be saved and reopened.
- [ ] The application can complete the workflow without developer tools.
- [ ] No known release-blocking crashes or security regressions remain.

## MVP Success Criteria

The MVP is successful when a normal user can take a real-world AcroForm or XFA form, complete it, add a signature or extra text, save it, and open the resulting file again without needing another PDF editor.

The complete workflow must be reproducible:

```text
Open
  ↓
Fill
  ↓
Add
  ↓
Save
  ↓
Close
  ↓
Reopen
  ↓
Verify
```

A feature is not considered complete solely because its implementation exists. Its expected behavior must be demonstrable through repeatable validation, and regressions should be automated where practical.

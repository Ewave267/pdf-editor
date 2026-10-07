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

## Step 7 — Safety

Treat every PDF as untrusted.

Test:

- Invalid PDFs.
- Corrupted PDFs.
- JavaScript-heavy PDFs.
- Unexpected XFA behavior.
- Very large documents.

Document JavaScript must remain isolated from unrestricted operating-system functionality.

## Step 8 — MVP Release

Package the application for one primary desktop platform first.

Only expand platform packaging after the complete open → edit → save → reopen workflow is reliable.

## MVP Success Criteria

The MVP is successful when a normal user can take a real-world AcroForm or XFA form, complete it, add a signature or extra text, save it, and open the resulting file again without needing another PDF editor.

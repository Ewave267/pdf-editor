# PDF Form Editor

An open-source desktop application for completing, annotating, and adding content to PDF documents.

The goal is to make common PDF workflows simple and reliable without requiring users to understand the underlying PDF format or depend on proprietary PDF software.

## Goals

The application should allow users to:

- Open and view PDF documents.
- Complete standard AcroForm forms.
- Complete XFA forms, including forms using JavaScript.
- Add text anywhere on a page.
- Add handwritten or image-based signatures.
- Add checkmarks, drawings, highlights, and images.
- Save the resulting PDF.
- Reopen a saved document without losing changes.
- Work entirely offline.

PDFs should be treated as documents a user can easily **complete and augment**, rather than as immutable files.

## Non-Goals

The project does not aim to:

- Edit existing PDF text like a word processor.
- Reconstruct the original document layout.
- Replace fonts or rewrite existing paragraphs.
- Provide full document-authoring capabilities.
- Support cryptographic/digital signatures in the initial versions.

These may be reconsidered independently in the future.

## Architecture

The application is desktop-first and designed to remain cross-platform.

```text
┌─────────────────────────────┐
│       Qt Quick / QML        │
│                             │
│ Viewer · Toolbar · Editing  │
└──────────────┬──────────────┘
               │
               ▼
┌─────────────────────────────┐
│            C++20            │
│                             │
│ Document model              │
│ Application logic           │
│ Added-content model         │
│ Undo / redo                 │
│ File handling               │
└──────────────┬──────────────┘
               │
               ▼
┌─────────────────────────────┐
│           PDFium            │
│                             │
│ PDF rendering               │
│ AcroForm                    │
│ XFA                         │
│ JavaScript / V8             │
│ PDF saving                  │
└─────────────────────────────┘
```

### Languages and Technologies

- **C++20** — core application and PDF integration.
- **Qt Quick / QML** — desktop user interface.
- **PDFium** — PDF rendering, forms, XFA, and JavaScript execution.
- **CMake** — application build system.
- **GN / Ninja** — PDFium's upstream build system.

PDFium should remain isolated behind a small application-facing abstraction so that PDF-specific implementation details do not spread throughout the codebase.

## Content Model

Existing document content and content added by the application are treated differently.

```text
Existing PDF
├── PDF content
├── AcroForm fields
└── XFA fields

Application additions
├── Text
├── Signature
├── Image
├── Checkmark
├── Highlight
└── Drawing
```

PDFium owns existing PDF/form behavior.

The application owns newly added content until it is written back into the PDF.

## Security

PDF files are untrusted input.

Document JavaScript must not receive unrestricted access to:

- The filesystem
- Processes
- Environment variables
- Network resources
- Application internals

XFA and JavaScript support must be designed with sandboxing in mind from the beginning.

## Development Principles

### Intent first

Important behavior should have a clear reason for existing.

Architecture decisions that affect the direction of the project should be recorded in `docs/adr/`.

### Understanding matters

Code should not be accepted simply because it works.

Important components should remain understandable enough that maintainers can explain:

- What they do.
- Why they exist.
- What assumptions they make.
- What could break them.

AI-generated code follows the same rule.

### Real PDFs are the specification

Automated tests should use a growing collection of representative PDFs:

```text
tests/pdfs/
├── normal/
├── acroform/
├── xfa-static/
├── xfa-dynamic/
├── xfa-javascript/
└── malformed/
```

Every PDF that exposes a bug should become a regression test when licensing and privacy permit.

## Repository Structure

```text
/
├── README.md
├── CMakeLists.txt
│
├── src/
│   ├── app/
│   ├── pdf/
│   ├── document/
│   └── ui/
│
├── qml/
│
├── tests/
│   └── pdfs/
│
└── docs/
    ├── MVP.md
    ├── ROADMAP.md
    └── adr/
```

## Current Status

Early development.

The first objective is not a complete editor. It is proving that the chosen architecture can reliably open, render, interact with, modify, save, and reopen real-world PDF and XFA forms.

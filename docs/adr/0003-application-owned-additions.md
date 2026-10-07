# 0003 — Application-owned additions above rendered PDF pages

Date: 2026-10-06

Status: Accepted for step 5.

## Context

Text, images, and signatures need editable geometry and content without changing
existing PDF text, forms, or the source file. Saving is a separate MVP milestone.

## Decision

Keep a document-owned C++ collection of typed additions with stable IDs,
zero-based page association, top-left page-point geometry, and owned text/image
content. Paint transparent overlays above PDF rasters in main pages and
thumbnails. Perform hit testing and geometry validation in C++; QML translates
pointer positions through the current zoom and manages dialogs and gestures.

Import signature images using the same snapshot mechanism as images while
retaining a distinct signature type. Reset the collection on document replacement
or close. Ask before discarding populated collections in the UI.

## Alternatives

Writing objects into PDFium immediately would couple pointer gestures to the
isolated worker and alter document state before saving is available. Keeping
geometry only in QML delegates would lose additions when scrolling recreates
those delegates. The document-owned collection avoids both problems.

## Consequences

Edits never touch the isolated renderer or original PDF in this milestone.
Additions survive zoom and page delegate recreation, but not application exit.
Step 6 must serialize the same content and geometry into a newly saved PDF.
Image decoding occurs in the GUI process with a pixel limit; further resource
and security hardening belongs to step 7. Drawing signatures, undo, and richer
text/image controls can build on this model later.

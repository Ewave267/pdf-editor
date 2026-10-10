# 0004 — Isolated Save As with an immutable source snapshot

Date: 2026-10-06

Status: Accepted for the implemented step 6 scope; full validation remains pending.

## Context

Saving must retain existing PDF content and forms, add text/images/signatures,
avoid overwriting the original, and retain changes after failure. Viewer form
interaction has not yet been implemented. PDFium cannot edit full XFA-generated
pages with its ordinary page-object API.

## Decision

Snapshot the source into a private temporary directory when opening it. Bind
that same read-only snapshot into independent render and save workers. Generate
an addition PDF with Qt's embedded text/fonts and lossless images, then import
its pages as Form XObjects in the save worker. Map display coordinates into the
native cropped/rotated PDF page before insertion. Return bounded PDF bytes over
stdout and atomically commit them with `QSaveFile` in the application.

Reject the original filename and aliases. Track content revisions so successful
saves clear only the saved revision, while later edits stay dirty. Keep additions
and the live renderer intact after failed saves. Refuse additions on full XFA,
while permitting native XFA copies without additions.

## Alternatives

- Rendering the whole document to images loses existing text and form behavior.
- Mutating the live renderer before saving risks duplicate additions on repeat
  saves and a corrupted session after failure.
- Giving a parsing worker access to the destination folder grants unnecessary
  write access. Return bytes and let the application commit them instead.
- Flattening full XFA makes additions possible but discards its form behavior.

## Consequences

Saved additions become ordinary PDF content rather than separately editable
application objects. Rendering and saving consume separate worker resources.
Step 4 must provide viewer form interaction and transport live form changes into
the save baseline; the current snapshot represents existing persisted values.
Full XFA additions need a separate compatibility solution. Interactive AcroForm
validation and representative static/foreground XFA fixtures remain outstanding.

## Step 4 update — 2026-10-07

[ADR 0005](0005-native-form-events.md) replaces the original immutable save
baseline with a live native form snapshot. Additions still enter only the
independent save worker. Form input, calculations and exact saved values are
now tested in the viewer; full XFA additions and the pinned AcroForm script
conversion limitation remain outstanding.

Follow-up (2026-10-10): the AcroForm conversion limitation is fixed by patch 0004
and a mixed-case input regression. Idle recovery and page/print exports have
separate documented contracts; see DESKTOP-EXPERIENCE and DOCUMENT-WORKFLOW.

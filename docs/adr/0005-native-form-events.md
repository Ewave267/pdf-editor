# 0005 — Native form events and live save snapshots

Date: 2026-10-07

Status: Accepted for step 4's synthetic AcroForm and dynamic XFA fixtures.

## Context

Forms must preserve native field behavior and XFA calculation scripts. Replacing
widgets with independent QML controls would duplicate PDFium's state and event
semantics. The immutable source snapshot from ADR 0004 cannot contain edits made
in the live renderer.

## Decision

Keep native pages and their form environments alive in the isolated renderer.
Forward bounded input commands from a Qt Quick item through the document's
serialized asynchronous queue. Let PDFium render its own fields, traverse focus,
commit values and execute scripts. Map display coordinates to PDF page space;
full XFA uses its top-left layout coordinates and matching popup viewport bounds.
Invalidate cached page and thumbnail images after form events.

Before Save As, commit native focus and serialize the live form document into a
private snapshot. Bind it read-only into the independent save worker, which
imports current additions and returns output bytes for atomic host commit.
Track form revisions separately from addition revisions and block form input
while saving. This updates ADR 0004's save baseline without inserting additions
into the renderer or exposing an output folder to document scripts.

## Alternatives

Independent QML replicas would require reproducing validation, calculation,
radio exclusivity, focus and choice behavior. Saving the original input alone
would silently discard field edits. Inserting additions into the renderer would
make repeated saves accumulate duplicates.

## Consequences

Native input and rendering share the existing worker's resource limits and
sandbox. A failed destination write retains live form state. A renderer crash
still loses in-memory field edits; recovery is future work. Full XFA additions,
dynamic page-count changes, complex input methods and broader production-form
compatibility need further work. The pinned PDFium AcroForm keystroke conversion
limitation is recorded in [FORMS](../FORMS.md).

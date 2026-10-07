# 0002 — Asynchronous viewer with an isolated render worker

Date: 2026-10-06

Status: Accepted for the Linux development viewer.

## Context

Step 3 needs responsive scrolling, zoom and thumbnails without exposing PDFium
or document JavaScript to QML or the desktop process. Step 2 already provides
a patched library and denied-action host callbacks.

## Decision

Expose a C++ `PdfDocument` object and `PdfPageItem` to the Qt/QML shell. Keep
all PDFium handles and calls in a persistent Bubblewrap-isolated worker.
Exchange page metadata and bounded PNG renders over a serialized JSON-line
protocol. Render requests run asynchronously, coalesce identical page/width
requests and cancel obsolete queued listeners. Use a 64 MiB host image cache
and viewport-limited ListView delegates.

Share PDFium library initialization and denied-action callbacks with the XFA
probe. Reset page caches and kill or gracefully stop the old worker before
opening a replacement document. A failed sandbox startup never falls back to
in-process rendering. The viewer is an opt-in CMake target so the lightweight
foundation build remains usable without Qt.

## Alternatives

- Rendering on the UI thread is simpler but blocks input and scrolling.
- An in-process background thread improves responsiveness but gives untrusted
  document scripts the desktop process's privileges and crash impact.
- A fresh process for each page repeats document parsing and XFA layout; keep
  one isolated worker per open document instead.

## Consequences

The first viewer is Linux-only and needs Bubblewrap user namespaces. Qt runtime
libraries are read-only inputs inside the worker. Render resolution and input
size are bounded, and errors are shown in the viewer. Form interactions and
saving need future protocol additions; they are not exposed by this viewer.
Platform packaging and stronger resource/security policies remain later work.

See [VIEWER](../VIEWER.md) for build commands, limits and validation.

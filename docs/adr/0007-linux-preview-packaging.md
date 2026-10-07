# 0007 — Relocatable Linux preview archive

Date: 2026-10-07

Status: Accepted for preview packaging; final MVP release gate remains open.

## Decision

Package the real Qt viewer with CMake install rules and CPack TGZ. Keep the GUI
and worker together in `bin`, patched PDFium in `lib/pdf-form-editor`, and notices,
desktop metadata and the icon in `share`. Prefer the installed PDFium path over
the development path. Set the worker install RPATH relative to its executable;
remove development RPATHs from installed GUI binaries. Worker mount paths and
kernel restrictions remain unchanged.

Use distribution-provided Qt, QML modules, Bubblewrap and operating-system
libraries. This avoids adding another library/plugin deployment mechanism to
the worker sandbox. Each binary is specific to its build distribution and
architecture; a Linux filename alone does not promise universal compatibility.

## Validation and consequences

Build a separate Release artifact locally. Validate an extracted, relocated copy
with system runtime linkage, offscreen GUI startup and the existing integration
harness using the shipped renderer/PDFium. The harness and independent reader are
validation tools and are excluded from the package. CI builds a separate Ubuntu
24.04 preview artifact and runs the same checks before uploading it.

Clean desktop testing, complete corresponding-source preparation and step 6
compatibility remain gates before public release. Archive generation must not
mark those gates complete. Full XFA additions are still refused rather than
producing a misleading saved document.

# Remaining roadmap tasks (cryptographic signing/validation excluded)

- [x] Production repeated-row multi-page growth/shrink and repeated saves; native
      and independent-reader checks detect lost controls, values and headings.
- [x] Broader licensed static/scripted/Unicode fixture admission and scenarios;
      per-fixture expected results and CI reporting identify operation failures.
- [x] Saved-form inspection and comparison in a fresh isolated process; mismatch,
      incomplete inspection, malformed output and timeout coverage.
- [x] Atomic verified save integration and user-facing passed/failed/unchecked
      report; failed checks retain edits and previous destination.
- [x] Signature/certification presence detection and operation warnings with
      cancellation; keep unsafe XFA operations blocked.
- [x] Large-document opening/render/input timing baselines with bounded tests.
- [x] Local dependency/advisory review and scoped corresponding-source archive
      preparation; exact native runtime advisory/source closure remains a gate.
- [x] Local Qt 6.11.2 relocated tarball and AppImage validation.
- [ ] Windows/macOS/ARM native execution; no remote workflows requested.
- [ ] External production acceptance: clean desktops, native accessibility,
      physical printers, platform signing/notarization and source distribution.

After each implementation slice: focused tests, relevant build/checks, review,
update this record, commit. Run the full six-gate suite at integration checkpoints.

Local commits only (user preference). Cryptographic signing and validation are
excluded. Genuine signed/certified interoperability, native-platform acceptance,
full vendor advisory/source closure and physical printing remain external gates;
the signature test markers deliberately contain invalid bytes.

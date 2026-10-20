# Remaining roadmap tasks (cryptographic signing/validation excluded)

- [ ] Production repeated-row multi-page growth/shrink and repeated saves; native
      and independent-reader checks detect lost controls, values and headings.
- [ ] Broader licensed static/scripted/Unicode fixture admission and scenarios;
      per-fixture expected results and CI reporting identify operation failures.
- [ ] Saved-form inspection and comparison in a fresh isolated process; mismatch,
      incomplete inspection, malformed output and timeout coverage.
- [ ] Atomic verified save integration and user-facing passed/failed/unchecked
      report; failed checks retain edits and previous destination.
- [ ] Signature/certification presence detection and operation warnings with
      cancellation; keep unsafe XFA operations blocked.
- [ ] Large-document opening/render/input timing baselines with bounded tests.
- [ ] Dependency/security release review and corresponding-source preparation.
- [ ] Local package validation; cross-platform CI when authorized.
- [ ] External production acceptance: clean desktops, native accessibility,
      physical printers, platform signing/notarization and source distribution.

After each implementation slice: focused tests, relevant build/checks, review,
update this record, commit. Run the full six-gate suite at integration checkpoints.

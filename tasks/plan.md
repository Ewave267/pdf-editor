# Remaining roadmap implementation, excluding cryptographic signatures

Authorized 2026-10-19: complete remaining planned work; retain deferred ADR 0007
features. Cryptographic PDF signing and signature trust/validity validation are
excluded. Detecting existing signature presence and warning about modifications
remain included. Continue with small verified commits without re-requesting plan
approval; the user requested local commits only: do not push or run remote workflows.

## Order and acceptance

1. Extend real-world repeated-row pagination and field scenarios. Verify native
   input/save/reopen and independent PDF.js state/layout; preserve fixture bytes,
   provenance and explicit reader differences. Add static/scripted/Unicode cases
   only with established redistribution rights.
2. Verify saved output in a fresh isolated worker before destination replacement.
   Capture committed form expectations, compare identities/types/values and
   XFA packets where available, fail atomically on mismatch or reopen failure,
   retain edits and report passed/failed/unavailable checks accurately. Reuse
   QSaveFile, sandbox launch arguments and worker limits; no new PDF parser.
3. Detect existing signatures/certification and warn before save, additions,
   page export and flattening. Cancellation leaves state unchanged; unsafe XFA
   operations remain blocked. Visual marks remain distinct from digital signing.
4. Record reproducible large-document interaction/render baselines and enforce
   bounded regression checks without claiming arbitrary hardware performance.
5. Review dependency/security coverage, prepare corresponding-source/build-input
   distribution and platform signing hooks where feasible, and validate local
   packages. Run native CI if authorized. Record genuine external gates with
   exact evidence still needed; do not mark them passed by documenting them.

## Trust and failure boundaries

PDF parsing and inspection stay in bounded isolated workers. Field data is
private; reports expose check scope and counts, never values in logs. Verification
must finish before QSaveFile destination commit. A failed verification preserves
the previous destination and current editable state. Signature detection makes no
cryptographic validity/trust claim. Source provenance/licensing is checked before
admitting downloaded fixtures.

## External acceptance

Windows/macOS/ARM native execution, clean desktop/screen-reader walkthroughs,
physical printing, production signing/notarization credentials and complete
advisory/source review require platform/service access or external evidence.
Prepare automation and reviewable artifacts; retain unfinished gates explicitly.

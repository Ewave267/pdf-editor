# ADR 0008: prioritize form preservation and cryptographic signatures

Status: accepted planning decision, 2026-10-12. Supersedes only the cryptographic
signature deferral in [ADR 0007](0007-advanced-feature-evaluation.md).

## Context

The user requested explicit next steps for a legally redistributable real-world
compatibility suite, reliable dynamic XFA filling/saving, cryptographic signatures,
operation warnings and a user-facing save compatibility check. Existing native
and independent-reader round-trip tests provide a foundation, but do not establish
arbitrary production-document compatibility.

## Decision

Plan the ordered milestones and acceptance criteria in
[ROADMAP](../ROADMAP.md#next-steps--form-and-signature-reliability-2026-10-12).
Extend the existing renderer, workers and PDF save paths. Verify supported form
fields and values by reopening staged output before atomic destination commit;
report failed and unavailable checks explicitly. Design cryptographic signing and
trust-chain validation before implementation, preserving prior signed revisions
and distinguishing signature validity from certificate trust.

## Alternatives

A new rendering engine or file format would expand scope without proving form
preservation. Reopening alone as a universal compatibility certificate would
overstate what can be checked. Keeping cryptographic signatures deferred would
conflict with the requested next steps.

## Consequences

These features remain planned, not implemented. Compatibility claims must name
the checks and their limits; script, layout and external-reader behavior still
need separate evidence. Existing source-preservation, isolation and production
release gates remain required. Other ADR 0007 decisions remain unchanged.

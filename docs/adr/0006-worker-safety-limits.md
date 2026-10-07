# 0006 — Worker syscall restrictions, allocation limits and controlled failure

Date: 2026-10-07

Status: Accepted; validated on Linux x86-64 with the synthetic safety corpus.

## Context

Namespace isolation and denied PDFium host callbacks were already present.
Workers still had no writable-data limit, and the kernel did not explicitly
prevent command execution or new sockets. Renderer failure cleared all
application-owned edits. Host source copying and integer conversion of unusual
raster geometry also needed explicit bounds.

## Decision

Install a shared, fail-closed Linux seccomp policy and hard resource limits at
native worker entry, before engine threads or document parsing. Deny execution,
process creation, sockets, namespace changes and privileged inspection APIs.
Permit V8 threads while inheriting restrictions. Deny renderer/save writable
opens and filesystem mutations; the development probe may still write its
private artifact mount.

Cap writable data at 768 MiB using `RLIMIT_DATA`. Address-space limits are
unsuitable for V8's huge reserved `PROT_NONE` ranges. Check both direct anonymous
allocation and commitment of a reserved range in the native policy harness.
Retain existing input/raster/output/deadline limits, bound source copying and
native command reading, and cap pending form commands.

On worker failure, stop the process tree, clear active operations and keep
application-owned additions. Explicitly report unrecoverable native form edits.
Validate real hanging scripts at startup, field exit and save time, rather than
only mocking timeout behavior. Store reproducible malicious/malformed fixtures.

## Alternatives

Callback denial alone would not restrict a compromised native worker. Disabling
all document JavaScript would break required XFA calculations. A strict syscall
allowlist is more restrictive, but needs a separate audited compatibility effort
across supported runtimes. A virtual-address cap conflicts with V8 reservation;
a writable-data cap preserves normal execution while bounding tested allocation.

## Consequences

This introduces no new external dependency or PDFium rebuild. It does require
Linux seccomp and the existing namespace sandbox to work; failures produce errors.
Qt and V8 must retain their tested threading/file-read behavior under the policy.
The safety suite takes about 100 seconds including prior gates and actual host
deadlines. Kernel vulnerabilities, complete syscall allowlisting, arbitrary
production compatibility and recovery of native form edits remain outside this
MVP gate. See [SAFETY](../SAFETY.md) for exact boundaries and validation evidence.

# Browser runtime and worker isolation

`start.sh` checks Bubblewrap before starting a cookie-authenticated local X
server, Openbox, x11vnc (container-loopback only), websockify/noVNC and Qt.
It fails closed if nested namespaces are blocked, supervises the services and
cleans up on termination. Tini forwards signals and reaps orphaned processes.
Compose publishes the browser endpoint on host localhost only.

The container runs non-root, drops all host capabilities, uses a read-only image
and temporary writable home/runtime directories. Host files persist through
`~/host_fs`, a writable bind mount of the host root, and `/documents`. Startup
resolves the configured UID in host `/etc/passwd` to initialize the file pickers;
no `HOST_HOME` variable is needed. SELinux labeling is disabled for this container
to allow the explicitly requested host-root access, without relabeling the host.
The GUI has the configured host user's file permissions; workers do not receive
the root mount.
Match the container UID/GID to the host user for usable saved-file ownership.

## Reviewed Docker seccomp profile

`seccomp-default.json` is the upstream Moby default at revision
`2ceae35d351c156cb5a8efc0fdc4a08cf94569d8`:
https://github.com/moby/profiles/blob/2ceae35d351c156cb5a8efc0fdc4a08cf94569d8/seccomp/default.json
Its Apache-2.0 license is retained in `MOBY-PROFILES-LICENSE`.

`tools/generate_docker_seccomp.py` appends one allow rule for `clone`, `unshare`,
`mount`, `umount2`, `pivot_root` and `setns`, producing `seccomp.json`. All other
upstream rules remain intact. These calls let Bubblewrap create private user,
PID, network and mount namespaces. This expands the outer container's syscall
permissions. The user explicitly approved creation and testing on 2026-10-07,
after automatic approval review initially rejected that expansion.

Host capabilities remain dropped. Before PDF parsing the worker installs its
own stricter filter, denying execution, process creation, sockets, namespace and
mount changes, tracing and file writes. The worker receives an empty `/proc`
and only read-only `/dev/null`, `/dev/zero`, `/dev/urandom`, `/dev/random`. It does
not need a fresh procfs or terminal-device mount, and Docker's masked paths stay
in place. The same narrower filesystem is used by the development XFA probe.

Local runtime and hostile-document checks passed on a Fedora Linux Docker host.
Additional AppArmor/user-namespace restrictions on other hosts may block startup;
no unconfined AppArmor, unconfined seccomp, privileged mode or unsandboxed PDF
fallback is configured. Other host policies must be reviewed before adjustment.

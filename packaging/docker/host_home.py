#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Resolve the container UID's host home through a mounted Linux root."""
import os
from pathlib import Path, PurePosixPath
import sys


def host_home(root: Path, uid: int) -> Path:
    for line in (root / "etc/passwd").read_text().splitlines():
        fields = line.split(":")
        if len(fields) != 7 or not fields[2].isdigit() or int(fields[2]) != uid:
            continue
        home = PurePosixPath(fields[5])
        if not home.is_absolute() or ".." in home.parts:
            raise ValueError("host account has an invalid home path")
        candidate = root.joinpath(*home.parts[1:])
        if candidate.is_dir() and os.access(candidate, os.R_OK | os.X_OK):
            return candidate
    # Directory-service accounts may not appear in /etc/passwd.
    homes = root / "home"
    candidates = [p for p in homes.iterdir()
                  if p.is_dir() and p.stat().st_uid == uid
                  and os.access(p, os.R_OK | os.X_OK)] if homes.is_dir() else []
    if len(candidates) == 1:
        return candidates[0]
    raise ValueError(f"cannot identify an accessible host home for UID {uid}; "
                     "match PDF_EDITOR_UID/GID to the host user")


if __name__ == "__main__":
    try:
        print(host_home(Path(sys.argv[1]), os.getuid()))
    except (OSError, ValueError) as error:
        print(f"Host home detection failed: {error}", file=sys.stderr)
        sys.exit(1)

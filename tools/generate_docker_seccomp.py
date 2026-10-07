#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Derive the reviewed Bubblewrap Docker policy from the vendored Moby default."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / 'packaging/docker'
EXTRA_SYSCALLS = ['clone', 'unshare', 'mount', 'umount2', 'pivot_root', 'setns']


def generate():
    policy = json.loads((DIRECTORY / 'seccomp-default.json').read_text())
    policy['syscalls'].append({'names': EXTRA_SYSCALLS, 'action': 'SCMP_ACT_ALLOW'})
    return json.dumps(policy, indent=2) + '\n'


if __name__ == '__main__':
    (DIRECTORY / 'seccomp.json').write_text(generate())

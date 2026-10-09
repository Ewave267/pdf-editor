# SPDX-License-Identifier: GPL-3.0-only
"""Collect the transitive app-local DLL imports of a Windows worker."""
from pathlib import Path
import re
import shutil
import subprocess


def imported_dlls(binary):
    # MSVC lists both normal and delay-load dependencies with /DEPENDENTS.
    result = subprocess.run(["dumpbin", "/nologo", "/dependents", str(binary)],
                            check=True, capture_output=True, text=True)
    return re.findall(r"^\s+([\w.+-]+\.dll)\s*$", result.stdout, re.MULTILINE | re.IGNORECASE)


def collect_worker_runtime(worker, destination, system_directory, imports=imported_dlls):
    worker = Path(worker)
    destination = Path(destination)
    available = {p.name.casefold(): p for p in worker.parent.iterdir() if p.is_file()}
    system = {p.name.casefold() for p in Path(system_directory).iterdir() if p.is_file()}
    pending = [worker]
    selected = {}
    while pending:
        binary = pending.pop()
        for dependency in imports(binary):
            name = dependency.casefold()
            if not re.fullmatch(r"[\w.+-]+\.dll", name):
                raise RuntimeError(f"Invalid DLL dependency: {dependency}")
            # App-local CRTs take precedence over any runner-installed version.
            if name in available:
                if name not in selected:
                    selected[name] = available[name]
                    pending.append(available[name])
            elif name not in system and not name.startswith(("api-ms-", "ext-ms-")):
                raise RuntimeError(f"Missing worker dependency {dependency}, imported by {binary.name}")
    destination.mkdir(parents=True, exist_ok=False)
    shutil.copy2(worker, destination / worker.name)
    for binary in selected.values():
        shutil.copy2(binary, destination / binary.name)
    return {"dll_count": len(selected),
            "dll_bytes": sum(p.stat().st_size for p in selected.values()),
            "dlls": sorted(p.name for p in selected.values())}

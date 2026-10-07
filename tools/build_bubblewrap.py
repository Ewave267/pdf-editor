#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build pinned non-setuid Bubblewrap against the native Linux build baseline."""
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
from fetch_native_inputs import download

ROOT = Path(__file__).resolve().parents[1]


def main():
    pin = json.loads((ROOT / "packaging/native/bubblewrap.json").read_text())
    deps = ROOT / ".deps"
    deps.mkdir(exist_ok=True)
    archive = deps / "bubblewrap-source.tar.gz"
    download(pin["url"], archive, pin["sha256"])
    source = deps / f"bubblewrap-{pin['revision']}"
    if not source.exists():
        with tarfile.open(archive) as handle:
            handle.extractall(deps, filter="data")
    build = deps / "bubblewrap-build"
    if not (build / "build.ninja").exists():
        subprocess.run(["meson", "setup", str(build), str(source), "--buildtype=release",
                        "-Dman=disabled", "-Dtests=false", "-Dselinux=enabled",
                        "-Dbash_completion=disabled", "-Dzsh_completion=disabled"], check=True)
    subprocess.run(["meson", "compile", "-C", str(build), "-j", "4"], check=True)
    help_text = subprocess.check_output([str(build / "bwrap"), "--help"], text=True)
    if "--size" not in help_text:
        raise RuntimeError("Bubblewrap lacks bounded tmpfs support")
    notices = build / "notices"
    notices.mkdir(exist_ok=True)
    shutil.copy2(source / "COPYING", notices / "COPYING")
    (notices / "build-info.json").write_text(json.dumps(pin, indent=2) + "\n")
    print(f"Built pinned Bubblewrap: {build / 'bwrap'}")


if __name__ == "__main__":
    main()

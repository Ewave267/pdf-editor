#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate relocated native Linux tarball/AppImage payloads and worker isolation."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact", type=Path)
    parser.add_argument("--harness", type=Path, required=True, help="Matching viewer-tests executable")
    parser.add_argument("--max-glibc", help="Reject packages newer than this host baseline, e.g. 2.34")
    args = parser.parse_args()
    artifact = args.artifact.resolve()
    with tempfile.TemporaryDirectory(prefix="pdf-editor-relocated-native-") as temp:
        relocation = Path(temp) / "folder with spaces"
        relocation.mkdir()
        if artifact.name.endswith(".tar.gz"):
            with tarfile.open(artifact) as archive:
                archive.extractall(relocation, filter="data")
            roots = list(relocation.iterdir())
            if len(roots) != 1 or not roots[0].is_dir():
                raise RuntimeError("Tarball must contain one top-level directory")
            root = roots[0]
        elif artifact.suffix == ".AppImage":
            subprocess.run([str(artifact), "--appimage-extract"], cwd=relocation,
                           stdout=subprocess.DEVNULL, check=True)
            root = relocation / "squashfs-root"
        else:
            parser.error("expected .tar.gz or .AppImage")
        manifest = json.loads((root / "release-info.json").read_text())
        if args.max_glibc and tuple(map(int, manifest["glibc_minimum"].split("."))) > tuple(map(int, args.max_glibc.split("."))):
            raise RuntimeError(f"Package requires glibc {manifest['glibc_minimum']}; target maximum is {args.max_glibc}")
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software")
        for key in ("LD_LIBRARY_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "QT_PLUGIN_PATH",
                    "QT_QPA_PLATFORM_PLUGIN_PATH", "FONTCONFIG_FILE"):
            env.pop(key, None)
        subprocess.run([str(root / "pdf-editor"), "--version"], env=env, check=True)
        with tempfile.TemporaryFile() as diagnostics:
            process = subprocess.Popen([str(root / "pdf-editor")], env=env, stderr=diagnostics)
            try:
                process.wait(timeout=3)
                raise RuntimeError(f"Native GUI exited prematurely: {process.returncode}")
            except subprocess.TimeoutExpired:
                pass
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=5)
            diagnostics.seek(0)
            errors = diagnostics.read().decode(errors="replace")
            if "failed to load component" in errors.lower():
                raise RuntimeError(errors)
        # Package libraries are resolved first. The harness uses the exact same
        # PdfDocument library, while production binaries remain unmodified.
        shutil.copy2(args.harness.resolve(), root / "bin/viewer-tests")
        probe = args.harness.resolve().parent / "xfa-probe-worker"
        if probe.is_file():
            shutil.copy2(probe, root / "bin/xfa-probe-worker")
        env["PATH"] = str(root / "bin") + os.pathsep + env.get("PATH", "")
        env["LD_LIBRARY_PATH"] = str(root / "lib/runtime") + ":" + str(root / "lib/pdf-form-editor")
        env["QT_PLUGIN_PATH"] = str(root / "plugins")
        env["QML_IMPORT_PATH"] = str(root / "qml")
        env["QML2_IMPORT_PATH"] = str(root / "qml")
        subprocess.run([str(root / "bin/viewer-tests")], env=env, check=True)
    print(f"Relocated native GUI and PDF integration checks passed: {artifact.name}")


if __name__ == "__main__":
    main()

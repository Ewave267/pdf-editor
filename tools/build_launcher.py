#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Cross-build self-contained Go launchers and release archives (developer tool)."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TARGETS = ("linux-amd64", "linux-arm64", "windows-amd64", "windows-arm64", "darwin-amd64", "darwin-arm64")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--go", default="go", help="Go executable (Go 1.23+)")
    parser.add_argument("--target", action="append", choices=TARGETS)
    parser.add_argument("--output", type=Path, default=ROOT / "dist/launcher")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    goroot = Path(subprocess.check_output([args.go, "env", "GOROOT"], cwd=ROOT, text=True).strip())
    checksums = []
    for target in args.target or TARGETS:
        goos, goarch = target.split("-")
        name = f"pdf-editor-{target}" + (".exe" if goos == "windows" else "")
        binary = args.output / name
        env = dict(os.environ, GOOS=goos, GOARCH=goarch, CGO_ENABLED="0")
        print(f"Building {target}", flush=True)
        subprocess.run([args.go, "build", "-trimpath", "-ldflags=-s -w", "-o", str(binary),
                        "./cmd/pdf-editor"], cwd=ROOT, env=env, check=True)
        archive = args.output / f"pdf-editor-{target}.zip"
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
            bundle.write(binary, name)
            for source, label in [(ROOT / "docs/LAUNCHER.md", "README.md"),
                                  (ROOT / "LICENSE", "LICENSE"),
                                  (ROOT / "packaging/docker/MOBY-PROFILES-LICENSE", "MOBY-PROFILES-LICENSE"),
                                  (goroot / "LICENSE", "GO-LICENSE")]:
                bundle.write(source, label)
        checksums.append(f"{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}")
    source_archive = args.output / "pdf-editor-launcher-source.zip"
    sources = [ROOT / "go.mod", ROOT / "launcher_assets.go", ROOT / "LICENSE",
               ROOT / "tools/build_launcher.py", ROOT / "tools/generate_docker_seccomp.py",
               ROOT / "docs/LAUNCHER.md", ROOT / "docs/RELEASE.md"]
    sources += list((ROOT / "cmd").rglob("*.go")) + list((ROOT / "internal").rglob("*.go"))
    sources += [ROOT / "packaging/docker" / name for name in
                ("seccomp.json", "seccomp-default.json", "MOBY-PROFILES-LICENSE", "README.md")]
    with zipfile.ZipFile(source_archive, "w", compression=zipfile.ZIP_DEFLATED) as bundle:
        for source in sorted(sources):
            bundle.write(source, source.relative_to(ROOT))
    checksums.append(f"{hashlib.sha256(source_archive.read_bytes()).hexdigest()}  {source_archive.name}")
    (args.output / "SHA256SUMS").write_text("\n".join(checksums) + "\n")
    print(f"Launchers and archives: {args.output}")


if __name__ == "__main__":
    main()

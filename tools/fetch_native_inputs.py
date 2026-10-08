#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fetch verified AppImage inputs and upstream Qt sources/notices for CI."""
import argparse
import hashlib
import json
import platform
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def download(url, path, checksum=None):
    if not path.exists():
        request = urllib.request.Request(url, headers={"User-Agent": "PDF-Editor-native-build"})
        with urllib.request.urlopen(request, timeout=120) as response, path.open("wb") as output:
            while block := response.read(1024 * 1024):
                output.write(block)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if checksum and digest != checksum:
        path.unlink()
        raise RuntimeError(f"Checksum mismatch: {url}")
    return digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--appimage", action="store_true")
    parser.add_argument("--portable-libgcc", action="store_true")
    parser.add_argument("--qt-version")
    parser.add_argument("--output", type=Path, default=ROOT / ".deps/release-inputs")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    architecture = platform.machine().lower()
    if (args.appimage or args.portable_libgcc) and architecture not in ("x86_64", "aarch64"):
        parser.error("Linux runtime inputs require x86_64 or aarch64")
    suffix = "-aarch64" if architecture == "aarch64" else ""
    if args.appimage:
        pin = json.loads((ROOT / f"packaging/native/appimage-runtime{suffix}.json").read_text())
        download(pin["url"], args.output / "appimage-runtime", pin["sha256"])
        download(pin["license_url"], args.output / "APPIMAGE-LICENSE", pin["license_sha256"])
    if args.portable_libgcc:
        pin = json.loads((ROOT / f"packaging/native/libgcc-runtime{suffix}.json").read_text())
        archive = args.output / "libgcc-runtime.rpm"
        download(pin["url"], archive, pin["sha256"])
        subprocess.run(["rpm", "--checksig", str(archive)], check=True)
        extraction = (args.output / "libgcc-runtime").resolve()
        extraction.mkdir(exist_ok=True)
        with subprocess.Popen(["rpm2cpio", str(archive.resolve())], stdout=subprocess.PIPE) as converter:
            try:
                subprocess.run(["cpio", "-id", "--quiet", "--no-absolute-filenames"],
                               cwd=extraction, stdin=converter.stdout, check=True)
            finally:
                converter.stdout.close()
            if converter.wait() != 0:
                raise RuntimeError("Cannot extract portable libgcc runtime")
        notices = args.output / "libgcc-notices"
        shutil.copytree(extraction / "usr/share/licenses/libgcc", notices, dirs_exist_ok=True)
        (notices / "build-info.json").write_text(json.dumps(pin, indent=2) + "\n")
    if args.qt_version:
        if not re.fullmatch(r"\d+\.\d+\.\d+", args.qt_version):
            parser.error("invalid Qt version")
        series = ".".join(args.qt_version.split(".")[:2])
        sources = args.output / "qt-sources"
        notices = args.output / "qt-notices"
        sources.mkdir(exist_ok=True)
        notices.mkdir(exist_ok=True)
        checksums = []
        for module in ("qtbase", "qtdeclarative", "qtsvg", "qtimageformats", "qtshadertools"):
            filename = f"{module}-everywhere-src-{args.qt_version}.tar.xz"
            url = f"https://download.qt.io/archive/qt/{series}/{args.qt_version}/submodules/{filename}"
            sidecar = urllib.request.urlopen(url + ".sha256", timeout=120).read().decode()
            expected = sidecar.split()[0]
            if not re.fullmatch(r"[0-9a-f]{64}", expected):
                raise RuntimeError(f"Invalid Qt checksum sidecar: {url}")
            checksum = download(url, sources / filename, expected)
            checksums.append(f"{checksum}  {filename}\n")
            with tarfile.open(sources / filename) as archive:
                for entry in archive:
                    if not entry.isfile():
                        continue
                    path = PurePosixPath(entry.name)
                    if path.is_absolute() or ".." in path.parts or len(path.parts) < 2:
                        continue
                    upper = path.name.upper()
                    if "LICENSES" not in path.parts and not upper.startswith(("LICENSE", "COPYING", "COPYRIGHT", "NOTICE")) and path.name != "qt_attribution.json":
                        continue
                    destination = notices / module / Path(*path.parts[1:])
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    destination.write_bytes(archive.extractfile(entry).read())
        (sources / "SHA256SUMS").write_text("".join(checksums))
        (notices / "SOURCE-INFO.txt").write_text(
            f"Unmodified Qt {args.qt_version} sources: official download.qt.io archive.\n"
            "Matching source archives/checksums are provided as separate workflow artifacts.\n")


if __name__ == "__main__":
    main()

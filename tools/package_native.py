#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bundle the real native Linux viewer as a relocatable tarball and AppImage.

Never downloads tools or builds PDFium implicitly. Use package_desktop.py for
native Windows/macOS candidates.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
CORE_LIBRARIES = re.compile(r"^(ld-linux.*|lib(c|m|pthread|dl|rt|resolv|nss_.*)\.so(?:\..*)?)$")


def run(command, **kwargs):
    return subprocess.run([str(item) for item in command], check=True, **kwargs)


def dependencies(binary):
    result = run(["ldd", binary], capture_output=True, text=True).stdout
    if "not found" in result:
        raise RuntimeError(f"Missing runtime dependency of {binary}:\n{result}")
    for line in result.splitlines():
        match = re.match(r"\s*(\S+) => (/.*?) \(", line)
        if match and not CORE_LIBRARIES.match(match[1]):
            yield match[1], Path(match[2])


def copy_notices(paths, destination):
    """Copy distro-provided notices for the files actually bundled."""
    packages = set()
    if shutil.which("rpm"):
        for path in paths:
            result = subprocess.run(["rpm", "-qf", "--qf", "%{NAME}", str(path)],
                                    capture_output=True, text=True)
            # An extracted Qt SDK has the same notices as the installed distro Qt.
            if result.returncode and "/usr/" in str(path):
                equivalent = "/usr/" + str(path).split("/usr/", 1)[1]
                result = subprocess.run(["rpm", "-qf", "--qf", "%{NAME}", equivalent],
                                        capture_output=True, text=True)
            if result.returncode == 0:
                packages.add(result.stdout.strip())
        for package in sorted(packages):
            notice = Path("/usr/share/licenses") / package
            if notice.is_dir():
                shutil.copytree(notice, destination / package, dirs_exist_ok=True)
    elif shutil.which("dpkg-query"):
        for path in paths:
            result = subprocess.run(["dpkg-query", "-S", str(path)],
                                    capture_output=True, text=True)
            if result.returncode == 0:
                for line in result.stdout.splitlines():
                    packages.add(line.split(": ", 1)[0].split(":", 1)[0])
        for package in sorted(packages):
            notice = Path("/usr/share/doc") / package / "copyright"
            if notice.is_file():
                (destination / package).mkdir(parents=True, exist_ok=True)
                shutil.copy2(notice, destination / package / "copyright")
    if not packages:
        raise RuntimeError("Cannot identify bundled dependency notices; use an RPM/DEB build host")
    return sorted(packages)


def stage(args, directory):
    run(["cmake", "--install", args.build_dir, "--prefix", directory])
    binary = directory / "bin/pdf-form-editor"
    worker = directory / "bin/pdf-render-worker"
    if not binary.is_file() or not worker.is_file():
        raise RuntimeError("Configure the real viewer with PDF_EDITOR_BUILD_VIEWER=ON first")
    qt = args.qt_runtime.resolve()
    for name in ("plugins", "qml"):
        if not (qt / name).is_dir():
            raise RuntimeError(f"Qt runtime folder missing: {qt / name}")
        shutil.copytree(qt / name, directory / name, symlinks=False)
    bwrap = shutil.which("bwrap")
    if not bwrap:
        raise RuntimeError("Bubblewrap is required on the packaging host")
    shutil.copy2(bwrap, directory / "bin/bwrap")
    (directory / "bin/bwrap").chmod(0o755)  # Never distribute a setuid executable.
    if "--size" not in run([bwrap, "--help"], capture_output=True, text=True).stdout:
        raise RuntimeError("Bubblewrap needs bounded tmpfs support; run tools/build_bubblewrap.py")
    if args.bwrap_notices:
        shutil.copytree(args.bwrap_notices, directory / "share/doc/pdf-form-editor/bubblewrap",
                        dirs_exist_ok=True)
    if args.libgcc_runtime:
        if not args.libgcc_notices:
            raise RuntimeError("--libgcc-notices is required for an overridden compiler runtime")
        shutil.copytree(args.libgcc_notices, directory / "share/doc/pdf-form-editor/libgcc",
                        dirs_exist_ok=True)
    originals = [Path(bwrap)]
    seeds = [binary, worker, directory / "bin/bwrap",
             directory / "lib/pdf-form-editor/libpdfium.so"]
    seeds += [p for name in ("plugins", "qml") for p in (directory / name).rglob("*.so")]
    runtime = directory / "lib/runtime"
    runtime.mkdir(parents=True)
    copied = {}
    for seed in seeds:
        for soname, source in dependencies(seed):
            if soname == "libgcc_s.so.1" and args.libgcc_runtime:
                source = args.libgcc_runtime.resolve()
            # Preserve PDFium's existing dedicated location.
            if soname == "libpdfium.so":
                continue
            source = source.resolve()
            if soname in copied and copied[soname] != source:
                raise RuntimeError(f"Conflicting dependency {soname}: {copied[soname]} and {source}")
            if soname not in copied:
                shutil.copy2(source, runtime / soname)
                copied[soname] = source
                originals.append(source)
    # ldd reports each seed's complete transitive closure, including plugins.
    originals += [p for name in ("plugins", "qml") for p in (qt / name).rglob("*.so")]
    fonts = directory / "share/fonts"
    fonts.mkdir(parents=True)
    for font in args.font_dir.rglob("*.ttf"):
        shutil.copy2(font, fonts / font.name)
        originals.append(font)
    if not list(fonts.glob("*.ttf")):
        raise RuntimeError("--font-dir must contain Liberation TrueType fonts")
    (fonts / "sandbox.conf").write_text('''<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig><dir>/runtime/fonts</dir><cachedir>/tmp/fontconfig</cachedir>
<alias><family>Arial</family><prefer><family>Liberation Sans</family></prefer></alias>
<alias><family>sans-serif</family><prefer><family>Liberation Sans</family></prefer></alias>
</fontconfig>
''')
    (fonts / "fontconfig.conf").write_text('''<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
<include ignore_missing="yes">/etc/fonts/fonts.conf</include>
<dir prefix="relative">.</dir><cachedir prefix="xdg">fontconfig</cachedir>
<alias><family>sans-serif</family><prefer><family>Liberation Sans</family></prefer></alias>
</fontconfig>
''')
    notices = directory / "share/doc/pdf-form-editor/runtime"
    notices.mkdir(parents=True)
    packages = copy_notices(originals, notices)
    shutil.copy2(ROOT / "packaging/native/AppRun", directory / "AppRun")
    (directory / "AppRun").chmod(0o755)
    (directory / "pdf-editor").symlink_to("AppRun")
    shutil.copy2(ROOT / "packaging/pdf-form-editor.svg", directory / "pdf-form-editor.svg")
    desktop = (ROOT / "packaging/pdf-form-editor.desktop").read_text()
    (directory / "pdf-form-editor.desktop").write_text(desktop.replace("Exec=pdf-form-editor", "Exec=AppRun"))
    shutil.copy2(ROOT / "LICENSE", directory / "LICENSE")
    shutil.copy2(ROOT / "docs/NATIVE-RELEASES.md", directory / "README.md")
    versions = set()
    for file in [*seeds, *runtime.iterdir(), Path(bwrap)]:
        dynamic = run(["readelf", "--version-info", file], capture_output=True, text=True).stdout
        versions.update(re.findall(r"\bGLIBC_(\d+\.\d+)\b", dynamic))
    minimum = max(versions, key=lambda v: tuple(map(int, v.split("."))))
    if tuple(map(int, minimum.split("."))) > tuple(map(int, args.max_glibc.split("."))):
        raise RuntimeError(f"Bundle requires glibc {minimum}; requested baseline is {args.max_glibc}. "
                           "Rebuild application, PDFium and dependencies on the baseline host. "
                           "Override --max-glibc only for a clearly identified local preview.")
    revision = run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    manifest = {"version": args.version, "platform": "linux-x86_64", "glibc_minimum": minimum,
                "build_host": platform.platform(), "application_revision": revision,
                "working_tree_modified": bool(run(["git", "status", "--porcelain"], cwd=ROOT,
                                                   capture_output=True, text=True).stdout),
                "bundled_packages": packages,
                "status": "candidate; clean-machine validation and complete corresponding source required"}
    (directory / "release-info.json").write_text(json.dumps(manifest, indent=2) + "\n")
    # Resolve the package with its own dependencies; no SDK/Qt directory needed.
    env = dict(os.environ, LD_LIBRARY_PATH=str(runtime) + ":" + str(directory / "lib/pdf-form-editor"))
    if run([binary, "--version"], env=env, capture_output=True, text=True).stdout.strip() != "PDF Form Editor 0.1.0":
        raise RuntimeError("Unexpected viewer version")
    for seed in seeds:
        result = run(["ldd", seed], env=env, capture_output=True, text=True).stdout
        if "not found" in result or ".deps" in result:
            raise RuntimeError(f"Non-relocatable dependency: {seed}\n{result}")
    print(f"Bundled Qt/PDFium/Bubblewrap; minimum glibc {minimum}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("linux", "windows", "macos"), default="linux")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build-release")
    parser.add_argument("--bwrap-notices", type=Path, help="Notices for a source-built Bubblewrap")
    parser.add_argument("--libgcc-runtime", type=Path, help="Pinned baseline-compatible libgcc_s.so.1")
    parser.add_argument("--libgcc-notices", type=Path, help="Notices for the overridden libgcc runtime")
    parser.add_argument("--qt-runtime", type=Path, help="Qt directory containing plugins/ and qml/")
    parser.add_argument("--font-dir", type=Path, help="Liberation fonts directory")
    parser.add_argument("--version", default="0.1.0-rc.1")
    parser.add_argument("--max-glibc", default="2.34", help="Maximum permitted glibc requirement (RHEL 9 baseline)")
    parser.add_argument("--output", type=Path, default=ROOT / "dist/native")
    parser.add_argument("--appimage-runtime", type=Path, help="Official runtime matching the checked-in SHA-256 pin")
    parser.add_argument("--appimage-license", type=Path, help="License distributed with the AppImage runtime")
    args = parser.parse_args()
    if args.target != "linux":
        parser.error("Use tools/package_desktop.py on a native Windows/macOS runner")
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        parser.error("Linux packaging currently requires an x86_64 Linux build host")
    if not args.qt_runtime or not args.font_dir:
        parser.error("--qt-runtime and --font-dir are required")
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", args.version):
        parser.error("invalid release version")
    if not re.fullmatch(r"\d+\.\d+", args.max_glibc):
        parser.error("--max-glibc must be a version such as 2.34")
    if args.appimage_runtime:
        pin = json.loads((ROOT / "packaging/native/appimage-runtime.json").read_text())
        if hashlib.sha256(args.appimage_runtime.read_bytes()).hexdigest() != pin["sha256"]:
            parser.error("AppImage runtime does not match the reviewed SHA-256 pin")
        if not args.appimage_license or not args.appimage_license.is_file():
            parser.error("--appimage-license is required with --appimage-runtime")
        if hashlib.sha256(args.appimage_license.read_bytes()).hexdigest() != pin["license_sha256"]:
            parser.error("AppImage license does not match the reviewed SHA-256 pin")
        if not shutil.which("mksquashfs"):
            parser.error("Install squashfs-tools on the packaging host")
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    name = f"pdf-editor-{args.version}-linux-x86_64"
    with tempfile.TemporaryDirectory(prefix="pdf-editor-native-") as temp:
        appdir = Path(temp) / name
        stage(args, appdir)
        if args.appimage_runtime:
            shutil.copy2(args.appimage_license, appdir / "share/doc/pdf-form-editor/runtime/APPIMAGE-LICENSE")
        archive = args.output / f"{name}.tar.gz"
        with tarfile.open(archive, "w:gz") as bundle:
            bundle.add(appdir, arcname=name)
        artifacts = [archive]
        if args.appimage_runtime:
            filesystem = Path(temp) / "payload.squashfs"
            run(["mksquashfs", appdir, filesystem, "-noappend", "-all-root", "-comp", "gzip", "-processors", "2"],
                stdout=subprocess.DEVNULL)
            image = args.output / f"{name}.AppImage"
            with image.open("wb") as output:
                output.write(args.appimage_runtime.read_bytes())
                with filesystem.open("rb") as input_file:
                    shutil.copyfileobj(input_file, output)
            image.chmod(0o755)
            artifacts.append(image)
        (args.output / "SHA256SUMS").write_text("".join(
            f"{hashlib.sha256(file.read_bytes()).hexdigest()}  {file.name}\n" for file in artifacts))
    print(f"Native artifacts: {args.output}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"Packaging failed: {error}")

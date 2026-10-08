#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deploy and validate native Windows/macOS release candidates, then ZIP them."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def run(arguments, **kwargs):
    return subprocess.run([str(arg) for arg in arguments], check=True, **kwargs)


def verify_gui(binary, environment):
    # A real QML window must survive startup, even when rendered offscreen.
    with tempfile.TemporaryFile() as diagnostics:
        process = subprocess.Popen([str(binary)], env=environment, stdout=diagnostics, stderr=diagnostics)
        exited = None
        try:
            exited = process.wait(timeout=4)
        except subprocess.TimeoutExpired:
            pass
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
        diagnostics.seek(0)
        output = diagnostics.read().decode(errors="replace")
        if exited is not None:
            raise RuntimeError(f"Packaged GUI exited during startup: {exited}\n{output}")
        if "failed to load component" in output.lower():
            raise RuntimeError(output)


def verify_worker_loader(worker, environment, results):
    # A loader failure also returns nonzero; that is not proof of isolation.
    # Require the policy's intentional rejection before testing the sandbox.
    result = subprocess.run([str(worker)], env=environment, capture_output=True, timeout=15)
    output = result.stdout + result.stderr
    (results / "worker-loader.log").write_bytes(output)
    expected = b"Worker requires"
    if result.returncode != 1 or expected not in output:
        raise RuntimeError(
            f"Packaged worker failed before its sandbox check: exit {result.returncode} "
            f"(0x{result.returncode & 0xffffffff:08x})\n{output.decode(errors='replace')}")


def run_smoke(test, fixtures, results, environment):
    # Keep startup diagnostics even when the temporary deployment is removed.
    log = results / "native-smoke.log"
    with log.open("wb") as output:
        try:
            result = subprocess.run([str(test), str(fixtures), str(results)], env=environment,
                                    stdout=output, stderr=subprocess.STDOUT, timeout=240)
        except subprocess.TimeoutExpired:
            print(log.read_text(errors="replace"), flush=True)
            raise
    transcript = log.read_text(errors="replace")
    print(transcript, flush=True)
    if result.returncode:
        raise RuntimeError(f"Native smoke failed: exit {result.returncode}; diagnostics: {log}")


def collect_mac_crashes(results, started):
    # Darwin can abort before stderr is initialized. Keep the OS crash report.
    destinations = results / "mac-crashes"
    for directory in (Path.home() / "Library/Logs/DiagnosticReports",
                      Path("/Library/Logs/DiagnosticReports")):
        if not directory.is_dir():
            continue
        for report in directory.glob("pdf-render-worker*"):
            try:
                if report.is_file() and report.stat().st_mtime >= started and report.stat().st_size < 5 * 1024 * 1024:
                    destinations.mkdir(exist_ok=True)
                    shutil.copy2(report, destinations / report.name)
            except OSError:
                pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("windows", "macos"), required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--qt-prefix", type=Path, required=True)
    parser.add_argument("--qt-notices", type=Path, required=True)
    parser.add_argument("--smoke", type=Path, required=True)
    parser.add_argument("--fixtures", type=Path, default=ROOT / "tests/pdfs")
    parser.add_argument("--output", type=Path, default=ROOT / "dist/native")
    parser.add_argument("--version", default="0.1.0-rc.1")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", args.version):
        parser.error("invalid release version")
    expected = "Windows" if args.target == "windows" else "Darwin"
    if platform.system() != expected:
        parser.error(f"{args.target} packages require a native {expected} runner")
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    args.qt_prefix = args.qt_prefix.resolve()
    smoke_results = args.output / "validation"
    smoke_results.mkdir(exist_ok=True)
    architecture = "arm64" if platform.machine().lower() in ("arm64", "aarch64") else "x64"
    name = f"pdf-editor-{args.version}-{args.target}-{architecture}"
    with tempfile.TemporaryDirectory(prefix="pdf-editor-native-desktop-") as temp:
        root = Path(temp) / "folder with spaces" / name
        run(["cmake", "--install", args.build_dir, "--prefix", root])
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software")
        if args.target == "windows":
            # Qt's offscreen FreeType backend does not enumerate Windows fonts
            # through the desktop backend. Provide its CI-only font directory.
            fonts = Path(os.environ["SystemRoot"]) / "Fonts"
            if not any(fonts.glob("*.ttf")):
                raise RuntimeError(f"Windows offscreen validation has no fonts: {fonts}")
            env["QT_QPA_FONTDIR"] = str(fonts)
        for key in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH",
                    "QML2_IMPORT_PATH", "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH"):
            env.pop(key, None)
        if args.target == "windows":
            binary = root / "pdf-editor.exe"
            worker = root / "pdf-render-worker.exe"
            deploy = args.qt_prefix / "bin/windeployqt.exe"
            for file in (binary, worker, root / "pdf-sandbox.exe", root / "pdfium.dll"):
                if not file.is_file():
                    raise RuntimeError(f"Missing real native executable/dependency: {file}")
            for executable in (binary, worker):
                run([deploy, "--release", "--no-compiler-runtime", "--qmldir", ROOT / "qml",
                     "--dir", root, executable])
            # Qt otherwise deploys a VC redistributable installer. A portable
            # ZIP needs the redistributable CRT DLLs beside its executables.
            redist = os.environ.get("VCToolsRedistDir")
            candidates = sorted((Path(redist) / "x64").glob("Microsoft.VC*.CRT")) if redist else []
            if len(candidates) != 1:
                raise RuntimeError("Cannot locate the app-local MSVC x64 CRT; run in the MSVC developer environment")
            for dependency in candidates[0].glob("*.dll"):
                shutil.copy2(dependency, root / dependency.name)
            for runtime_name in ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
                if not (root / runtime_name).is_file():
                    raise RuntimeError(f"Missing app-local compiler runtime: {runtime_name}")
            platforms = root / "platforms"
            platforms.mkdir(exist_ok=True)
            shutil.copy2(args.qt_prefix / "plugins/platforms/qoffscreen.dll", platforms / "qoffscreen.dll")
            test = root / "native-smoke.exe"
            shutil.copy2(args.smoke, test)
            env["PATH"] = os.path.join(os.environ["SystemRoot"], "System32")
            env["QT_PLUGIN_PATH"] = str(root)
            notices = root / "share/doc/pdf-form-editor/qt"
        else:
            source = root / "pdf-form-editor.app"
            bundle = root / "PDF Editor.app"
            source.rename(bundle)
            contents = bundle / "Contents"
            binary = contents / "MacOS/pdf-form-editor"
            worker = contents / "MacOS/pdf-render-worker"
            library = contents / "Frameworks/libpdfium.dylib"
            run(["install_name_tool", "-id", "@rpath/libpdfium.dylib", library])
            # GN may use an absolute build identity; normalize every reference
            # to PDFium before Qt's deploy tool scans the nested worker.
            linkage = run(["otool", "-L", worker], capture_output=True, text=True).stdout
            for line in linkage.splitlines()[1:]:
                dependency = line.strip().split(" (", 1)[0]
                if dependency.endswith("libpdfium.dylib") and dependency != "@rpath/libpdfium.dylib":
                    run(["install_name_tool", "-change", dependency, "@rpath/libpdfium.dylib", worker])
            test = contents / "MacOS/native-smoke"
            shutil.copy2(args.smoke, test)
            # Deploy only editor plugins. Copying every SQL driver drags in
            # optional database client libraries absent from hosted runners.
            plugins = []
            for category in ("platforms", "imageformats", "iconengines", "styles"):
                destination = contents / "PlugIns" / category
                destination.mkdir(parents=True, exist_ok=True)
                for plugin in sorted((args.qt_prefix / "plugins" / category).glob("*.dylib")):
                    deployed = destination / plugin.name
                    shutil.copy2(plugin, deployed)
                    plugins.append(deployed)
            for required in ("libqcocoa.dylib", "libqoffscreen.dylib"):
                if not (contents / "PlugIns/platforms" / required).is_file():
                    raise RuntimeError(f"Required native Qt platform plugin is missing: {required}")
            # Scan each selected plugin as well as the GUI/worker/helper so its
            # own framework dependencies are deployed and rewritten too.
            run([args.qt_prefix / "bin/macdeployqt", bundle, f"-qmldir={ROOT / 'qml'}",
                 f"-executable={worker}", f"-executable={test}",
                 *[f"-executable={plugin}" for plugin in plugins],
                 "-no-plugins", "-always-overwrite"])
            for file in (binary, worker, test, *plugins):
                rpaths = run(["otool", "-l", file], capture_output=True, text=True).stdout
                for path in re.findall(r"\bpath (.+) \(offset \d+\)", rpaths):
                    if str(args.qt_prefix) in path:
                        run(["install_name_tool", "-delete_rpath", path, file])
                linkage = run(["otool", "-L", file], capture_output=True, text=True).stdout
                if str(args.qt_prefix) in linkage:
                    raise RuntimeError(f"Undeployed Qt dependency in {file}:\n{linkage}")
                if "@executable_path/../Frameworks" not in rpaths:
                    run(["install_name_tool", "-add_rpath", "@executable_path/../Frameworks", file])
            run(["codesign", "--force", "--sign", "-", "--entitlements",
                 ROOT / "packaging/native/macos-worker.entitlements", worker])
            run(["codesign", "--force", "--deep", "--preserve-metadata=entitlements", "--sign", "-",
                 "--entitlements", ROOT / "packaging/native/macos-worker.entitlements", bundle])
            env["QT_PLUGIN_PATH"] = str(contents / "PlugIns")
            notices = contents / "Resources/notices/qt"
            # Keep the distributable Mac ZIP centered on its actual app bundle.
            for source in (root / "share", root / "LICENSE", root / "README.md"):
                if source.exists():
                    destination = contents / "Resources" / source.name
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.move(str(source), destination)
        if not args.qt_notices.is_dir():
            raise RuntimeError("Qt dependency notices are missing")
        shutil.copytree(args.qt_notices, notices, dirs_exist_ok=True)
        shutil.copy2(ROOT / "LICENSE", root / "LICENSE")
        shutil.copy2(ROOT / "docs/NATIVE-RELEASES.md", root / "README.md")
        env["PDF_EDITOR_WORKER_DIAGNOSTICS"] = "1"
        if args.target == "windows":
            env["PDF_EDITOR_SANDBOX_LOG"] = str(smoke_results / "windows-broker.log")
        started = time.time()
        try:
            verify_worker_loader(worker, env, smoke_results)
            verify_gui(binary, env)
            run_smoke(test, args.fixtures.resolve(), smoke_results, env)
        except Exception:
            if args.target == "macos":
                for _ in range(5):
                    collect_mac_crashes(smoke_results, started)
                    time.sleep(1)
            raise
        test.unlink()
        if args.target == "macos":
            run(["codesign", "--force", "--deep", "--preserve-metadata=entitlements", "--sign", "-",
                 "--entitlements", ROOT / "packaging/native/macos-worker.entitlements", bundle])
            run(["codesign", "--verify", "--deep", "--strict", bundle])
        revision = run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
        manifest = {"version": args.version, "platform": args.target, "architecture": architecture,
                    "application_revision": revision, "runtime_smoke_passed": True,
                    "signing": "unsigned" if args.target == "windows" else "ad-hoc; not notarized"}
        (root / "release-info.json").write_text(json.dumps(manifest, indent=2) + "\n")
        archive = args.output / f"{name}.zip"
        if args.target == "macos":
            # ditto preserves bundle symlinks and executable permissions.
            run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", root, archive])
        else:
            with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
                for file in sorted(root.rglob("*")):
                    if file.is_file():
                        output.write(file, Path(name) / file.relative_to(root))
        (args.output / "SHA256SUMS").write_text(f"{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}\n")
    print(f"Validated {args.target} native artifact: {archive}")


if __name__ == "__main__":
    main()

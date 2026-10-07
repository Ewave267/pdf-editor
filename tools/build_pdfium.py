#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the pinned PDFium XFA/V8 library with the project's persistence fix.

All checkouts, toolchains, logs and outputs stay under .deps. No system install
or package-manager commands are performed. Run from a Linux x64 build host.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / ".deps"
PDFIUM_REVISION = "2fd6cff57d9412cc42ef1a7e4e0a59b13a1e7cec"
DEPOT_REVISION = "f7ea32ec994dbfd43c61edfcf276af6f98662878"
DISTRIBUTOR_REVISION = "5325fa6d0d9379329f10f98fdc4839b4e40f2e79"
VERSION = "157.0.8086.0"
PATCH = ROOT / "third_party/pdfium/patches/0001-xfa-persistence.patch"


def run(command, cwd, environment, label):
    logs = DEPS / "logs"
    logs.mkdir(parents=True, exist_ok=True)
    log = logs / f"{label}.log"
    print(f"{label}: {log}", flush=True)
    with log.open("w") as output:
        result = subprocess.run(command, cwd=cwd, env=environment, stdout=output,
                                stderr=subprocess.STDOUT, check=False)
    if result.returncode:
        tail = "\n".join(log.read_text(errors="replace").splitlines()[-30:])
        raise RuntimeError(f"{label} failed ({result.returncode}):\n{tail}")


def checkout(url, revision, directory, environment):
    if not (directory / ".git").exists():
        run(["git", "clone", "--depth", "1", url, str(directory)], ROOT, environment,
            f"clone-{directory.name}")
    actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=directory, text=True).strip()
    if actual != revision:
        run(["git", "fetch", "--depth", "1", "origin", revision], directory, environment,
            f"fetch-{directory.name}")
        run(["git", "checkout", "--detach", revision], directory, environment,
            f"checkout-{directory.name}")


def apply_patch(patch, directory, environment):
    check = subprocess.run(["git", "apply", "--check", str(patch)], cwd=directory,
                           capture_output=True)
    if check.returncode == 0:
        run(["git", "apply", str(patch)], directory, environment, f"patch-{patch.stem}")
    elif subprocess.run(["git", "apply", "--reverse", "--check", str(patch)],
                        cwd=directory, capture_output=True).returncode != 0:
        raise RuntimeError(f"patch does not match the pinned source: {patch}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--skip-sync", action="store_true", help="reuse an already completed pinned source sync")
    args = parser.parse_args()
    if sys.platform != "linux" or platform.machine() != "x86_64":
        raise RuntimeError("this source-build recipe currently supports Linux x64 only")
    if not 1 <= args.jobs <= 8:
        raise RuntimeError("jobs must be between 1 and 8")
    for program in ("git", "ninja", "cmake"):
        if shutil.which(program) is None:
            raise RuntimeError(f"required build tool is missing: {program}")
    DEPS.mkdir(exist_ok=True)
    workspace = DEPS / "pdfium-source"
    workspace.mkdir(exist_ok=True)
    source = workspace / "pdfium"
    depot = DEPS / "depot_tools"
    distributor = DEPS / "pdfium-build"
    environment = dict(os.environ,
        DEPOT_TOOLS_UPDATE="0", VPYTHON_VIRTUALENV_ROOT=str(workspace / ".venv"),
        CIPD_CACHE_DIR=str(workspace / ".cipd-cache"))
    # Avoid downloading full repository-history mirrors for a shallow build.
    environment.pop("GIT_CACHE_PATH", None)
    checkout("https://chromium.googlesource.com/chromium/tools/depot_tools.git",
             DEPOT_REVISION, depot, environment)
    checkout("https://github.com/bblanchon/pdfium-binaries.git",
             DISTRIBUTOR_REVISION, distributor, environment)
    environment["PATH"] = str(depot) + os.pathsep + environment["PATH"]
    if not args.skip_sync:
        (workspace / ".gclient").write_text(
            "solutions = [{'name': 'pdfium', 'url': 'https://pdfium.googlesource.com/pdfium.git', "
            "'managed': False, 'custom_vars': {'checkout_configuration': 'small'}}]\n")
        run([str(depot / "gclient"), "sync", "-r", PDFIUM_REVISION,
             "--no-history", "--shallow", "--nohooks", "--jobs", str(args.jobs)],
            workspace, environment, "sync")
    actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    if actual != PDFIUM_REVISION or not (workspace / ".gclient_entries").exists():
        raise RuntimeError("source sync is incomplete or not at the pinned revision")
    run([str(depot / "gclient"), "runhooks"], workspace, environment, "hooks")
    for name, directory in [
        ("shared_library.patch", source), ("public_headers.patch", source),
        ("v8/pdfium.patch", source),
    ]:
        apply_patch(distributor / "patches" / name, directory, environment)
    apply_patch(ROOT / "third_party/pdfium/patches/0002-v8-shared-library-tls.patch",
                source / "v8", environment)
    apply_patch(PATCH, source, environment)
    apply_patch(ROOT / "third_party/pdfium/patches/0003-xfa-radio-and-font-fallback.patch",
                source, environment)
    build = source / "out/persistence"
    build.mkdir(parents=True, exist_ok=True)
    (build / "args.gn").write_text("""is_debug = false
is_component_build = false
pdf_is_standalone = true
pdf_use_partition_alloc = false
pdf_enable_xfa = true
pdf_enable_v8 = true
pdf_enable_fontations = false
pdf_use_skia = false
v8_enable_i18n_support = false
v8_use_external_startup_data = false
clang_use_chrome_plugins = false
treat_warnings_as_errors = false
use_remoteexec = false
use_sysroot = false
use_glib = false
target_cpu = "x64"
target_os = "linux"
""")
    run([str(source / "buildtools/linux64/gn"), "gen", str(build)], source, environment, "configure")
    run([shutil.which("ninja"), "-C", str(build), "-j", str(args.jobs), "pdfium", "pdfium_unittests"],
        source, environment, "compile")
    run([str(build / "pdfium_unittests"), "--gtest_filter=CFXXML*"],
        source, environment, "xml-tests")
    package = DEPS / "pdfium-patched"
    (package / "lib").mkdir(parents=True, exist_ok=True)
    shutil.copyfile(build / "libpdfium.so", package / "lib/libpdfium.so")
    shutil.copytree(source / "public", package / "include", dirs_exist_ok=True)
    shutil.copyfile(build / "args.gn", package / "args.gn")
    shutil.copyfile(source / "LICENSE", package / "LICENSE")
    shutil.copyfile(ROOT / "LICENSE", package / "PROJECT-LICENSE")
    shutil.copyfile(distributor / "LICENSE", package / "DISTRIBUTOR-LICENSE")
    (package / "NOTICE").write_text(
        "PDFium source revision: " + PDFIUM_REVISION + "\n"
        "Modified by PDF Form Editor contributors for XFA persistence, radio editing and Linux font fallback.\n"
        "Project modifications are distributed under GPL-3.0-only; see PROJECT-LICENSE.\n"
        "Original PDFium and third-party notices: LICENSE and licenses/.\n"
        "Shared-library/V8 initialization recipe: DISTRIBUTOR-LICENSE.\n")
    (package / "PDFiumConfig.cmake").write_text(f'''set(PDFium_VERSION "{VERSION}")
set(PDF_EDITOR_PDFIUM_PATCHSET "2")
add_library(pdfium SHARED IMPORTED)
set_target_properties(pdfium PROPERTIES
  IMPORTED_LOCATION "${{CMAKE_CURRENT_LIST_DIR}}/lib/libpdfium.so"
  INTERFACE_INCLUDE_DIRECTORIES "${{CMAKE_CURRENT_LIST_DIR}}/include")
''')
    (package / "build-info.json").write_text(json.dumps({
        "version": VERSION, "pdfium_revision": PDFIUM_REVISION,
        "v8_revision": subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=source / "v8", text=True).strip(),
        "depot_tools_revision": DEPOT_REVISION, "distributor_revision": DISTRIBUTOR_REVISION,
        "patch_sha256": hashlib.sha256(PATCH.read_bytes()).hexdigest(),
        "radio_font_patch_sha256": hashlib.sha256((ROOT / "third_party/pdfium/patches/0003-xfa-radio-and-font-fallback.patch").read_bytes()).hexdigest(),
        "v8_tls_patch_sha256": hashlib.sha256((ROOT / "third_party/pdfium/patches/0002-v8-shared-library-tls.patch").read_bytes()).hexdigest(),
        "library_sha256": hashlib.sha256((package / "lib/libpdfium.so").read_bytes()).hexdigest(),
    }, indent=2) + "\n")
    # Generate notices from the actual build graph using the pinned distributor's recipe.
    license_environment = dict(environment, PDFium_SOURCE_DIR=str(source),
                               PDFium_BUILD_DIR=str(build), PDFium_ENABLE_V8="true")
    run(["bash", str(distributor / "steps/08-licenses.sh")], distributor,
        license_environment, "licenses")
    shutil.copytree(distributor / "staging/licenses", package / "licenses", dirs_exist_ok=True)
    for library in ("libc++", "libc++abi"):
        shutil.copyfile(source / "third_party" / library / "src/LICENSE.TXT",
                        package / "licenses" / f"{library}.txt")
    print(f"Built and XML-tested PDFium persistence/compatibility patchset 2: {package}", flush=True)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)

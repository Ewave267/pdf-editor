#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the PDFium worker in a fail-closed Linux Bubblewrap sandbox."""
import argparse
import os
from pathlib import Path
import resource
import shutil
import signal
import subprocess
import sys
import tempfile


def sandbox_command(worker: Path, library: Path, input_pdf: Path, output: Path) -> list[str]:
    bwrap = shutil.which("bwrap")
    if sys.platform != "linux" or bwrap is None:
        raise RuntimeError("Linux and Bubblewrap are required; no unsandboxed fallback is available")
    command = [
        bwrap, "--unshare-all", "--die-with-parent", "--new-session", "--cap-drop", "ALL",
        "--clearenv", "--setenv", "LD_LIBRARY_PATH", "/pdfium", "--setenv", "HOME", "/tmp",
        "--setenv", "LANG", "C.UTF-8", "--ro-bind", "/usr", "/usr",
        "--symlink", "usr/lib", "/lib", "--symlink", "usr/lib64", "/lib64",
        "--dir", "/proc", "--dir", "/dev", "--size", "67108864", "--tmpfs", "/tmp",
    ]
    for device in ("/dev/null", "/dev/zero", "/dev/urandom", "/dev/random"):
        command.extend(["--ro-bind", device, device])
    # Fonts and their configuration are runtime inputs, never writable host paths.
    for path in ("/etc/fonts", "/var/cache/fontconfig"):
        if Path(path).exists():
            command.extend(["--ro-bind", path, path])
    command.extend([
        "--ro-bind", str(worker), "/probe", "--ro-bind", str(library), "/pdfium/libpdfium.so",
        "--ro-bind", str(input_pdf), "/input.pdf", "--bind", str(output), "/output",
        "--chdir", "/output",
    ])
    return command


def resource_limits() -> None:
    resource.setrlimit(resource.RLIMIT_CPU, (15, 15))
    resource.setrlimit(resource.RLIMIT_FSIZE, (64 * 1024 * 1024, 64 * 1024 * 1024))
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_NOFILE, (128, 128))


def run_isolated(command: list[str], timeout: int) -> subprocess.CompletedProcess:
    process = subprocess.Popen(
        command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        start_new_session=True, preexec_fn=resource_limits,
    )
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.communicate()
        raise RuntimeError(f"isolated probe exceeded {timeout} seconds") from None
    return subprocess.CompletedProcess(command, process.returncode, stdout, stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", required=True, type=Path)
    parser.add_argument("--pdfium", required=True, type=Path, help="PDFium package directory")
    parser.add_argument("--input", required=True, type=Path, help="one-page calculation fixture")
    parser.add_argument("--output", required=True, type=Path, help="new directory for verified results")
    parser.add_argument("--value", default="saved-value")
    parser.add_argument("--timeout", type=int, default=30)
    args = parser.parse_args()
    try:
        if not 1 <= len(args.value) <= 64 or any(not 32 <= ord(c) < 127 for c in args.value):
            raise RuntimeError("value must contain 1–64 printable ASCII characters")
        if not 1 <= args.timeout <= 60:
            raise RuntimeError("timeout must be between 1 and 60 seconds")
        worker = args.worker.resolve(strict=True)
        library = (args.pdfium / "lib/libpdfium.so").resolve(strict=True)
        input_pdf = args.input.resolve(strict=True)
        output = args.output.absolute()
        if output.exists() or output.is_symlink():
            raise RuntimeError("output directory already exists; original files are never overwritten")
        if not worker.is_file() or not library.is_file() or not input_pdf.is_file():
            raise RuntimeError("worker, library, and input must be regular files")
        output.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="xfa-probe-") as temporary:
            staging = Path(temporary)
            command = sandbox_command(worker, library, input_pdf, staging)
            command.extend(["/probe", "/input.pdf", "/output", args.value])
            result = run_isolated(command, args.timeout)
            print(result.stdout, end="")
            print(result.stderr, end="", file=sys.stderr)
            if result.returncode != 0:
                raise RuntimeError(f"sandbox/worker failed with exit code {result.returncode}")
            artifacts = ("saved.pdf", "resaved.pdf", "before.ppm", "edited.ppm", "reopened.ppm")
            for name in artifacts:
                path = staging / name
                if path.is_symlink() or not path.is_file() or path.stat().st_size == 0:
                    raise RuntimeError(f"worker did not produce a regular, nonempty {name}")
            output.mkdir()  # Exclusive creation, even if another process races us.
            for name in artifacts:
                shutil.copyfile(staging / name, output / name)
            print(f"Verified artifacts: {output}")
        return 0
    except (OSError, RuntimeError) as error:
        print(f"XFA probe: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

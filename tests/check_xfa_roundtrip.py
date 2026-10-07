#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Strict milestone gate: succeeds only when exact values survive save/reopen."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix="xfa-roundtrip-") as temporary:
    for fixture in ("calculation.pdf", "single-stream.pdf"):
        output = Path(temporary) / fixture.removesuffix(".pdf")
        result = subprocess.run([
            sys.executable, str(ROOT / "tools/run_xfa_probe.py"),
            "--worker", sys.argv[1], "--pdfium", sys.argv[2],
            "--input", str(ROOT / "tests/pdfs/xfa-javascript" / fixture),
            "--output", str(output),
        ], check=False)
        if result.returncode:
            sys.exit(result.returncode)
        for filename, value in (("saved.pdf", "saved-value"), ("resaved.pdf", "saved-value-again")):
            result = subprocess.run([
                sys.argv[3], str(ROOT / "tools/check_xfa_with_pdfjs.mjs"),
                sys.argv[4], str(output / filename), value,
            ], check=False)
            if result.returncode:
                sys.exit(result.returncode)

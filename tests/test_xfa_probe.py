#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Persistence, event, isolation, invalid-input, and fixture regression tests."""
import hashlib
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from run_xfa_probe import run_isolated, sandbox_command

WORKER = Path(sys.argv.pop(1)).resolve()
PDFIUM = Path(sys.argv.pop(1)).resolve()
FIXTURE = ROOT / "tests/pdfs/xfa-javascript/calculation.pdf"


class XfaProbeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="xfa-regression-")
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.output = self.directory / "verified"

    def probe(self, fixture=FIXTURE, environment=None, value="saved-value"):
        return subprocess.run([
            sys.executable, str(ROOT / "tools/run_xfa_probe.py"),
            "--worker", str(WORKER), "--pdfium", str(PDFIUM),
            "--input", str(fixture), "--output", str(self.output), "--value", value,
        ], capture_output=True, text=True, timeout=40, env=environment)

    def test_packet_array_preserves_exact_values(self):
        before = hashlib.sha256(FIXTURE.read_bytes()).digest()
        result = self.probe()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("initial_javascript=verified", result.stdout)
        self.assertIn("edited_input_and_exit_javascript=verified", result.stdout)
        self.assertIn("save_as_copy=written", result.stdout)
        self.assertIn("denied_host_requests=1", result.stdout)
        self.assertIn("repeated_save_and_reopen=verified", result.stdout)
        for name in ["saved.pdf", "resaved.pdf", "before.ppm", "edited.ppm", "reopened.ppm"]:
            self.assertGreater((self.output / name).stat().st_size, 0)
        self.assertEqual(before, hashlib.sha256(FIXTURE.read_bytes()).digest())

    def test_single_stream_preserves_exact_values(self):
        result = self.probe(FIXTURE.with_name("single-stream.pdf"))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("save_as_copy=written", result.stdout)
        self.assertIn("repeated_save_and_reopen=verified", result.stdout)
        self.assertTrue((self.output / "saved.pdf").is_file())

    def test_xml_sensitive_text_and_whitespace_survive(self):
        for fixture in [FIXTURE, FIXTURE.with_name("single-stream.pdf")]:
            with self.subTest(fixture=fixture.name):
                self.output = self.directory / fixture.stem
                result = self.probe(fixture, value="  A<&>\"' B  ")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn("repeated_save_and_reopen=verified", result.stdout)

    def test_javascript_result_is_actually_checked(self):
        changed = self.directory / "changed-script.pdf"
        # Same byte count preserves the stream lengths and cross-reference offsets.
        changed.write_bytes(FIXTURE.read_bytes().replace(b'"JS:"', b'"NO:"'))
        result = self.probe(changed)
        self.assertEqual(result.returncode, 1)
        self.assertIn("expected 'JS:original', got 'NO:original'", result.stderr)
        self.assertNotIn("save_as_copy=written", result.stdout)

    def test_non_xfa_is_rejected(self):
        result = self.probe(ROOT / "tests/pdfs/normal/blank.pdf")
        self.assertEqual(result.returncode, 1)
        self.assertIn("expected an XFA document, got form type 0", result.stderr)

    def test_malformed_pdf_is_rejected(self):
        result = self.probe(ROOT / "tests/pdfs/malformed/not-a-pdf.pdf")
        self.assertEqual(result.returncode, 1)
        self.assertIn("PDF open failed", result.stderr)

    def test_existing_output_is_preserved(self):
        self.output.mkdir()
        sentinel = self.output / "saved.pdf"
        sentinel.write_bytes(b"keep existing document")
        result = self.probe()
        self.assertEqual(result.returncode, 1)
        self.assertIn("output directory already exists", result.stderr)
        self.assertEqual(sentinel.read_bytes(), b"keep existing document")

    def test_missing_sandbox_fails_closed(self):
        environment = dict(os.environ, PATH="")
        result = self.probe(environment=environment)
        self.assertEqual(result.returncode, 1)
        self.assertIn("no unsandboxed fallback", result.stderr)
        self.assertNotIn("form_type=", result.stdout)

    def test_sandbox_denies_host_access(self):
        secret = self.directory / "host-secret"
        secret.write_text("private host data")
        with socket.socket() as server:
            server.bind(("127.0.0.1", 0))
            server.listen(1)
            port = server.getsockname()[1]
            script = f"""
import os, socket
from pathlib import Path
assert not Path({str(secret)!r}).exists(), 'host filesystem leaked'
assert not Path('/etc/passwd').exists(), 'host configuration leaked'
assert 'PDF_EDITOR_TEST_SECRET' not in os.environ, 'host environment leaked'
try:
    open('/input.pdf', 'wb')
except OSError:
    pass
else:
    raise AssertionError('input is writable')
try:
    socket.create_connection(('127.0.0.1', {port}), timeout=1)
except OSError:
    pass
else:
    raise AssertionError('host network is accessible')
Path('/output/allowed').write_text('sandbox output')
print('sandbox policy verified')
"""
            staging = self.directory / "staging"
            staging.mkdir()
            command = sandbox_command(WORKER, PDFIUM / "lib/libpdfium.so", FIXTURE, staging)
            old = os.environ.get("PDF_EDITOR_TEST_SECRET")
            os.environ["PDF_EDITOR_TEST_SECRET"] = "must-not-leak"
            try:
                result = run_isolated(command + ["/usr/bin/python3", "-c", script], 5)
            finally:
                if old is None:
                    del os.environ["PDF_EDITOR_TEST_SECRET"]
                else:
                    os.environ["PDF_EDITOR_TEST_SECRET"] = old
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("sandbox policy verified", result.stdout)
            self.assertEqual((staging / "allowed").read_text(), "sandbox output")

    def test_worker_deadline_is_enforced(self):
        command = sandbox_command(WORKER, PDFIUM / "lib/libpdfium.so", FIXTURE, self.directory)
        with self.assertRaisesRegex(RuntimeError, "exceeded 1 seconds"):
            run_isolated(command + ["/usr/bin/python3", "-c", "import time; time.sleep(10)"], 1)

    def test_fixtures_are_reproducible(self):
        from generate_probe_fixtures import generate
        paths = [FIXTURE, FIXTURE.with_name("single-stream.pdf"),
                 ROOT / "tests/pdfs/normal/blank.pdf", ROOT / "tests/pdfs/normal/single-page.pdf",
                 ROOT / "tests/pdfs/normal/multi-page.pdf", ROOT / "tests/pdfs/normal/rotated-cropped.pdf",
                 ROOT / "tests/pdfs/acroform/text.pdf", ROOT / "tests/pdfs/acroform/controls.pdf",
                 ROOT / "tests/pdfs/xfa-dynamic/controls.pdf", ROOT / "tests/pdfs/malformed/not-a-pdf.pdf"]
        before = [path.read_bytes() for path in paths]
        generated_root = self.directory / "generated"
        generate(generated_root)
        self.assertEqual(before, [(generated_root / path.relative_to(ROOT)).read_bytes() for path in paths])


if __name__ == "__main__":
    unittest.main()

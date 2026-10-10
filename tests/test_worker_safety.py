#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Native kernel policy, hostile scripts, protocol limits and corpus regression."""
import hashlib
import base64
import json
import os
import resource
import random
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from generate_probe_fixtures import generate, many_pages_pdf, safety_fixtures
from run_xfa_probe import sandbox_command

WORKER = Path(sys.argv.pop(1)).resolve()
POLICY_WORKER = Path(sys.argv.pop(1)).resolve()
PDFIUM = Path(sys.argv.pop(1)).resolve()


class WorkerSafetyTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="pdf-safety-")
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)

    def command(self, worker, fixture):
        command = sandbox_command(worker, PDFIUM / "lib/libpdfium.so", fixture, self.directory)
        index = command.index("--bind")  # Renderer has no writable host output mount.
        del command[index:index + 3]
        command[command.index("--chdir") + 1] = "/tmp"
        return command + ["/probe"]

    def run_worker(self, fixture, commands=(), environment=None):
        result = subprocess.run(self.command(WORKER, fixture),
                                input="".join(json.dumps(command) + "\n" for command in commands),
                                text=True, capture_output=True, timeout=10, env=environment)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        return [json.loads(line) for line in result.stdout.splitlines()]

    def test_merge_rejects_unprepared_form_appearances(self):
        fixture = ROOT / "tests/pdfs/normal/single-page.pdf"
        source = (ROOT / "tests/pdfs/acroform/phase3.pdf").read_bytes()
        options = {"pages": [0], "rotation": 0, "flatten": True, "mergeLengths": [len(source)]}
        payload = b"PDFEDITOR-EXPORT\n" + json.dumps(options).encode() + b"\n" + source
        result = subprocess.run(self.command(WORKER, fixture) + ["--save"], input=payload,
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"Flatten AcroForm", result.stderr)

    def test_high_resolution_transport_is_compressed(self):
        responses = self.run_worker(ROOT / "tests/pdfs/normal/single-page.pdf",
                                    [{"id": 1, "page": 0, "width": 2400}])
        self.assertLess(len(base64.b64decode(responses[1]["png"])), 1024 * 1024)

    def test_upstream_compatibility_corpus(self):
        for name, expected in (("arabic.pdf", 0), ("simple_xfa.pdf", 2),
                               ("static_password_field_rotate.pdf", 3)):
            fixture = ROOT / "tests/pdfs/upstream-pdfium" / name
            responses = self.run_worker(fixture, [{"id": 1, "page": 0, "width": 300}])
            self.assertEqual(responses[0]["formType"], expected, name)
            self.assertIn("png", responses[1], name)
        fixture = ROOT / "tests/pdfs/upstream-pdfium/encrypted_hello_world_r6.pdf"
        result = subprocess.run(self.command(WORKER, fixture), input=b"", capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"password", result.stderr)

    def test_deterministic_mutation_corpus(self):
        """Small reproducible malformed-input campaign under the real sandbox."""
        randomizer = random.Random(20261010)
        original = (ROOT / "tests/pdfs/acroform/controls.pdf").read_bytes()
        for iteration in range(32):
            data = bytearray(original)
            if iteration % 3 == 0:
                del data[randomizer.randrange(5, len(data)):]
            else:
                for _ in range(1 + iteration % 12):
                    data[randomizer.randrange(5, len(data))] = randomizer.randrange(256)
            fixture = self.directory / f"mutation-{iteration}.pdf"
            fixture.write_bytes(data)
            result = subprocess.run(self.command(WORKER, fixture),
                                    input=b'{"id":1,"page":0,"width":96}\n',
                                    capture_output=True, timeout=10)
            self.assertIn(result.returncode, (0, 1, 2),
                          f"mutation {iteration}: {result.returncode} {result.stderr!r}")
            self.assertNotIn(b"AddressSanitizer", result.stderr)
            self.assertLess(len(result.stdout), 4 * 1024 * 1024)

    def test_export_rejects_unbounded_and_invalid_options(self):
        fixture = ROOT / "tests/pdfs/normal/single-page.pdf"
        for options in ({"pages": [5000], "rotation": 0},
                        {"pages": [0], "rotation": 8},
                        {"pages": [0] * 2001, "rotation": 0},
                        {"pages": [0], "rotation": 0, "mergeLengths": [1000]}):
            payload = b"PDFEDITOR-EXPORT\n" + json.dumps(options).encode() + b"\n"
            result = subprocess.run(self.command(WORKER, fixture) + ["--save"],
                                    input=payload, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertFalse(result.stdout.startswith(b"%PDF-"))

    def test_warm_worker_loads_bounded_pipe_input(self):
        fixture = ROOT / "tests/pdfs/normal/single-page.pdf"
        command = self.command(WORKER, fixture) + ["--warm"]
        with subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE) as process:
            self.assertEqual(json.loads(process.stdout.readline()), {"warm": True})
            self.assertIsNone(process.poll())
            # Exercise binary framing across multiple 64KiB transport chunks.
            pdf = fixture.read_bytes() + b" " * 131072
            payload = json.dumps({"op": "load", "bytes": len(pdf)}).encode() + b"\n" + pdf
            payload += b'{"id":1,"page":0,"width":612}\n'
            output, diagnostics = process.communicate(payload, timeout=10)
            self.assertEqual(process.returncode, 0, diagnostics.decode(errors="replace"))
            replies = [json.loads(line) for line in output.splitlines()]
            self.assertEqual(len(replies[0]["pages"]), 1)
            self.assertIn("png", replies[1])

    def test_warm_worker_rejects_invalid_and_truncated_input(self):
        fixture = ROOT / "tests/pdfs/normal/single-page.pdf"
        for payload in (b'{"op":"load","bytes":67108865}\n',
                        b'{"op":"load","bytes":10}\nshort', b'{"op":"load"'):
            with self.subTest(payload=payload):
                result = subprocess.run(self.command(WORKER, fixture) + ["--warm"],
                                        input=payload, capture_output=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)

    def test_kernel_denies_access_even_without_javascript_api_checks(self):
        secret = self.directory / "host-secret"
        secret.write_text("host-only sentinel")
        fixture = ROOT / "tests/pdfs/normal/blank.pdf"
        before = hashlib.sha256(fixture.read_bytes()).digest()
        environment = dict(os.environ, PDF_EDITOR_TEST_SECRET="must-not-leak")
        result = subprocess.run(self.command(POLICY_WORKER, fixture) + [str(secret)],
                                capture_output=True, text=True, timeout=10, env=environment)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("PASS: files, commands, sockets, environment, threads and memory policy", result.stdout)
        self.assertEqual(secret.read_text(), "host-only sentinel")
        self.assertEqual(hashlib.sha256(fixture.read_bytes()).digest(), before)

    def test_document_scripts_cannot_access_host_capabilities(self):
        secret = self.directory / "host-secret"
        secret.write_text("host-only sentinel")
        unwanted = self.directory / "host-write"
        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(0.1)
            port = listener.getsockname()[1]
            for kind in ("acroform", "xfa"):
                with self.subTest(kind=kind):
                    original = safety_fixtures()[f"host-access-{kind}.pdf"]
                    # Rebuild stream lengths and xref offsets after substituting targets.
                    from generate_probe_fixtures import acroform_controls, xfa_pdf, SAFETY_SCRIPT
                    script = SAFETY_SCRIPT.replace("/tmp/pdf-editor-host-secret", str(secret)).replace("/tmp/pdf-editor-host-write", str(unwanted)).replace("http://example.invalid/safety", f"http://127.0.0.1:{port}/safety")
                    if kind == "acroform":
                        contents = acroform_controls(script)
                    else:
                        # This fixture's XDP is a single uncompressed stream.
                        start = original.index(b"stream\n") + len(b"stream\n")
                        end = original.index(b"\nendstream", start)
                        xml = original[start:end].replace(b"/tmp/pdf-editor-host-secret", str(secret).encode()).replace(b"http://example.invalid/safety", f"http://127.0.0.1:{port}/safety".encode())
                        contents = xfa_pdf(xml)
                    fixture = self.directory / f"host-{kind}.pdf"
                    fixture.write_bytes(contents)
                    environment = dict(os.environ, PDF_EDITOR_TEST_SECRET="must-not-leak")
                    responses = self.run_worker(fixture, [{"id": 1, "op": "event", "page": 0,
                                                           "action": "click", "x": 100, "y": 87}], environment)
                    audit = responses[1]["text"].split("|")
                    self.assertIn("ran", audit, "adversarial script must actually execute")
                    for name in ("process", "require", "Deno", "fetch", "XMLHttpRequest", "getenv", "system"):
                        self.assertIn(f"{name}:undefined", audit)
                    self.assertIn("read:blocked", audit)
                    self.assertTrue(any(item.startswith("network:") for item in audit))
                    if kind == "xfa":
                        self.assertGreaterEqual(responses[0]["deniedHostRequests"], 1)
                    self.assertEqual(secret.read_text(), "host-only sentinel")
                    self.assertFalse(unwanted.exists())
                    self.assertEqual(fixture.read_bytes(), contents)
                    with self.assertRaises(socket.timeout):
                        listener.accept()

    def test_script_exceptions_and_finite_heavy_scripts(self):
        for kind in ("acroform", "xfa"):
            for name, expected in (("throw", "ran-before-error"), ("stress", "sum:4999950000"), ("memory", "memory:blocked")):
                with self.subTest(kind=kind, script=name):
                    fixture = ROOT / f"tests/pdfs/safety/{name}-{kind}.pdf"
                    responses = self.run_worker(fixture, [
                        {"id": 1, "op": "event", "page": 0, "action": "click", "x": 100, "y": 87},
                        {"id": 2, "page": 0, "width": 612}])
                    self.assertEqual(responses[1]["text"], expected)
                    self.assertIn("png", responses[2])

    def test_invalid_native_requests_do_not_kill_worker(self):
        fixture = ROOT / "tests/pdfs/normal/blank.pdf"
        responses = self.run_worker(fixture, [{"id": 1, "page": -1, "width": 612},
                                             {"id": 2, "op": "unexpected"},
                                             {"id": 3, "page": 0, "width": 96}])
        self.assertIn("error", responses[1])
        self.assertIn("error", responses[2])
        self.assertIn("png", responses[3])
        result = subprocess.run(self.command(WORKER, fixture), input="X" * (65 * 1024) + "\n",
                                capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)

    def test_cyclic_form_and_extreme_geometry(self):
        responses = self.run_worker(ROOT / "tests/pdfs/safety/cyclic-acroform.pdf",
                                    [{"id": 1, "page": 0, "width": 612}])
        self.assertIn("png", responses[1])
        responses = self.run_worker(ROOT / "tests/pdfs/safety/extreme-page.pdf",
                                    [{"id": 1, "page": 0, "width": 2400}])
        self.assertIn("too large", responses[1]["error"])
        result = subprocess.run(self.command(WORKER, ROOT / "tests/pdfs/safety/tiny-page.pdf"),
                                input="", capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 1)
        self.assertIn("unsupported page size", json.loads(result.stdout)["error"])

    def test_large_file_and_page_limit(self):
        fixture = self.directory / "large.pdf"
        fixture.write_bytes(many_pages_pdf(padding=20 * 1024 * 1024))
        responses = self.run_worker(fixture, [{"id": 1, "page": 999, "width": 612}])
        self.assertEqual(len(responses[0]["pages"]), 1000)
        self.assertIn("png", responses[1])
        # Linux wait4 includes reaped descendants, including the Bubblewrap worker.
        peak_kib = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
        self.assertLess(peak_kib, 384 * 1024)
        fixture.write_bytes(many_pages_pdf(count=2001))
        result = subprocess.run(self.command(WORKER, fixture), input="", text=True,
                                capture_output=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("1 to 2000", json.loads(result.stdout)["error"])

    def test_safety_corpus_is_reproducible(self):
        generate(self.directory)
        for name in safety_fixtures():
            self.assertEqual((ROOT / "tests/pdfs/safety" / name).read_bytes(),
                             (self.directory / "tests/pdfs/safety" / name).read_bytes())


if __name__ == "__main__":
    unittest.main()

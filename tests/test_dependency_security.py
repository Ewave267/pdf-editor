#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "security", Path(__file__).resolve().parents[1] / "tools/check_dependency_security.py")
security = importlib.util.module_from_spec(spec)
spec.loader.exec_module(security)


class DependencySecurityTests(unittest.TestCase):
    def run_check(self, findings):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "report.json"
            log = io.StringIO()
            with patch("sys.argv", ["check", "--output", str(output)]), \
                    patch.object(security, "query", side_effect=findings), \
                    contextlib.redirect_stdout(log):
                error = None
                try:
                    security.main()
                except SystemExit as exc:
                    error = exc
            return error, json.loads(output.read_text()), log.getvalue()

    def test_reviewed_freshness_is_visible_without_failing(self):
        finding = {"id": "V8-FRESHNESS", "summary": "Outdated dependency on V8"}
        error, report, log = self.run_check([[], [finding], [], [], []])
        self.assertIsNone(error)
        self.assertEqual(report["inputs"]["V8"]["findings"], [finding])
        self.assertEqual(report["inputs"]["V8"]["nonblocking_policy_notices"], ["V8-FRESHNESS"])
        self.assertIn("::warning::", log)

    def test_unreviewed_pin_still_fails(self):
        with patch.object(security, "REVIEWED_V8_FRESHNESS_PIN", "different-pin"):
            error, _, _ = self.run_check([[], [{"id": "V8-FRESHNESS"}], [], [], []])
        self.assertIsNotNone(error)

    def test_other_findings_still_fail_alongside_freshness(self):
        error, report, _ = self.run_check(
            [[], [{"id": "V8-FRESHNESS"}, {"id": "TEST-VULNERABILITY"}], [], [], []])
        self.assertIsNotNone(error)
        self.assertEqual(len(report["inputs"]["V8"]["findings"]), 2)

    def test_freshness_on_another_dependency_still_fails(self):
        error, _, _ = self.run_check([[{"id": "V8-FRESHNESS"}], [], [], [], []])
        self.assertIsNotNone(error)

    def test_query_failure_propagates(self):
        with patch("sys.argv", ["check"]), \
                patch.object(security, "query", side_effect=OSError("unavailable")):
            with self.assertRaises(OSError):
                security.main()


if __name__ == "__main__":
    unittest.main()

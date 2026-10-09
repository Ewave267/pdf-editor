# SPDX-License-Identifier: GPL-3.0-only
"""Platform-independent checks for the Windows worker dependency collector."""
import sys
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from windows_runtime import collect_worker_runtime, imported_dlls


class WorkerRuntimeTests(unittest.TestCase):
    def test_import_scan_includes_delay_dependencies(self):
        output = ("Image has the following dependencies:\r\n\r\n    Qt6Core.dll\r\n"
                  "Image has the following delay load dependencies:\r\n    pdfium.dll\r\n"
                  "  Summary\r\n    1000 .data\r\n")
        with patch("windows_runtime.subprocess.run", return_value=SimpleNamespace(stdout=output)):
            self.assertEqual(imported_dlls(Path("worker.exe")), ["Qt6Core.dll", "pdfium.dll"])

    def test_closure_excludes_ui_and_handles_cycles_and_system_imports(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, system = root / "app", root / "system"
            source.mkdir()
            system.mkdir()
            for name in ("worker.exe", "Qt6Core.dll", "pdfium.dll", "vcruntime140.dll", "Qt6Quick.dll"):
                (source / name).write_bytes(b"fixture")
            (system / "KERNEL32.dll").write_bytes(b"system")
            graph = {"worker.exe": ["QT6CORE.dll", "pdfium.dll", "KERNEL32.dll"],
                     "Qt6Core.dll": ["vcruntime140.dll", "api-ms-win-core-file-l1-1-0.dll"],
                     "pdfium.dll": ["Qt6Core.dll"], "vcruntime140.dll": ["pdfium.dll"]}
            report = collect_worker_runtime(source / "worker.exe", root / "runtime", system,
                                            lambda p: graph[p.name])
            self.assertEqual(report["dll_count"], 3)
            self.assertEqual({p.name for p in (root / "runtime").iterdir()},
                             {"worker.exe", "Qt6Core.dll", "pdfium.dll", "vcruntime140.dll"})

    def test_missing_dependency_fails_before_creating_package(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            worker = root / "worker.exe"
            worker.touch()
            with self.assertRaisesRegex(RuntimeError, "Missing worker dependency"):
                collect_worker_runtime(worker, root / "runtime", root,
                                       lambda p: ["missing.dll"])
            self.assertFalse((root / "runtime").exists())


if __name__ == "__main__":
    unittest.main()

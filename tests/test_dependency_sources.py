#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("sources", Path(__file__).resolve().parents[1] / "tools/package_dependency_sources.py")
sources = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sources)


class SourceBundleTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.revisions = {}
        for relative in (".", ".deps/depot_tools", ".deps/pdfium-build", ".deps/pdfium-source/pdfium", ".deps/pdfium-source/pdfium/v8"):
            directory = self.root / relative
            directory.mkdir(parents=True, exist_ok=True)
            subprocess.run(["git", "init", "-q", str(directory)], check=True)
            (directory / "source.txt").write_text(relative)
            subprocess.run(["git", "-C", str(directory), "add", "source.txt"], check=True)
            if relative == ".":
                (directory / ".gitignore").write_text(".deps/\nartifacts/\n")
                subprocess.run(["git", "-C", str(directory), "add", ".gitignore"], check=True)
            subprocess.run(["git", "-C", str(directory), "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                            "commit", "-qm", "source"], check=True)
            self.revisions[relative] = sources.git(directory, "rev-parse", "HEAD").decode().strip()
        workspace = self.root / ".deps/pdfium-source"
        self.entries = {"pdfium": "https://example.invalid/pdfium.git",
                        "pdfium/v8": "https://example.invalid/v8.git@" + self.revisions[".deps/pdfium-source/pdfium/v8"],
                        "pdfium/tools:package": "pinned-tool-input"}
        (workspace / ".gclient").write_text("solutions = []\n")
        self.write_entries()
        directory = self.root / ".deps/pdfium-patched"
        directory.mkdir()
        (directory / "build-info.json").write_text(json.dumps({
            "pdfium_revision": self.revisions[".deps/pdfium-source/pdfium"],
            "v8_revision": self.revisions[".deps/pdfium-source/pdfium/v8"],
            "depot_tools_revision": self.revisions[".deps/depot_tools"],
            "distributor_revision": self.revisions[".deps/pdfium-build"]}))
        self.output = self.root / "artifacts/sources.tar.gz"

    def write_entries(self):
        (self.root / ".deps/pdfium-source/.gclient_entries").write_text("entries = " + repr(self.entries))

    def test_exact_modified_sources_and_pins_without_git_metadata(self):
        (self.root / ".deps/pdfium-source/pdfium/source.txt").write_text("patched source")
        manifest = sources.package(self.root, self.output)
        self.assertEqual(len(manifest["repositories"]), 5)
        self.assertTrue(manifest["repositories"][3]["modified"])
        with tarfile.open(self.output) as archive:
            self.assertFalse(any(".git" in Path(name).parts for name in archive.getnames()))
            self.assertEqual(archive.extractfile("pdf-editor/.deps/pdfium-source/pdfium/source.txt").read(), b"patched source")
            self.assertEqual(json.load(archive.extractfile("SOURCE-MANIFEST.json")), manifest)
        original = self.output.read_bytes()
        sources.package(self.root, self.output)
        self.assertEqual(self.output.read_bytes(), original)

    def test_wrong_revision_preserves_previous_output(self):
        self.output.parent.mkdir()
        self.output.write_bytes(b"previous")
        self.entries["pdfium/v8"] = "https://example.invalid/v8.git@wrong"
        self.write_entries()
        with self.assertRaisesRegex(ValueError, "revision mismatch"):
            sources.package(self.root, self.output)
        self.assertEqual(self.output.read_bytes(), b"previous")

    def test_uncommitted_application_source_is_rejected(self):
        (self.root / "source.txt").write_text("uncommitted")
        with self.assertRaisesRegex(ValueError, "Commit application changes"):
            sources.package(self.root, self.output)
        self.assertFalse(self.output.exists())

    def test_inventory_cannot_escape_workspace(self):
        self.entries["../outside"] = "https://example.invalid/outside@pin"
        self.write_entries()
        with self.assertRaisesRegex(ValueError, "Unsafe dependency path"):
            sources.package(self.root, self.output)
        self.assertFalse(self.output.exists())

    def test_tracked_symlink_cannot_expose_host_file(self):
        directory = self.root / ".deps/pdfium-source/pdfium"
        (directory / "escape").symlink_to("/etc/passwd")
        subprocess.run(["git", "-C", str(directory), "add", "escape"], check=True)
        with self.assertRaisesRegex(ValueError, "symlink escapes"):
            sources.package(self.root, self.output)
        self.assertFalse(self.output.exists())

    def test_absolute_source_symlink_is_not_portable(self):
        directory = self.root / ".deps/pdfium-source/pdfium"
        (directory / "absolute").symlink_to(directory / "source.txt")
        subprocess.run(["git", "-C", str(directory), "add", "absolute"], check=True)
        with self.assertRaisesRegex(ValueError, "symlink escapes"):
            sources.package(self.root, self.output)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()

# SPDX-License-Identifier: GPL-3.0-only
"""Exercise release input gates and actual GitHub artifact ZIP layouts."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import json
import subprocess
import zipfile

SPEC = importlib.util.spec_from_file_location(
    "publish_preview", Path(__file__).resolve().parents[1] / "tools/publish_preview.py")
preview = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(preview)


class PreviewReleaseTests(unittest.TestCase):
    def setUp(self):
        self.run = {"name": "Native release candidates", "conclusion": "success", "event": "push",
                    "head_branch": "native-releases", "head_repository": {"full_name": "owner/editor"}}
        self.artifacts = [{"name": name, "expired": False, "id": index}
                          for index, name in enumerate((*preview.PACKAGES, preview.QT_SOURCES))]

    def test_complete_build_and_partial_build(self):
        self.assertEqual(len(preview.select_artifacts(self.run, self.artifacts, "owner/editor")), 6)
        self.assertIsNone(preview.select_artifacts(self.run, self.artifacts[:-1], "owner/editor"))
        self.artifacts[0]["expired"] = True
        self.assertIsNone(preview.select_artifacts(self.run, self.artifacts, "owner/editor"))

    def test_failed_fork_and_untrusted_event_are_rejected(self):
        for changes in ({"conclusion": "failure"}, {"event": "pull_request"},
                        {"head_branch": "unreviewed"}, {"head_repository": {"full_name": "fork/editor"}},
                        {"name": "Other workflow"}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                preview.select_artifacts(dict(self.run, **changes), self.artifacts, "owner/editor")

    def collect(self, name, files):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        archive = root / "artifact.zip"
        output = root / "assets"
        output.mkdir()
        with zipfile.ZipFile(archive, "w") as contents:
            for filename, value in files.items():
                contents.writestr(filename, value)
        preview.collect_archive(archive, name, output)
        return archive, output

    def test_windows_retains_single_extraction_and_worker_runtime(self):
        archive, output = self.collect("pdf-editor-windows-x64", {
            "pdf-editor.exe": b"gui", "worker-runtime/pdf-render-worker.exe": b"worker",
            "qml/QtQuick/qmldir": b"module"})
        packaged = output / "pdf-editor-windows-x64.zip"
        self.assertEqual(archive.read_bytes(), packaged.read_bytes())
        with zipfile.ZipFile(packaged) as contents:
            self.assertIn("pdf-editor.exe", contents.namelist())
            self.assertIn("worker-runtime/pdf-render-worker.exe", contents.namelist())

    def test_linux_unwraps_only_the_outer_actions_archive(self):
        _, output = self.collect("pdf-editor-linux-x86_64", {
            "pdf-editor-linux-x86_64.tar.gz": b"tar", "pdf-editor-linux-x86_64.AppImage": b"app",
            "SHA256SUMS": b"old platform checksums"})
        self.assertEqual(sorted(path.suffix for path in output.iterdir()), [".AppImage", ".gz"])
        self.assertEqual((output / "pdf-editor-linux-x86_64.AppImage").read_bytes(), b"app")

    def test_macos_bundle_archive_is_preserved(self):
        _, output = self.collect("pdf-editor-macos-arm64", {"pdf-editor-macos-arm64.zip": b"bundle"})
        self.assertEqual((output / "pdf-editor-macos-arm64.zip").read_bytes(), b"bundle")

    def test_sources_are_included_and_missing_packages_fail(self):
        _, output = self.collect(preview.QT_SOURCES, {
            f"qt{module}-src.tar.xz": b"sources"
            for module in ("base", "declarative", "svg", "imageformats", "shadertools")})
        self.assertEqual(len(list(output.iterdir())), 5)
        for name, files in ((preview.QT_SOURCES, {"qtbase-src.tar.xz": b"incomplete"}),
                            ("pdf-editor-windows-x64", {"nested/pdf-editor.exe": b"nested"}),
                            ("pdf-editor-linux-x86_64", {"pkg.tar.gz": b"missing appimage"}),
                            ("pdf-editor-macos-arm64", {"../escape.zip": b"unsafe"})):
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.collect(name, files)

    def test_publish_uses_exact_commit_and_draft_then_prerelease(self):
        calls = []
        run = dict(self.run, head_sha="a" * 40, html_url="https://example.test/run/123", run_number=9)

        def fake_gh(*args, **kwargs):
            calls.append(args)
            if "--slurp" in args:
                value = [{"artifacts": self.artifacts}] if "/artifacts?" in args[-1] else [[]]
                return subprocess.CompletedProcess(args, 0, stdout=json.dumps(value))
            if args[0] == "api" and "stdout" in kwargs:
                stream = kwargs["stdout"]
                if "/zipball/" in args[1]:
                    with zipfile.ZipFile(stream, "w") as archive:
                        archive.writestr("repo/README.md", "source")
                else:
                    index = int(args[1].split("/")[-2])
                    name = self.artifacts[index]["name"]
                    with zipfile.ZipFile(stream, "w") as archive:
                        if name == "pdf-editor-windows-x64":
                            archive.writestr("pdf-editor.exe", b"gui")
                        elif name == preview.QT_SOURCES:
                            for number in range(5):
                                archive.writestr(f"qt-module-{number}.tar.xz", b"source")
                        elif "linux" in name:
                            archive.writestr(name + ".tar.gz", b"tar")
                            archive.writestr(name + ".AppImage", b"app")
                        else:
                            archive.writestr(name + ".zip", b"bundle")
            if args[:2] == ("release", "upload"):
                assets = [path for path in args[3:] if isinstance(path, Path)]
                self.assertEqual(len(assets), 15)
                checksums = next(path for path in assets if path.name == "SHA256SUMS").read_text()
                for asset in assets:
                    if asset.name != "SHA256SUMS":
                        self.assertIn(f"{preview.digest(asset)}  {asset.name}\n", checksums)
            return subprocess.CompletedProcess(args, 0)

        with patch.object(preview, "gh", side_effect=fake_gh), patch.object(preview, "api", return_value=run), \
                patch.dict(preview.os.environ, {"GH_REPO": "owner/editor"}), \
                patch("sys.argv", ["publish_preview.py", "--run-id", "123"]):
            preview.main()
        create = next(call for call in calls if call[:2] == ("release", "create"))
        self.assertIn("--draft", create)
        self.assertEqual(create[create.index("--target") + 1], "a" * 40)
        edit = next(call for call in calls if call[:2] == ("release", "edit"))
        self.assertIn("--draft=false", edit)
        self.assertIn("--prerelease", edit)
        self.assertIn("--latest=false", edit)


if __name__ == "__main__":
    unittest.main()

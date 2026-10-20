#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bundle exact local PDFium/V8 source checkouts and rebuild tooling, without Git metadata."""
import argparse
import ast
import gzip
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def git(directory, *arguments):
    return subprocess.check_output(["git", "-C", str(directory), *arguments])


def repository_files(directory, prefix, expected=None):
    directory = directory.resolve()
    revision = git(directory, "rev-parse", "HEAD").decode().strip()
    if expected and revision != expected:
        raise ValueError(f"Source revision mismatch: {prefix}")
    files = []
    for raw in git(directory, "ls-files", "-z").split(b"\0"):
        if not raw:
            continue
        relative = PurePosixPath(raw.decode("utf-8"))
        if relative.is_absolute() or ".." in relative.parts or "\\" in str(relative):
            raise ValueError("Unsafe source path")
        path = directory / str(relative)
        if path.is_symlink():
            if path.readlink().is_absolute() or not path.resolve().is_relative_to(directory):
                raise ValueError(f"Source symlink escapes its checkout: {prefix}/{relative}")
        elif path.is_dir():
            continue  # Gitlinks are collected through the dependency manifest.
        elif not path.is_file():
            raise ValueError(f"Missing tracked source: {prefix}/{relative}")
        files.append((path, f"{prefix}/{relative}"))
    return files, {"path": prefix, "revision": revision,
                   "modified": bool(git(directory, "diff", "HEAD", "--name-only").strip())}


def package(root, output):
    root = root.resolve()
    if git(root, "status", "--porcelain", "--untracked-files=normal").strip():
        raise ValueError("Commit application changes before packaging corresponding sources")
    build = json.loads((root / ".deps/pdfium-patched/build-info.json").read_text())
    workspace = root / ".deps/pdfium-source"
    tree = ast.parse((workspace / ".gclient_entries").read_text())
    assignments = [node for node in tree.body if isinstance(node, ast.Assign)
                   and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name)
                   and node.targets[0].id == "entries"]
    if len(assignments) != 1:
        raise ValueError("Missing gclient source inventory")
    entries = ast.literal_eval(assignments[0].value)
    if not isinstance(entries, dict) or not all(isinstance(k, str) and isinstance(v, str)
                                               for k, v in entries.items()):
        raise ValueError("Invalid gclient source inventory")
    if "pdfium" not in entries or "pdfium/v8" not in entries:
        raise ValueError("PDFium and V8 source checkouts are required")
    if entries["pdfium/v8"].rsplit("@", 1)[-1] != build["v8_revision"]:
        raise ValueError("V8 source revision mismatch")
    files, repositories = [], []
    roots = [(root, "pdf-editor", None),
             (root / ".deps/depot_tools", "pdf-editor/.deps/depot_tools", build["depot_tools_revision"]),
             (root / ".deps/pdfium-build", "pdf-editor/.deps/pdfium-build", build["distributor_revision"])]
    for name, source in sorted(entries.items()):
        if ":" in name:
            continue  # CIPD tool binaries remain pinned retrieval inputs, not source code.
        relative = PurePosixPath(name)
        if relative.is_absolute() or ".." in relative.parts or "\\" in name:
            raise ValueError("Unsafe dependency path")
        directory = workspace / name
        if not directory.resolve().is_relative_to(workspace.resolve()):
            raise ValueError("Dependency checkout escapes the workspace")
        expected = build["pdfium_revision"] if name == "pdfium" else source.rsplit("@", 1)[-1]
        roots.append((directory, f"pdf-editor/.deps/pdfium-source/{name}", expected))
    for directory, prefix, expected in roots:
        contents, record = repository_files(directory, prefix, expected)
        files.extend(contents)
        repositories.append(record)
    for name in (".gclient", ".gclient_entries"):
        files.append((workspace / name, f"pdf-editor/.deps/pdfium-source/{name}"))
    files.append((root / ".deps/pdfium-patched/build-info.json", "pdf-editor/.deps/pdfium-patched/build-info.json"))
    manifest = {"scope": "Application, PDFium/V8 and checked-out Git dependency sources; not a complete native runtime source distribution",
                "build": build, "repositories": repositories, "retrieval_inputs": entries, "files": []}
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="sources-", dir=output.parent) as temporary:
        staged = Path(temporary) / "sources.tar.gz"
        with staged.open("wb") as stream, gzip.GzipFile(filename="", fileobj=stream, mode="wb", mtime=0) as compressed, \
                tarfile.open(fileobj=compressed, mode="w") as archive:
            seen = set()
            for path, name in sorted(files, key=lambda item: item[1]):
                if name in seen:
                    raise ValueError(f"Overlapping source checkout: {name}")
                seen.add(name)
                info = archive.gettarinfo(str(path), name)
                info.uid = info.gid = info.mtime = 0
                info.uname = info.gname = ""
                if info.issym():
                    data = info.linkname.encode()
                    archive.addfile(info)
                else:
                    data = path.read_bytes()
                    if len(data) != info.size:
                        raise ValueError("Source changed while packaging")
                    archive.addfile(info, io.BytesIO(data))
                manifest["files"].append({"path": name, "sha256": hashlib.sha256(data).hexdigest()})
            data = (json.dumps(manifest, indent=2) + "\n").encode()
            info = tarfile.TarInfo("SOURCE-MANIFEST.json")
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))
        staged.replace(output)
    checksum = hashlib.sha256()
    with output.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(block)
    output.with_suffix(output.suffix + ".sha256").write_text(checksum.hexdigest() + "  " + output.name + "\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "dist/sources/pdfium-source.tar.gz")
    args = parser.parse_args()
    manifest = package(ROOT, args.output.resolve())
    print(f"Packaged {len(manifest['files'])} source files from {len(manifest['repositories'])} checkouts: {args.output}")
    print("Qt/distribution runtime sources and platform SDKs require separate coverage; see docs/SOURCE-DISTRIBUTION.md.")


if __name__ == "__main__":
    main()

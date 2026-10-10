#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Publish an existing complete native build as a public development prerelease."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tempfile
import zipfile

PACKAGES = (
    "pdf-editor-windows-x64", "pdf-editor-macos-x64", "pdf-editor-macos-arm64",
    "pdf-editor-linux-x86_64", "pdf-editor-linux-aarch64",
)
QT_SOURCES = "qt-sources-windows-x64"


def gh(*arguments, **kwargs):
    return subprocess.run(["gh", *map(str, arguments)], check=True, **kwargs)


def api(endpoint):
    return json.loads(gh("api", endpoint, capture_output=True, text=True).stdout)


def list_releases(repository):
    pages = json.loads(gh("api", "--paginate", "--slurp", f"repos/{repository}/releases?per_page=100",
                          capture_output=True, text=True).stdout)
    return [release for page in pages for release in page]


def preview_id(release):
    match = re.fullmatch(r"preview-(\d+)", release["tag_name"])
    return int(match[1]) if match and release["prerelease"] else None


def cleanup_previews(repository):
    releases = list_releases(repository)
    published = [preview_id(release) for release in releases
                 if not release["draft"] and preview_id(release) is not None]
    if not published:
        return
    newest = max(published)
    for release in releases:
        number = preview_id(release)
        if number is not None and number < newest:
            gh("release", "delete", release["tag_name"], "--cleanup-tag", "--yes")
            print("Removed superseded development preview: " + release["tag_name"])


def select_artifacts(run, artifacts, repository):
    if (run["name"] != "Native release candidates" or run["conclusion"] != "success"
            or run["event"] not in ("push", "workflow_dispatch")
            or run["head_branch"] not in ("native-releases", "main", "master")
            or run["head_repository"]["full_name"] != repository):
        raise ValueError("Only successful native builds from trusted repository branches can publish")
    available = {item["name"]: item for item in artifacts if not item["expired"]}
    missing = set((*PACKAGES, QT_SOURCES)) - available.keys()
    if missing:
        print("No preview published: incomplete/expired build artifacts: " + ", ".join(sorted(missing)))
        return None
    return {name: available[name] for name in (*PACKAGES, QT_SOURCES)}


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def collect_archive(archive, name, output):
    """Keep the Windows artifact itself; unwrap only package/source archives elsewhere."""
    with zipfile.ZipFile(archive) as contents:
        for member in contents.infolist():
            path = PurePosixPath(member.filename)
            if path.is_absolute() or ".." in path.parts or "\\" in member.filename:
                raise ValueError("Unsafe artifact member: " + member.filename)
        if name == "pdf-editor-windows-x64":
            if "pdf-editor.exe" not in contents.namelist():
                raise ValueError("Windows artifact must contain pdf-editor.exe at its root")
            shutil.copyfile(archive, output / (name + ".zip"))
            return
        if name == QT_SOURCES:
            selected = [item for item in contents.infolist() if item.filename.endswith(".tar.xz")]
            if len(selected) != 5:
                raise ValueError("Expected all five matching Qt module source archives")
        else:
            selected = [item for item in contents.infolist()
                        if item.filename.endswith((".zip", ".tar.gz", ".AppImage"))]
            expected = 2 if "linux" in name else 1
            if len(selected) != expected:
                raise ValueError(f"Expected {expected} application packages in {name}")
        for member in selected:
            destination = output / PurePosixPath(member.filename).name
            if destination.exists():
                raise ValueError("Duplicate release asset: " + destination.name)
            with contents.open(member) as source, destination.open("wb") as target:
                shutil.copyfileobj(source, target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-id", required=True, type=int)
    args = parser.parse_args()
    repository = os.environ["GH_REPO"]
    run = api(f"repos/{repository}/actions/runs/{args.run_id}")
    artifacts = json.loads(gh("api", "--paginate", "--slurp",
                             f"repos/{repository}/actions/runs/{args.run_id}/artifacts?per_page=100",
                             capture_output=True, text=True).stdout)
    selected = select_artifacts(run, [item for page in artifacts for item in page["artifacts"]], repository)
    if selected is None:
        return
    # One immutable build identity per preview: no moving tags or mixed builds.
    tag = f"preview-{args.run_id}"
    releases = list_releases(repository)
    existing = next((release for release in releases if release["tag_name"] == tag), None)
    if existing and not existing["draft"]:
        print("Already published: " + existing["html_url"])
        cleanup_previews(repository)
        return
    if any(not release["draft"] and (preview_id(release) or 0) > args.run_id for release in releases):
        print("No preview published: a newer development preview is already available")
        cleanup_previews(repository)
        return
    with tempfile.TemporaryDirectory(prefix="pdf-editor-preview-") as temporary:
        folder = Path(temporary)
        output = folder / "assets"
        output.mkdir()
        for name, artifact in selected.items():
            archive = folder / (name + ".zip")
            with archive.open("wb") as stream:
                gh("api", f"repos/{repository}/actions/artifacts/{artifact['id']}/zip", stdout=stream)
            collect_archive(archive, name, output)
        sha = run["head_sha"]
        with (output / "pdf-editor-source.zip").open("wb") as source:
            gh("api", f"repos/{repository}/zipball/{sha}", stdout=source)
        (output / "build-info.json").write_text(json.dumps({
            "commit": sha, "run_url": run["html_url"], "run_id": args.run_id,
            "channel": "development-preview", "stable": False,
        }, indent=2) + "\n")
        (output / "SHA256SUMS").write_text("".join(
            f"{digest(path)}  {path.name}\n" for path in sorted(output.iterdir())))
        notes = folder / "notes.md"
        notes.write_text(
            "**Development preview — not a stable release.**\n\n"
            "These packages passed the native build's automated packaging and PDF checks. "
            "Windows binaries are unsigned; macOS bundles are not notarized. "
            "Desktop acceptance and production release gates remain open.\n\n"
            "Download the package for your system below. Extract the Windows ZIP once, "
            "then open `pdf-editor.exe`; keep its DLLs and folders together. "
            "Linux packages are portable tarballs and AppImages. macOS ZIPs contain the app bundle.\n\n"
            f"Built from [{sha[:12]}](https://github.com/{repository}/commit/{sha}). "
            f"[Validation run]({run['html_url']}).\n\n"
            "Application sources and matching Qt sources are attached. PDFium/V8 dependency "
            "pins, patches and rebuild instructions are in the source repository. "
            "See `docs/NATIVE-RELEASES.md` for dependency source retrieval and known limitations.\n")
        if not existing:
            gh("release", "create", tag, "--target", sha, "--draft", "--prerelease", "--latest=false",
               "--title", f"Development preview — build {run['run_number']}", "--notes-file", notes)
        # A failed upload leaves a draft, rather than a publicly incomplete release.
        gh("release", "upload", tag, *sorted(output.iterdir()), "--clobber")
        gh("release", "edit", tag, "--draft=false", "--prerelease", "--latest=false",
           "--notes-file", notes)
        print(f"Published development preview: https://github.com/{repository}/releases/tag/{tag}")
        # Only clean up after all uploads and the public publication succeeded.
        cleanup_previews(repository)


if __name__ == "__main__":
    main()

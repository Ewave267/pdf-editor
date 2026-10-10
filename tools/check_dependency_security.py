#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check tracked runtime inputs against OSV; no build or credentials required."""
import argparse
import ast
import datetime
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def query(payload):
    findings = []
    for _ in range(20):
        request = urllib.request.Request("https://api.osv.dev/v1/query",
                                         data=json.dumps(payload).encode(),
                                         headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=45) as response:
            data = response.read(32 * 1024 * 1024 + 1)
        if len(data) > 32 * 1024 * 1024:
            raise RuntimeError("OSV response exceeds the response limit")
        result = json.loads(data)
        findings += [value for value in result.get("vulns", []) if not value.get("withdrawn")]
        token = result.get("next_page_token")
        if not token:
            return findings
        payload = dict(payload, page_token=token)
    raise RuntimeError("OSV pagination did not complete; review is required")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "dist/dependency-security.json")
    args = parser.parse_args()
    # Read literal upstream pins without importing/executing the build script.
    tree = ast.parse((ROOT / "tools/build_pdfium.py").read_text())
    pins = {node.targets[0].id: ast.literal_eval(node.value) for node in tree.body
            if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name)
            and node.targets[0].id in ("PDFIUM_REVISION", "V8_REVISION", "VERSION")}
    reader = json.loads((ROOT / "tests/reader/package.json").read_text())
    inputs = {
        "PDFium": {"commit": pins["PDFIUM_REVISION"]},
        "V8": {"commit": pins["V8_REVISION"]},
        "Qt desktop qtbase": {"package": {"name": "https://code.qt.io/qt/qtbase.git", "ecosystem": "GIT"}, "version": "v6.8.3"},
        "Qt desktop qtdeclarative": {"package": {"name": "https://code.qt.io/qt/qtdeclarative.git", "ecosystem": "GIT"}, "version": "v6.8.3"},
        "Independent reader": {"package": {"name": "pdfjs-dist", "ecosystem": "npm"}, "version": reader["dependencies"]["pdfjs-dist"]},
    }
    manifest = ROOT / ".deps/pdfium-patched/build-info.json"
    if manifest.exists():
        build = json.loads(manifest.read_text())
        if build["pdfium_revision"] != pins["PDFIUM_REVISION"] or build["v8_revision"] != pins["V8_REVISION"]:
            raise RuntimeError("Cached PDFium metadata does not match the source pin")
    report = {"checked_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "coverage": "OSV-listed advisories only; review Qt notices and Linux distribution advisories separately", "inputs": {}}
    report["v8_coverage"] = "queried tracked source pin"
    count = 0
    for name, payload in inputs.items():
        findings = query(payload)
        count += len(findings)
        report["inputs"][name] = {"query": payload, "findings": findings}
        print(f"{name}: {len(findings)} advisory findings")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    if count:
        raise SystemExit("Dependency advisories require review before release")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate deterministic, original PDF fixtures without a PDF dependency."""
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


def pdf(objects: list[bytes]) -> bytes:
    result = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
    offsets = [0]
    for number, body in enumerate(objects, 1):
        offsets.append(len(result))
        result.extend(f"{number} 0 obj\n".encode() + body + b"\nendobj\n")
    xref = len(result)
    result.extend(f"xref\n0 {len(offsets)}\n0000000000 65535 f \n".encode())
    for offset in offsets[1:]:
        result.extend(f"{offset:010d} 00000 n \n".encode())
    result.extend(
        f"trailer\n<< /Size {len(offsets)} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()
    )
    return bytes(result)


def stream(data: bytes) -> bytes:
    return f"<< /Length {len(data)} >>\nstream\n".encode() + data + b"\nendstream"


def generate(output_root: Path = ROOT) -> None:
    fixture = ROOT / "tests/pdfs/xfa-javascript/calculation.xdp"
    output_fixture = output_root / fixture.relative_to(ROOT)
    for category in ("xfa-javascript", "normal", "malformed"):
        (output_root / "tests/pdfs" / category).mkdir(parents=True, exist_ok=True)
    document = pdf([
        b"<< /Type /Catalog /Pages 2 0 R /AcroForm 4 0 R /NeedsRendering true "
        b"/Extensions << /ADBE << /BaseVersion /1.7 /ExtensionLevel 8 >> >> >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> >>",
        b"<< /XFA 5 0 R /Fields [] >>",
        stream(fixture.read_bytes()),
    ])
    # PDFium loads a single XDP stream, but its save path only serializes an
    # XFA packet array. Retain both encodings to expose that compatibility gap.
    output_fixture.with_name("single-stream.pdf").write_bytes(document)
    root = ET.fromstring(fixture.read_bytes())
    packets = [b'<xdp:xdp xmlns:xdp="http://ns.adobe.com/xdp/">']
    packets.extend(ET.tostring(child, encoding="utf-8") for child in root)
    packets.append(b"</xdp:xdp>")
    names = ["preamble", "config", "template", "datasets", "postamble"]
    references = " ".join(f"({name}) {index} 0 R" for index, name in enumerate(names, 5))
    document = pdf([
        b"<< /Type /Catalog /Pages 2 0 R /AcroForm 4 0 R /NeedsRendering true "
        b"/Extensions << /ADBE << /BaseVersion /1.7 /ExtensionLevel 8 >> >> >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> >>",
        f"<< /XFA [{references}] /Fields [] >>".encode(),
        *(stream(packet) for packet in packets),
    ])
    output_fixture.with_suffix(".pdf").write_bytes(document)
    (output_root / "tests/pdfs/normal/blank.pdf").write_bytes(pdf([
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << >> >>",
    ]))
    (output_root / "tests/pdfs/malformed/not-a-pdf.pdf").write_bytes(b"This is not a PDF.\n")


if __name__ == "__main__":
    generate()

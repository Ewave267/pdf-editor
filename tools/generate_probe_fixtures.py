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


def viewer_pdf(page_count: int) -> bytes:
    sizes = [(600, 780), (780, 600), (420, 840)]
    colors = [(0.2, 0.45, 0.8), (0.2, 0.65, 0.4), (0.9, 0.5, 0.16)]
    kids = " ".join(f"{4 + 2 * i} 0 R" for i in range(page_count))
    objects = [b"<< /Type /Catalog /Pages 2 0 R >>",
               f"<< /Type /Pages /Kids [{kids}] /Count {page_count} >>".encode(),
               b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"]
    for index in range(page_count):
        width, height = sizes[index]
        red, green, blue = colors[index]
        content = (f"{red} {green} {blue} rg 32 96 {width - 64} {height - 192} re f\n"
                   f"0.12 0.18 0.25 rg BT /F1 30 Tf 48 {height - 64} Td (PAGE {index + 1}) Tj ET\n"
                   f"BT /F1 14 Tf 48 52 Td (Original synthetic viewer fixture - {width} x {height} pt) Tj ET\n").encode()
        objects.extend([f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 {width} {height}] /Resources << /Font << /F1 3 0 R >> >> /Contents {5 + 2 * index} 0 R >>".encode(), stream(content)])
    return pdf(objects)


def generate(output_root: Path = ROOT) -> None:
    fixture = ROOT / "tests/pdfs/xfa-javascript/calculation.xdp"
    output_fixture = output_root / fixture.relative_to(ROOT)
    for category in ("xfa-javascript", "normal", "malformed", "acroform"):
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
    (output_root / "tests/pdfs/acroform/text.pdf").write_bytes(pdf([
        b"<< /Type /Catalog /Pages 2 0 R /AcroForm 4 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 6 0 R >> >> /Contents 7 0 R /Annots [5 0 R] >>",
        b"<< /Fields [5 0 R] /DR << /Font << /Helv 6 0 R >> >> /DA (/Helv 18 Tf 0 g) >>",
        b"<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /V (Edited value) /DV (Original value) /Rect [40 620 340 660] /P 3 0 R /F 4 /DA (/Helv 18 Tf 0 g) /AP << /N 8 0 R >> /AA << /K << /S /JavaScript /JS (event.change = event.change.toUpperCase\\(\\);) >> >> >>",
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        stream(b"BT /F1 24 Tf 40 720 Td (Original AcroForm document) Tj ET"),
        b"<< /Type /XObject /Subtype /Form /BBox [0 0 300 40] /Resources << /Font << /Helv 6 0 R >> >> /Length 48 >>\nstream\nBT /Helv 18 Tf 0 g 4 12 Td (Edited value) Tj ET\nendstream",
    ]))
    (output_root / "tests/pdfs/normal/rotated-cropped.pdf").write_bytes(pdf([
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 >>",
        *(f"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 700 900] /CropBox [50 60 650 840] /Rotate {rotation} /Resources << >> >>".encode() for rotation in (0,90,270)),
    ]))
    (output_root / "tests/pdfs/normal/single-page.pdf").write_bytes(viewer_pdf(1))
    (output_root / "tests/pdfs/normal/multi-page.pdf").write_bytes(viewer_pdf(3))
    (output_root / "tests/pdfs/malformed/not-a-pdf.pdf").write_bytes(b"This is not a PDF.\n")


if __name__ == "__main__":
    generate()

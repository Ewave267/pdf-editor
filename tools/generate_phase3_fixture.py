#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate the original, redistributable multi-page Phase 3 AcroForm fixture."""
from pathlib import Path
from generate_probe_fixtures import pdf, stream, xfa_pdf

ROOT = Path(__file__).resolve().parents[1]


def generate():
    def text(name, rect, value='', flags=0, extra=''):
        return f'<< /Type /Annot /Subtype /Widget /FT /Tx /T ({name}) /TU ({name}) /Rect [{rect}] /F 4 /Ff {flags} /V ({value}) /DA (/Helv 12 Tf 0 g) {extra} >>'.encode()

    def appearance(data):
        return b'<< /Type /XObject /Subtype /Form /BBox [0 0 20 20] /Resources << >> /Length ' + str(len(data)).encode() + b' >>\nstream\n' + data + b'\nendstream'

    objects = [
        b'<< /Type /Catalog /Pages 2 0 R /AcroForm 4 0 R >>',
        b'<< /Type /Pages /Kids [5 0 R 7 0 R] /Count 2 >>',
        b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
        b'<< /Fields [9 0 R 10 0 R 11 0 R 12 0 R 13 0 R 14 0 R 15 0 R] /DR << /Font << /Helv 3 0 R >> >> /DA (/Helv 12 Tf 0 g) /NeedAppearances true >>',
        b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R >> >> /Contents 6 0 R /Annots [9 0 R 10 0 R 11 0 R 12 0 R 13 0 R 14 0 R] /Tabs /R >>',
        stream(b'BT /F1 18 Tf 72 750 Td (Everyday form experience) Tj ET'),
        b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 600 900] /Resources << /Font << /F1 3 0 R >> >> /Contents 8 0 R /Annots [15 0 R] /Tabs /R >>',
        stream(b'BT /F1 18 Tf 72 850 Td (Cross-page field navigation) Tj ET'),
        text('Required name', '72 690 300 720', flags=2, extra='/P 5 0 R'),
        text('Date', '72 630 300 660', flags=2, extra=r'/P 5 0 R /AA << /F << /S /JavaScript /JS (AFDate_FormatEx\("yyyy-mm-dd"\);) >> >>'),
        b'<< /Type /Annot /Subtype /Widget /FT /Ch /T (Choice) /Rect [72 570 272 600] /P 5 0 R /F 4 /Ff 131074 /Opt [(Alpha) (Beta) (Gamma)] /V (Alpha) /DA (/Helv 12 Tf 0 g) >>',
        b'<< /Type /Annot /Subtype /Widget /FT /Btn /T (Consent) /Rect [72 510 92 530] /P 5 0 R /F 4 /Ff 2 /V /Off /AS /Off /AP << /N << /Off 16 0 R /Yes 17 0 R >> >> >>',
        text('Read only', '72 450 300 480', 'LOCKED', 1, '/P 5 0 R'),
        b'<< /Type /Annot /Subtype /Widget /FT /Sig /T (Signature) /Rect [72 350 300 410] /P 5 0 R /F 4 >>',
        text('Second page', '72 70 300 100', 'original second page', 0, '/P 7 0 R'),
        appearance(b'q 1 w 0 G 1 1 18 18 re S Q'),
        appearance(b'q 1 w 0 G 1 1 18 18 re S 0 g 5 5 10 10 re f Q'),
    ]
    (ROOT / 'tests/pdfs/acroform/phase3.pdf').write_bytes(pdf(objects))
    locked = list(objects)
    locked[8] = locked[8][:-2] + br' /AA << /Fo << /S /JavaScript /JS (this.getField\("Required name"\).readonly = true;) >> >> >>'
    (ROOT / 'tests/pdfs/acroform/phase3-lock-on-focus.pdf').write_bytes(pdf(locked))
    xml = (ROOT / 'tests/pdfs/xfa-dynamic/phase3-navigation.xdp').read_bytes()
    (ROOT / 'tests/pdfs/xfa-dynamic/phase3-navigation.pdf').write_bytes(xfa_pdf(xml))


if __name__ == '__main__':
    generate()

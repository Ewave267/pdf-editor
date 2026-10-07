# Probe fixture corpus

All current fixtures are original synthetic test documents created for this
project, contain no personal data or external copyrighted content, and use
the repository's GPL-3.0-only license. The scripted URL uses the reserved
`example.invalid` domain and is denied by the embedder callback.

Regenerate the PDFs with `python3 tools/generate_probe_fixtures.py`. The XFA XML
sources are `xfa-javascript/calculation.xdp` and `xfa-dynamic/controls.xdp`; the generator writes deterministic
uncompressed PDFs with byte-accurate cross-reference tables.

| Fixture | Purpose | Expected behavior with patched pinned build |
| --- | --- | --- |
| `xfa-javascript/calculation.pdf` | Dynamic XFA packet array; editable input, initial calculation, exit-event JavaScript, denied URL action | Renders, executes JavaScript, edits, and preserves exact values across repeated saves |
| `xfa-javascript/single-stream.pdf` | Same form as one XDP stream | Same exact round trip as packet arrays; independent PDF.js layout retains values |
| `normal/single-page.pdf` | Original labeled/color-coded one-page viewer fixture | Opens and renders at 600 × 780 points |
| `normal/multi-page.pdf` | Three labeled/color-coded pages with portrait, landscape and tall layouts | Every page and thumbnail preserves its own dimensions and color |
| `acroform/controls.pdf` | Text, checkbox, mutually exclusive radios and dropdown | Native input, keyboard navigation and exact saved control values verified in PDF.js |
| `xfa-dynamic/controls.pdf` | Same controls plus calculated text and exit-event JavaScript | Native input, checked/unchecked persistence, radio exclusivity and exact values verified in PDF.js |
| `acroform/text.pdf` | Editable text widget with differing value/default and a keystroke JavaScript action | Save with additions retains values, widget, appearance and actions |
| `normal/rotated-cropped.pdf` | Three cropped pages rotated 0, 90 and 270 degrees | Saved additions retain displayed positions |
| `normal/blank.pdf` | Valid one-page PDF without forms | Probe rejects it as non-XFA |
| `malformed/not-a-pdf.pdf` | Invalid PDF bytes | Probe rejects it with a PDFium open error |

See [FORMS](../../docs/FORMS.md) for the pinned AcroForm script limitation.
No real-world static or dynamic XFA documents have been added yet. The empty
category directories reserve space for future licensed/privacy-reviewed
regression documents.

// SPDX-License-Identifier: GPL-3.0-only
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";
const [directory, filename] = process.argv.slice(2);
const { getDocument, version } = await import(pathToFileURL(resolve(directory, "legacy/build/pdf.mjs")));
assert.equal(version, "6.4.299");
const task = getDocument({ data: new Uint8Array(await readFile(filename)),
  isEvalSupported: false, disableFontFace: true, useSystemFonts: false, fontExtraProperties: true });
try {
  const pdf = await task.promise;
  const page = await pdf.getPage(1);
  const text = await page.getTextContent();
  const added = text.items.find(item => item.str === "Styled text");
  assert.ok(added, "Added text must survive saving");
  assert.ok(Math.abs(Math.hypot(added.transform[0], added.transform[1]) - 24) < 0.1,
    "Saved font size must be 24 points");
  await page.getOperatorList();
  const font = page.commonObjs.get(added.fontName);
  assert.match(font.name, /LiberationSerif.*BoldItalic/i);
  assert.ok(font.data?.length > 0, "Font must be embedded, without system-font fallback");
  console.log("PASS: PDF.js verified embedded bold italic serif text at 24 points");
} finally {
  await task.destroy();
}

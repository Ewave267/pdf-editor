// SPDX-License-Identifier: GPL-3.0-only
// Independent reader verification: PDF.js opens and lays out the saved XFA.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

const [pdfjsDirectory, savedPdf, expected] = process.argv.slice(2);
if (!pdfjsDirectory || !savedPdf || expected === undefined) {
  throw new Error("Usage: node check_xfa_with_pdfjs.mjs PDFJS_DIRECTORY SAVED_PDF EXPECTED_VALUE");
}
const { getDocument, version } = await import(
  pathToFileURL(resolve(pdfjsDirectory, "legacy/build/pdf.mjs"))
);
assert.equal(version, "6.4.299", "independent reader version must match the pinned package");
const task = getDocument({
  data: new Uint8Array(await readFile(savedPdf)),
  enableXfa: true,
  isEvalSupported: false,
  disableFontFace: true,
  useSystemFonts: true,
});
try {
  const document = await task.promise;
  assert.equal(document.isPureXfa, true, "saved document must retain dynamic XFA");
  assert.equal(document.numPages, 1);
  const page = await document.getPage(1);
  const viewport = page.getViewport({ scale: 1 });
  assert.equal(viewport.width, 612);
  assert.equal(viewport.height, 792);
  const tree = await page.getXfa();
  assert.ok(tree, "independent XFA page layout must exist");
  function find(node, predicate) {
    if (predicate(node)) return node;
    for (const child of node.children ?? []) {
      const found = find(child, predicate);
      if (found) return found;
    }
    return undefined;
  }
  for (const [name, value] of [["input", expected], ["calculated", `JS:${expected}`]]) {
    const field = find(tree, node => node.attributes?.xfaName === name);
    assert.ok(field, `PDF.js must lay out the ${name} field`);
    const control = find(field, node => node.name === "input" || node.name === "textarea");
    assert.ok(control, `PDF.js must produce a control for ${name}`);
    assert.equal(control.attributes?.value ?? control.value, value, `${name} must preserve its exact value`);
  }
  console.log(`PASS: PDF.js ${version} independently opened and laid out the saved XFA with exact values`);
} finally {
  await task.destroy();
}

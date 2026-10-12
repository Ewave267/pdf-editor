// SPDX-License-Identifier: GPL-3.0-only
// Independent opening/layout admission check; does not certify saved values.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const [pdfjsDirectory, manifestPath] = process.argv.slice(2);
const record = JSON.parse(await readFile(manifestPath, "utf8"));
const { getDocument, version } = await import(pathToFileURL(resolve(pdfjsDirectory, "legacy/build/pdf.mjs")));
assert.equal(version, "6.4.299");
const expected = record.expected;
const task = getDocument({
  data: new Uint8Array(await readFile(resolve(dirname(manifestPath), record.file))),
  enableXfa: true, isEvalSupported: false, disableFontFace: true, useSystemFonts: true,
});
try {
  const document = await task.promise;
  assert.equal(document.isPureXfa, expected.pure_xfa);
  assert.equal(document.numPages, expected.pages.length);
  for (let index = 0; index < document.numPages; index++) {
    const page = await document.getPage(index + 1);
    const viewport = page.getViewport({ scale: 1 });
    assert.equal(viewport.width, expected.pages[index].width);
    assert.equal(viewport.height, expected.pages[index].height);
    const controls = [], names = [];
    function visit(node) {
      if (node.attributes?.xfaName) names.push(node.attributes.xfaName);
      if (["input", "textarea", "select"].includes(node.name)) controls.push(node);
      for (const child of node.children ?? []) visit(child);
    }
    const tree = await page.getXfa();
    assert.ok(tree, `page ${index + 1} must have XFA layout`);
    visit(tree);
    assert.equal(controls.length, expected.controls_per_page[index]);
    const values = controls.map(node => node.attributes?.value ?? node.value ?? "");
    assert.ok(values.every(value => value === "" || value === "0"), "template must have no filled responses");
    assert.equal(values.filter(value => value === "0").length, expected.unchecked_per_page[index]);
    for (const name of expected.sample_fields_per_page[index]) assert.ok(names.includes(name), `missing ${name}`);
  }
  console.log(`PASS: PDF.js independently verified ${record.file} opening and XFA layout`);
} finally {
  await task.destroy();
}

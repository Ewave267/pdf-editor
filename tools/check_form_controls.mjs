// SPDX-License-Identifier: GPL-3.0-only
// Verify persisted widget state with a reader independent of PDFium.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";
const [directory, filename, kind, checked = "1", radio = "B", dropdown = "Beta"] = process.argv.slice(2);
const { getDocument, version } = await import(pathToFileURL(resolve(directory, "legacy/build/pdf.mjs")));
assert.equal(version, "6.4.299");
const task = getDocument({data: new Uint8Array(await readFile(filename)), enableXfa: true,
  isEvalSupported: false, disableFontFace: true, useSystemFonts: true});
function find(node, predicate) {
  if (predicate(node)) return node;
  for (const child of node.children ?? []) {
    const found = find(child, predicate);
    if (found) return found;
  }
}
try {
  const pdf = await task.promise;
  assert.equal(pdf.numPages, 1);
  if (kind === "normal") {
    const text = await (await pdf.getPage(1)).getTextContent();
    assert.ok(text.items.map(item => item.str ?? "").join(" ").includes("Native release smoke test"),
      "Added text did not survive saving/reopening");
  } else if (kind === "acroform") {
    const fields = await pdf.getFieldObjects();
    assert.equal(fields.get("Input")[0].value, "edited");
    assert.equal(fields.get("Check")[0].value, checked === "1" ? "Yes" : "Off");
    const buttons = fields.get("Choice").filter(field => field.type === "radiobutton");
    assert.equal(buttons.length, 2);
    for (const button of buttons) assert.equal(button.value, radio);
    assert.equal(fields.get("Dropdown")[0].value, dropdown);
    const annotations = await (await pdf.getPage(1)).getAnnotations();
    assert.equal(annotations.filter(field => field.fieldName === "Choice" && field.fieldValue === radio).length, 2);
  } else {
    assert.equal(pdf.isPureXfa, true);
    const tree = await (await pdf.getPage(1)).getXfa();
    const control = name => {
      const field = find(tree, node => node.attributes?.xfaName === name);
      assert.ok(field, `missing field ${name}`);
      const widget = find(field, node => ["input", "textarea", "select"].includes(node.name));
      assert.ok(widget, `missing control ${name}`);
      return widget;
    };
    assert.equal(control("input").attributes.value, "edited");
    assert.equal(control("calculated").attributes.value, "JS:edited");
    assert.equal(!!control("check").attributes.checked, checked === "1");
    assert.equal(!!control("optionA").attributes.checked, radio === "A");
    assert.equal(!!control("optionB").attributes.checked, radio === "B");
    assert.equal(control("dropdown").attributes.value, dropdown);
  }
  console.log(kind === "normal" ? "PASS: PDF.js verified saved added text" :
    `PASS: PDF.js verified saved ${kind} text, checkbox, radio and dropdown values`);
} finally {
  await task.destroy();
}

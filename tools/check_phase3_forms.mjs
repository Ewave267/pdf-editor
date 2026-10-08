// SPDX-License-Identifier: GPL-3.0-only
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";
const [directory, filename, kind] = process.argv.slice(2);
const { getDocument, version, OPS } = await import(pathToFileURL(resolve(directory, "legacy/build/pdf.mjs")));
assert.equal(version, "6.4.299");
const task = getDocument({data: new Uint8Array(await readFile(filename)), isEvalSupported: false,
  disableFontFace: true, enableXfa: true});
try {
  const pdf = await task.promise;
  assert.equal(pdf.numPages, 2);
  if (kind === "acroform") {
    const fields = await pdf.getFieldObjects();
    for (const [name, expected] of Object.entries({"Required name": "Saved helper", "Date": "2026-10-08",
      "Choice": "Beta", "Consent": "Yes", "Read only": "LOCKED", "Second page": "Saved second"})) {
      assert.equal(fields.get(name)[0].value, expected, name);
    }
    assert.equal(fields.get("Read only")[0].editable, false);
    const annotations = await (await pdf.getPage(1)).getAnnotations();
    assert.equal(annotations.find(field => field.fieldName === "Read only").readOnly, true);
    assert.equal(annotations.find(field => field.fieldName === "Required name").fieldFlags & 2, 2);
    assert.equal(fields.get("Signature")[0].type, "signature");
    assert.doesNotMatch((await readFile(filename)).toString("latin1"), /\/ByteRange\s*\[/,
      "A visual signature must not add a cryptographic signature dictionary");
    const page = await pdf.getPage(1);
    const ops = await page.getOperatorList();
    assert.ok(ops.fnArray.includes(OPS.constructPath), "Visual signature stroke must persist");
  } else {
    const find = (node, predicate) => {
      if (predicate(node)) return node;
      for (const child of node.children ?? []) { const match = find(child, predicate); if (match) return match; }
    };
    for (const [index, name] of ["first", "second"].entries()) {
      const tree = await (await pdf.getPage(index+1)).getXfa();
      const field = find(tree, node => node.attributes?.xfaName === name);
      assert.ok(field, name);
      const widget = find(field, node => ["input", "textarea"].includes(node.name));
      assert.ok(widget, name);
      assert.equal(widget.attributes.value, name, name);
    }
  }
  console.log(`PASS: PDF.js independently verified Phase 3 ${kind} values and form structure`);
} finally { await task.destroy(); }

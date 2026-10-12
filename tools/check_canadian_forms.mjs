// SPDX-License-Identifier: GPL-3.0-only
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

const [directory, original, saved, editedField, value] = process.argv.slice(2);
const { getDocument, version } = await import(pathToFileURL(resolve(directory, "legacy/build/pdf.mjs")));
assert.equal(version, "6.4.299");
const tasks = [];
try {
  const documents = [];
  for (const filename of [original, saved]) {
    const task = getDocument({ data: new Uint8Array(await readFile(filename)),
      isEvalSupported: false, disableFontFace: true });
    tasks.push(task);
    documents.push(await task.promise);
  }
  const [before, after] = documents;
  assert.equal(after.numPages, before.numPages);
  const fieldsBefore = await before.getFieldObjects();
  const fieldsAfter = await after.getFieldObjects();
  assert.ok(fieldsBefore.has(editedField), editedField);
  assert.deepEqual([...fieldsAfter.keys()].sort(), [...fieldsBefore.keys()].sort());
  for (const [name, widgets] of fieldsBefore) {
    const expected = widgets.map(widget => ({ type: widget.type, page: widget.page,
      value: name === editedField && widget.page >= 0 ? value : widget.value }));
    assert.deepEqual(fieldsAfter.get(name).map(widget => ({ type: widget.type,
      page: widget.page, value: widget.value })), expected, name);
  }
  console.log("PASS: PDF.js verified production form structure and saved values");
} finally {
  for (const task of tasks) await task.destroy();
}

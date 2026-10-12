// SPDX-License-Identifier: GPL-3.0-only
// Independent production XFA admission and saved-control preservation check.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const [pdfjsDirectory, manifestPath, savedPdf, savedValue, scenarioName] = process.argv.slice(2);
assert.equal(savedPdf === undefined, savedValue === undefined, "saved PDF and expected value must be supplied together");
assert.ok(scenarioName === undefined || (savedPdf && scenarioName === "calculation_round_trip"));
const record = JSON.parse(await readFile(manifestPath, "utf8"));
const { getDocument, version } = await import(pathToFileURL(resolve(pdfjsDirectory, "legacy/build/pdf.mjs")));
assert.equal(version, "6.4.299");
const expected = record.expected;
let baseline;
for (const file of [resolve(dirname(manifestPath), record.file), ...(savedPdf ? [savedPdf] : [])]) {
  const saved = file === savedPdf;
  const task = getDocument({
    data: new Uint8Array(await readFile(file)),
    enableXfa: true, isEvalSupported: false, disableFontFace: true, useSystemFonts: true,
  });
  try {
    const document = await task.promise;
    assert.equal(document.isPureXfa, expected.pure_xfa);
    assert.equal(document.numPages, expected.pages.length);
    const state = [];
    for (let index = 0; index < document.numPages; index++) {
      const page = await document.getPage(index + 1);
      const viewport = page.getViewport({ scale: 1 });
      assert.equal(viewport.width, expected.pages[index].width);
      assert.equal(viewport.height, expected.pages[index].height);
      const controls = [], names = [];
      function visit(node, name = "") {
        name = node.attributes?.xfaName ?? name;
        if (node.attributes?.xfaName) names.push(node.attributes.xfaName);
        if (["input", "textarea", "select"].includes(node.name)) {
          controls.push(node);
          state.push({ page: index, name, type: node.name,
            value: node.attributes?.value ?? node.attributes?.textContent ?? node.value ?? "",
            checked: node.attributes?.checked ?? false });
        }
        for (const child of node.children ?? []) visit(child, name);
      }
      const tree = await page.getXfa();
      assert.ok(tree, `page ${index + 1} must have XFA layout`);
      visit(tree);
      assert.equal(controls.length, expected.controls_per_page[index]);
      const values = controls.map(node => node.attributes?.value ?? node.attributes?.textContent ?? node.value ?? "");
      if (!saved) {
        assert.ok(values.every(value => value === "" || value === "0"), "template must have no filled responses");
        assert.equal(values.filter(value => value === "0").length, expected.unchecked_per_page[index]);
      }
      for (const name of expected.sample_fields_per_page[index]) assert.ok(names.includes(name), `missing ${name}`);
    }
    if (saved) {
      const scenario = expected.round_trip;
      const expectedState = baseline.map(control => ({ ...control }));
      let changes = scenario.committed_controls;
      if (scenarioName) {
        const calculation = expected[scenarioName];
        const index = Number(savedValue) - 1;
        assert.ok(Number.isInteger(index) && index >= 0 && index < calculation.generations.length);
        const generation = calculation.generations[index];
        assert.equal(generation.values.length, calculation.inputs.length);
        assert.equal(generation.totals.length, changes.length);
        changes = [
          ...calculation.inputs.map((field, index) => ({ ...field, page: calculation.page, value: generation.values[index] })),
          ...changes.map((field, index) => ({ ...field, value: generation.totals[index] })),
        ];
      } else {
        const edited = expectedState.filter(control => control.page === scenario.page && control.name === scenario.field);
        assert.equal(edited.length, 1);
        edited[0].value = savedValue;
      }
      // PDF.js does not execute the native calculations; pin their committed outputs.
      for (const change of changes) {
        const control = expectedState.filter(item => item.page === change.page)[change.control];
        assert.equal(control.name, change.name);
        control.value = change.value;
      }
      assert.deepEqual(state, expectedState, "saved XFA must preserve every control and its expected value");
    } else {
      baseline = state;
    }
    console.log(`PASS: PDF.js independently verified ${file} ${saved ? "saved controls and values" : "opening and XFA layout"}`);
  } finally {
    await task.destroy();
  }
}

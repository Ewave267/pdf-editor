// SPDX-License-Identifier: GPL-3.0-only
// Independent production XFA admission and saved-control preservation check.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const [pdfjsDirectory, manifestPath, savedPdf, savedValue, scenarioName] = process.argv.slice(2);
assert.equal(savedPdf === undefined, savedValue === undefined, "saved PDF and expected value must be supplied together");
assert.ok(scenarioName === undefined || (savedPdf && ["calculation_round_trip", "multiline_round_trip", "repeated_rows_round_trip"].includes(scenarioName)));
const record = JSON.parse(await readFile(manifestPath, "utf8"));
const { getDocument, version } = await import(pathToFileURL(resolve(pdfjsDirectory, "legacy/build/pdf.mjs")));
assert.equal(version, "6.4.299");
const expected = record.expected;
let generation;
if (scenarioName) {
  const index = Number(savedValue) - 1;
  const generations = expected[scenarioName].generations;
  assert.ok(Number.isInteger(index) && index >= 0 && index < generations.length);
  generation = generations[index];
}
let baseline;
for (const file of [resolve(dirname(manifestPath), record.file), ...(savedPdf ? [savedPdf] : [])]) {
  const saved = file === savedPdf;
  const multiline = saved && scenarioName === "multiline_round_trip";
  const repeated = saved && scenarioName === "repeated_rows_round_trip";
  const counts = multiline || repeated ? generation.reader_controls_per_page : expected.controls_per_page;
  const task = getDocument({
    data: new Uint8Array(await readFile(file)),
    enableXfa: true, isEvalSupported: false, disableFontFace: true, useSystemFonts: true,
  });
  try {
    const document = await task.promise;
    assert.equal(document.isPureXfa, expected.pure_xfa);
    assert.equal(document.numPages, counts.length);
    const state = [];
    const headings = [];
    for (let index = 0; index < document.numPages; index++) {
      const page = await document.getPage(index + 1);
      const viewport = page.getViewport({ scale: 1 });
      const dimensions = multiline || repeated ? expected.pages[0] : expected.pages[index];
      assert.equal(viewport.width, dimensions.width);
      assert.equal(viewport.height, dimensions.height);
      const controls = [], names = [];
      let pageHeadings = 0;
      function visit(node, name = "") {
        name = node.attributes?.xfaName ?? name;
        if (node.attributes?.xfaName) names.push(node.attributes.xfaName);
        if (node.attributes?.xfaName === "HeaderRow") pageHeadings++;
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
      assert.equal(controls.length, counts[index]);
      headings.push(pageHeadings);
      const values = controls.map(node => node.attributes?.value ?? node.attributes?.textContent ?? node.value ?? "");
      if (!saved) {
        assert.ok(values.every(value => value === "" || value === "0"), "template must have no filled responses");
        assert.equal(values.filter(value => value === "0").length, expected.unchecked_per_page[index]);
      }
      if (!multiline && !repeated) {
        for (const name of expected.sample_fields_per_page[index]) assert.ok(names.includes(name), `missing ${name}`);
      }
    }
    if (saved) {
      if (repeated) {
        assert.deepEqual(headings, generation.reader_headings_per_page,
          "save/reopen must preserve the expected headings on every page");
        const scenario = expected.repeated_rows_round_trip;
        const headers = baseline.slice(0, scenario.header_controls).map(control => ({ ...control }));
        const row = baseline.slice(scenario.header_controls);
        assert.deepEqual(row.map(control => control.name), scenario.row_fields);
        assert.equal(headers.at(-1).name, "number_of_issues");
        headers.at(-1).value = generation.count;
        assert.equal(generation.values.length, Number(generation.count));
        const rows = generation.values.flatMap((value, index) => row.map(control => ({ ...control,
          page: generation.reader_row_pages?.[index] ?? generation.reader_row_page,
          value: control.name === "identified_issue" ? value : control.value,
        })));
        assert.deepEqual(state, [...headers, ...rows], "saved repeated rows must preserve every control and value");
        console.log(`PASS: PDF.js verified ${file} repeated rows and values`);
        continue;
      }
      const scenario = expected.round_trip;
      const expectedState = baseline.map(control => ({ ...control }));
      let changes = scenario.committed_controls;
      if (scenarioName === "calculation_round_trip") {
        const calculation = expected[scenarioName];
        assert.equal(generation.values.length, calculation.inputs.length);
        assert.equal(generation.totals.length, changes.length);
        changes = [
          ...calculation.inputs.map((field, index) => ({ ...field, page: calculation.page, value: generation.values[index] })),
          ...changes.map((field, index) => ({ ...field, value: generation.totals[index] })),
        ];
      } else if (multiline) {
        const edited = expectedState.filter(control => control.name === expected.multiline_round_trip.field);
        assert.equal(edited.length, 1);
        for (const control of expectedState) control.page = generation.reader_page_mapping[control.page];
        edited[0].page = generation.reader_field_page;
        edited[0].value = generation.value;
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

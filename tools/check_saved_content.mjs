// SPDX-License-Identifier: GPL-3.0-only
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";
const [directory, filename, kind] = process.argv.slice(2);
const { getDocument, version, OPS } = await import(pathToFileURL(resolve(directory, "legacy/build/pdf.mjs")));
assert.equal(version, "6.4.299");
const task = getDocument({data: new Uint8Array(await readFile(filename)), isEvalSupported: false, disableFontFace: true, useSystemFonts: true});
try {
  const pdf = await task.promise;
  if (kind === "acroform") {
    const fields = await pdf.getFieldObjects();
    assert.equal(fields.get("Name")[0].value, "Edited value");
    assert.equal(fields.get("Name")[0].defaultValue, "Original value");
    assert.ok(fields.get("Name")[0].actions.get("Keystroke").some(script => script.includes("toUpperCase")));
    const page = await pdf.getPage(1);
    const annotations = await page.getAnnotations();
    assert.equal(annotations[0].fieldName, "Name");
    assert.equal(annotations[0].fieldValue, "Edited value");
    assert.equal(annotations[0].readOnly, false);
    const text = (await page.getTextContent()).items.map(item => item.str).join(" ");
    assert.ok(text.includes("Original AcroForm document"));
    assert.ok(text.includes("Added beside form"));
  } else {
    assert.equal(pdf.numPages, 3);
    for (let index=0; index<3; ++index) {
      const page = await pdf.getPage(index+1);
      const expected = [[600,780],[780,600],[420,840]][index];
      const viewport = page.getViewport({scale:1});
      assert.equal(viewport.width, expected[0]); assert.equal(viewport.height, expected[1]);
      const items = (await page.getTextContent()).items;
      const text = items.map(item => item.str).join(" ");
      assert.ok(text.includes(`PAGE ${index+1}`));
      assert.ok(text.includes("Original synthetic viewer fixture"));
      if (index===0) {
        assert.equal(text.split("Saved résumé <literal>").length-1,1);
        const added = items.find(item => item.str.includes("Saved"));
        assert.ok(Math.abs(added.transform[4]-50)<1);
        assert.ok(Math.abs(added.transform[5]-(780-150))<25);
      } else {
        const operations=await page.getOperatorList();
        assert.equal(operations.fnArray.filter(op => op === OPS.paintImageXObject || op === OPS.paintInlineImageXObject).length,1);
      }
    }
  }
  console.log(`PASS: PDF.js independently verified ${kind} Save As output`);
} finally { await task.destroy(); }

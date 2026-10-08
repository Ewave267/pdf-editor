// SPDX-License-Identifier: GPL-3.0-only
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";
const [directory, filename] = process.argv.slice(2);
const { getDocument, version, OPS } = await import(pathToFileURL(resolve(directory, "legacy/build/pdf.mjs")));
assert.equal(version, "6.4.299");
const task = getDocument({data: new Uint8Array(await readFile(filename)), isEvalSupported: false, disableFontFace: true});
try {
  const pdf = await task.promise;
  assert.equal(pdf.numPages, 1);
  const page = await pdf.getPage(1);
  const items = (await page.getTextContent()).items;
  const text = items.map(item => item.str).join(" ");
  assert.equal(text.split("APPROVED").length - 1, 1, JSON.stringify(items.filter(item => item.str.includes("APPROVED"))));
  assert.equal(text.split("Everyday editing").length - 1, 1);
  const ops = await page.getOperatorList();
  assert.equal(ops.fnArray.filter(op => op === OPS.paintImageXObject || op === OPS.paintInlineImageXObject).length, 2,
    "Only the imported image and signature should be raster images");
  assert.ok(ops.fnArray.filter(op => op === OPS.constructPath).length >= 7,
    "Drawing tools must survive as PDF vector paths");
  const transparency = ops.argsArray.filter((_, index) => ops.fnArray[index] === OPS.setGState).flat(3);
  assert.ok(transparency.some(value => typeof value === "number" && value > 0 && value < 1),
    "Highlight transparency must survive export");
  console.log("PASS: PDF.js verified vector tools, stamps, highlight alpha and image/signature persistence");
} finally { await task.destroy(); }

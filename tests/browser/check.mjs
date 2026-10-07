// SPDX-License-Identifier: GPL-3.0-only
// Use only a dedicated temporary documents directory mounted at /documents.
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { copyFile, mkdir, readFile, stat } from 'node:fs/promises';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import { chromium } from 'playwright';
const [url = 'http://127.0.0.1:8080', directory] = process.argv.slice(2);
assert.ok(directory, 'Pass the dedicated host documents directory mounted at /documents');
const root = resolve(fileURLToPath(new URL('../..', import.meta.url)));
const documents = resolve(directory);
await mkdir(documents, {recursive: true});
const run = `browser-${Date.now()}`;
const browser = await chromium.launch({headless: true});
try {
  const page = await browser.newPage({viewport: {width: 1600, height: 1100}});
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.goto(url);
  await page.waitForSelector('#noVNC_container canvas');
  await page.waitForFunction(() => document.querySelector('#noVNC_container canvas').width === 1600);
  await page.waitForFunction(() => document.querySelector('#noVNC_status').textContent.includes('Connected'));
  await page.waitForTimeout(2000);
  // The app starts fullscreen. Map synthetic PDF coordinates through its fitted
  // page and the scaled noVNC canvas instead of using old window positions.
  const clickPdf = async (x, y) => {
    const canvas = page.locator('#noVNC_container canvas');
    const rect = await canvas.boundingBox();
    const {width, height} = await canvas.evaluate(element => ({width: element.width, height: element.height}));
    const viewWidth = width - 170; // QML thumbnail sidebar.
    const viewHeight = height - 56 - 52 - 58; // Header, footer, content tools.
    const zoom = Math.min((viewWidth - 48) / 612, (viewHeight - 32) / 792);
    const left = 170 + (viewWidth - 612 * zoom) / 2;
    const top = 56 + 58 + 16;
    await page.mouse.click(rect.x + (left + x * zoom) * rect.width / width,
                          rect.y + (top + y * zoom) * rect.height / height);
    await page.waitForTimeout(500);
  };
  const toolbar = async x => {
    const canvas = page.locator('#noVNC_container canvas');
    const rect = await canvas.boundingBox();
    const width = await canvas.evaluate(element => element.width);
    await page.mouse.click(rect.x + x * rect.width / width,
                          rect.y + 28 * rect.width / width);
    await page.waitForTimeout(500);
  };
  const open = () => toolbar(203);
  const save = () => toolbar(311);
  const location = async name => {
    await page.keyboard.press('Control+l');
    await page.keyboard.type(`/documents/${name}`);
    await page.keyboard.press('Enter');
    await page.waitForTimeout(2500);
  };
  for (const kind of ['acroform', 'xfa']) {
    const source = resolve(root, `tests/pdfs/${kind === 'xfa' ? 'xfa-dynamic/empty-calculation' : 'acroform/controls'}.pdf`);
    const input = `${run}-${kind}-input.pdf`, output = `${run}-${kind}-saved.pdf`;
    await copyFile(source, resolve(documents, input));
    const hash = bytes => createHash('sha256').update(bytes).digest('hex');
    const original = hash(await readFile(resolve(documents, input)));
    await open();
    await location(input);
    await clickPdf(172, 87);
    await page.keyboard.press('Control+a');
    await page.keyboard.type('edited');
    await page.waitForTimeout(500);
    await clickPdf(82, kind === 'xfa' ? 212 : 150);
    await clickPdf(152, kind === 'xfa' ? 272 : 212);
    await clickPdf(265, kind === 'xfa' ? 337 : 275);
    await page.keyboard.press('ArrowDown');
    await page.waitForTimeout(500);
    await page.keyboard.press('Enter');
    await page.waitForTimeout(500);
    await save(); // Keyboard entry in the real file dialog.
    await location(output);
    const saved = resolve(documents, output);
    for (let attempt = 0; attempt < 40; ++attempt) {
      if (await stat(saved).catch(() => null)) break;
      await page.waitForTimeout(250);
    }
    assert.ok((await stat(saved)).size > 0);
    execFileSync(process.execPath, [resolve(root, 'tools/check_form_controls.mjs'),
      resolve(root, 'tests/reader/node_modules/pdfjs-dist'), saved, kind], {stdio: 'inherit'});
    assert.equal(hash(await readFile(resolve(documents, input))), original);
    await open();
    await location(output);
    await clickPdf(172, 87);
    // Exercise the supported noVNC clipboard panel -> native paste route.
    await page.locator('#noVNC_clipboard_text').evaluate(element => {
      element.value = 'edited';
      element.dispatchEvent(new Event('change', {bubbles: true}));
    });
    await page.waitForTimeout(1500);
    await page.keyboard.press('Control+a');
    await page.keyboard.press('Backspace');
    await page.waitForTimeout(500);
    await page.keyboard.press('Control+v');
    await page.waitForTimeout(500);
    await save();
    const reopened = `${run}-${kind}-reopened.pdf`;
    await location(reopened);
    execFileSync(process.execPath, [resolve(root, 'tools/check_form_controls.mjs'),
      resolve(root, 'tests/reader/node_modules/pdfjs-dist'), resolve(documents, reopened), kind], {stdio: 'inherit'});
    await page.screenshot({path: resolve(documents, `${run}-${kind}-reopened.png`)});
  }
  assert.deepEqual(errors, []);
  console.log('PASS: browser connection, mouse/keyboard, dialogs, AcroForm/XFA controls, mounted saves, independent reader, reopen and clipboard');
} finally {
  await browser.close();
}

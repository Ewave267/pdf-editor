# Step 4 — Forms

Click an existing field to focus it. Type in text fields, click checkboxes and
radio buttons, and open dropdowns using their arrow. Use Up/Down and Enter to
select a dropdown value. Tab and Shift+Tab move between native form fields.
Ctrl+A, Ctrl+C and Ctrl+V select, copy and paste field text; Backspace and Delete
edit it. Zoomed pages map clicks to native page coordinates.

Use **Save As** to create a new PDF containing the current form values. Saving
commits the focused field first, so exit events and XFA calculations run before
serialization. The source PDF remains unchanged. Unsaved changes trigger the
existing discard protection. Form input is paused while a save is running.

## Implementation

`FormInput` forwards Qt mouse, keyboard and committed input-method text through
`PdfDocument`'s asynchronous request queue. Native PDFium widgets handle field
state, keyboard traversal, actions and rendering inside the existing Bubblewrap
worker. QML does not access PDFium. Open pages and their form environments stay
alive between events; page and thumbnail caches refresh after input.

Save As requests a bounded snapshot of the live native document before the
separate save worker imports additions. The live renderer never receives those
addition objects, so repeated saves do not duplicate them. Form and addition
revisions are tracked separately; a destination write failure retains both.
See [ADR 0005](adr/0005-native-form-events.md) and [SAVING](SAVING.md).

## Reproducible validation

Original GPL-3.0-only fixtures cover all four control types:

- `tests/pdfs/acroform/controls.pdf`
- `tests/pdfs/xfa-dynamic/controls.pdf`, generated from `controls.xdp`

With the combined viewer/XFA build and pinned PDF.js installed:

```sh
cmake --build build-viewer --parallel 4
ctest --test-dir build-viewer --output-on-failure
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  ./build-viewer/viewer-tests formEventsAndPersistence formMouseAndKeyboard
```

The native tests edit text, trigger XFA exit/calculation events, persist both
checked and unchecked states, switch radio choices, choose a dropdown value,
save repeatedly, retain edits after a write failure, and close/reopen exact
values. PDF.js independently checks every control value and radio exclusivity.
The QML tests click fields, type, copy/paste, delete selections, traverse forward
and backward, select choices, wait for redraws and save. Existing additions,
viewer lifecycle and XFA round-trip regressions remain in the full suite.

## Current limits

Coverage is synthetic single-page AcroForm and dynamic XFA, not a claim of
arbitrary production-form compatibility. Foreground/static XFA, changes that
add/remove/reflow pages, timer-driven forms, pointer-drag text selection, and
full input-method composition/caret positioning remain unvalidated or unsupported.
Native Tab navigation works within the tested page; automatic scrolling across
pages has not been implemented. Committed Unicode input is forwarded as UTF-16,
but comprehensive complex-script input has not been validated.

The pinned PDFium build retains AcroForm JavaScript actions in saved output,
but its native keystroke path does not apply the fixture's `event.change`
uppercase conversion. The local source's `CPDFSDK_Widget::OnAAction` returns
false after executing the AcroForm action, causing `OnBeforeKeyStroke` to skip
applying changed action data. A dependency fix and regression gate are needed
before claiming full AcroForm script behavior. Required XFA calculations and
exit scripts are verified independently and continue to pass.

Dynamic full XFA form edits can be saved without added content. Saving additions
on full XFA still returns a controlled error and retains the edits/additions.
There is no autosave or recovery of in-memory form edits after a renderer crash.
Step 7 retains application-owned additions, clears pending operations, and
explicitly reports native form loss; see [SAFETY](SAFETY.md).

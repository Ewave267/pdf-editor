# Step 4 — Forms

Click an existing field to focus it. Type in text fields, click checkboxes and
radio buttons, and open dropdowns using their arrow. Use Up/Down and Enter to
select a dropdown value. Tab and Shift+Tab move between native form fields.
Ctrl+A, Ctrl+C, Ctrl+X and Ctrl+V select, copy, cut and paste field text; Backspace and Delete
edit it. Zoomed pages map clicks to native page coordinates.

Use **Save a copy** to create a new PDF containing the current form values. Saving
commits the focused field first, so exit events and XFA calculations run before
serialization. The source PDF remains unchanged. Unsaved changes trigger the
existing discard protection. Form input is paused while a save is running.

## Phase 3 — form helpers

The form toolbar appears for documents with native fields. **Highlight fields**
toggles PDFium's field highlight without changing saved content. AcroForm
widgets also have outlines: required fields have an asterisk and amber border,
failed checks use red, and read-only fields are dimmed.

For supported AcroForms:

- **Fields** lists fields with their page, label, required status and read-only
  status. Select an editable field to scroll it into view and focus it.
- **Previous**, **Next**, Tab and Shift+Tab navigate editable fields
  across pages, following page/annotation order and skipping read-only widgets.
  Native focus actions and validation run through PDFium. Focus scrolling also
  moves far-down fields into view and returns keyboard input to that page.
- **Check form** commits the current edit and lists missing required values or
  invalid recognized dates. Click an issue to focus its field. Document alerts
  from native validation are shown as plain text. These checks supplement the
  document's scripts; they do not certify a form is ready for submission.
- **Form → Reset selected field…** restores the selected field's opening value;
  a radio button restores its group's opening choice. **Reset form** restores editable fields
  to their opening values. Both ask for confirmation and keep added text,
  drawings, images and signatures. These are the values in the opened PDF,
  rather than necessarily its `/DV` defaults. Resets use native events, so
  document calculations and access/validation rules still apply. If a script
  rejects a reset, its error and any retained changes remain visible/unsaved.
- **Choose value** offers a scrollable list of labels for the selected combo/list
  field. Selection uses PDFium's native choice API. Native controls remain
  available, including their keyboard and multi-selection behavior.
- **Date…** appears for text fields with a recognized `AFDate_FormatEx` format.
  Year/month/day controls account for month length and leap years, and insert
  the document's format through native typing/commit. Recognized formats are
  `yyyy-mm-dd`, `mm/dd/yyyy`, `dd/mm/yyyy`, `mm/dd/yy` and `dd/mm/yy`. The helper
  offers years 1900–2100; other dates/formats can be entered in the native field.

**Sign…** guides drawing a signature or importing a signature image, then
placing/resizing it with the added-content tools. This is a visual addition;
it does not cryptographically sign or complete a native digital signature
field. Dynamic full XFA additions still cannot be saved; the dialog explains
this limit. See [ADDED-CONTENT](ADDED-CONTENT.md) and [SAVING](SAVING.md).

### XFA behavior and helper bounds

Full XFA keeps its native widgets, conditional access rules, calculations and
Tab behavior. **Reset form** recreates its native environment from the immutable
opening snapshot; it does not rewrite XFA XML and keeps application additions.
Native caret callbacks scroll text focus into view. When PDFium reports no next
widget on a page, navigation tries up to 32 adjacent pages, preserving native
field selection. The synthetic two-page fixture verifies forward/backward Tab,
scrolling, keyboard editing, reset and saved values. Layouts that silently wrap
Tab within their current page cannot reliably be detected through the public
API; arbitrary XFA cross-page traversal is not claimed.

PDFium does not expose full XFA field enumeration, required flags, choice options
or individual reset through the public annotation API. The corresponding helper
buttons stay disabled; use the document's native controls and reset buttons.
Foreground/static XFA helpers are not validated.

AcroForm helpers enumerate at most 512 visible widgets with a 4 MiB metadata
budget. Option lists show at most 100 labels, each bounded to 512 characters;
larger choices use the native control. Text values beyond the helper's 4,095
UTF-16-character read bound are reported as too large to check or restore,
rather than silently reset to empty. Incomplete metadata disables full helper
navigation, checking and AcroForm reset; native editing/highlighting remains
available. Labels/messages are rendered as plain text in the host UI.

## Implementation

`FormInput` forwards Qt mouse, keyboard and committed input-method text through
`PdfDocument`'s asynchronous request queue. Native PDFium widgets handle field
state, keyboard traversal, actions and rendering inside the existing Bubblewrap
worker. QML does not access PDFium. Open pages and their form environments stay
alive between events; page and thumbnail caches refresh after input.

Save a copy requests a bounded snapshot of the live native document before the
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
Phase 3 adds original fixtures `acroform/phase3.pdf` and
`xfa-dynamic/phase3-navigation.pdf`, regenerated with
`python3 tools/generate_phase3_fixture.py`. Tests cover required/readonly fields,
invalid dates, leap-day entry, native choice selection, individual/group reset,
reset cancellation, preserved additions, native cut/paste, and cross-page focus
and typing in both form types. PDF.js independently verifies repeated saves,
form flags/values, preserved digital signature field structure and visual paths.
XFA calculations continue to work after snapshot reset. The
`phase3-lock-on-focus.pdf` regression verifies that entry scripts can make a
field read-only before a helper edits it; access is checked after native focus. The actual QML form
screen is visually inspected.

The QML tests click fields, type, copy/paste, delete selections, traverse forward
and backward, select choices, wait for redraws and save. Existing additions,
viewer lifecycle and XFA round-trip regressions remain in the full suite.

## Current limits

Coverage is synthetic single-page AcroForm and dynamic XFA, not a claim of
arbitrary production-form compatibility. Foreground/static XFA, changes that
add/remove/reflow pages, timer-driven forms, pointer-drag text selection, and
full input-method composition/caret positioning remain unvalidated or unsupported.
AcroForm helpers support cross-page navigation and scrolling; XFA navigation
limits are described above. Committed Unicode input is forwarded as UTF-16,
but comprehensive complex-script input has not been validated.

The pinned dependency patch now reports successfully executed AcroForm actions
back to the form filler. The keystroke conversion regression confirms that
`event.change` uppercase conversion applies to mixed-case input. Native package
validation checks conversion and visible values after flattening. Required XFA
calculations and exit scripts continue to have independent-reader checks.

Dynamic full XFA form edits can be saved without added content. Saving additions
on full XFA still returns a controlled error and retains the edits/additions.
Idle recovery checkpoints retain the last successfully captured native form state
and editable additions; see [DESKTOP-EXPERIENCE](DESKTOP-EXPERIENCE.md). Edits since
the last checkpoint can still be lost after a renderer crash.
Step 7 retains application-owned additions, clears pending operations, and
explicitly reports native form loss; see [SAFETY](SAFETY.md).

### Form-authored conditional controls

The locally reported maternity/parental leave XFA form enables section 6's
Quebec “De base 55 %” checkbox only when “De base 70 %” is selected in the same
Parentales or Adoption row. Selecting “Résidents du Québec seulement” alone
leaves these checkboxes read-only; choosing “Spéciales 75 %” clears and locks
them. These are document script rules, not overrides supplied by the editor.
Both dependent checkboxes were verified with 70 % selected, including saved
values and reopening. The reported document is not stored in the repository.

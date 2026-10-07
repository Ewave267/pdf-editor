# MVP Progress

Last updated: 2026-10-06

## Current state

Step 1 is implemented and verified locally. The application is a C++20
command-line foundation executable; PDF rendering, forms, and editing are not
implemented. The existing GOAL, MVP, and ROADMAP documents were read before
starting implementation.

## Step 1 — Repository Foundation

- [x] Add README with scope, build instructions, and project layout.
- [x] Preserve `docs/MVP.md` and `docs/ROADMAP.md`.
- [x] Create `docs/adr/` with guidance for future decisions.
- [x] Create `src/`, `qml/`, `tests/`, and `tests/pdfs/`.
- [x] Add GPL 3.0 license (`GPL-3.0-only`), as requested by the user.
- [x] Add `.gitignore` for build products and local configuration.
- [x] Add basic CMake project and C++20 executable.
- [x] Add `.clang-format` and `.editorconfig`.
- [x] Add minimal GitHub Actions configure/build/test workflow.
- [x] Verify local configuration, compilation, smoke test, and formatting.

No external libraries were introduced. Empty future source and PDF corpus
directories have `.gitkeep` files so they are preserved in version control.

## Validation

Passed locally with GNU C++ 15.2.1:

```sh
cmake -S . -B build -DCMAKE_CXX_COMPILER=/usr/bin/c++ -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
clang-format --dry-run --Werror src/app/main.cpp
```

CTest: 1/1 smoke checks passed. The explicit compiler path bypasses this
workspace's default ccache wrapper, which attempted to write to a read-only
cache. Normal build instructions remain in README.

CI is configured but has not been run remotely. This workspace does not have
usable Git metadata; no commit or push was performed.

## Remaining MVP steps

| Step | Status | Next milestone |
| --- | --- | --- |
| 2 — Prove XFA First | Not started | PDFium XFA/JavaScript open, interact, save, and reopen probe |
| 3 — Minimal Viewer | Not started | Qt/QML shell behind a C++ PDF abstraction |
| 4 — Forms | Not started | AcroForm and representative XFA field interaction |
| 5 — Added Content | Not started | Text, signature, and image object model |
| 6 — Save | Not started | Save As and verify preserved changes on reopen |
| 7 — Safety | Not started | Untrusted PDF and JavaScript isolation testing |
| 8 — MVP Release | Not started | Package the verified workflow for one desktop platform |

## Next work

Start step 2 by selecting a reproducible PDFium build with XFA and V8 enabled,
recording its integration and isolation decisions in `docs/adr/`, and acquiring
a redistributable XFA fixture with expected field values. The probe must verify
the complete interaction/save/reopen workflow before UI work begins.

Update this file as milestones land, including the checks actually run and
any remaining limitations. Do not mark PDF compatibility complete based only
on the foundation smoke check.

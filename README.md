# PDF Form Editor

An open-source desktop application for completing PDF forms and adding text,
signatures, and annotations, entirely offline.

The project is in early development. The main executable is a repository
foundation smoke check. A separate isolated PDFium probe now opens, renders,
and interacts with a synthetic dynamic XFA form and executes its JavaScript.
Step 2 verifies exact values through repeated saves and reopening in PDFium
and an independent PDF.js reader. The editor UI has not been started.

The planned stack is C++20, Qt Quick / QML, PDFium, and CMake. Qt and PDFium are
not dependencies of the default foundation build. PDFium is an explicit,
source-pinned, locally patched opt-in dependency for the Linux x64 XFA probe.

## Build and verify

Requirements: CMake 3.20 or newer, a C++20 compiler, and a native build tool
(such as Make or Ninja).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/pdf-form-editor
```

For a multi-configuration generator, add `--config Debug` to the build command
and `-C Debug` to the test command. The executable will be under `build/Debug/`.

## Development

C++ formatting uses the repository's `.clang-format` configuration:

```sh
clang-format -i src/app/main.cpp
clang-format --dry-run --Werror src/app/main.cpp
```

GitHub Actions configures, builds, and runs the smoke check on Linux.

## XFA integration probe

See [the probe instructions and persistence fixes](docs/XFA-PROBE.md) for the
source build, sandbox requirements, and validation commands. The XFA CI workflow
runs regression, isolation, and strict independent-reader checks on pushes and
pull requests. Preserve the generated package's upstream and dependency license
notices when distributing it. Current coverage is synthetic, one-page dynamic
XFA; broader form compatibility belongs to the later MVP steps.

## Project layout

- `src/app/`: application entry point and lifecycle.
- `src/pdf/`: future PDFium abstraction.
- `src/document/`: future document and added-content models.
- `src/ui/` and `qml/`: future desktop interface.
- `tests/pdfs/`: compatibility corpus, organized by document type.
- `docs/adr/`: architecture decision records.

Read [GOAL](docs/GOAL.md), [MVP](docs/MVP.md), and
[ROADMAP](docs/ROADMAP.md) for the product and implementation scope.
[PROGRESS](PROGRESS.md) tracks completed work, validation, and next steps.

## License

Copyright (c) 2026 PDF Form Editor contributors.

Licensed under the GNU General Public License version 3 only
(`GPL-3.0-only`); see [LICENSE](LICENSE).

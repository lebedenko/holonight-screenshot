# Native code quality tasks

Status: Verified

## Inception

Provide sibling-style local quality tasks using the native CMake presets.

### Acceptance criteria

- Formatting checks detect C and C++ drift without editing files.
- Static analysis, QML lint, licensing, and development tests run through one gate.
- Standalone analysis and QML lint support both presets.

## Construction

- [x] Add [Task entry points](../../Taskfile.yml) and [C/C++ source checks](../../scripts/quality.py).
- [x] Resolve QML types for [CMake lint](../../CMakeLists.txt) and document the workflow in the [README](../../README.md).

## Verification and handoff

- `task format-check`, `task license-check`, and `task qml-lint` passed.
- `task tidy PRESET=release` analyzed 10 translation units, and `task qml-lint PRESET=release` passed.
- In a disposable checkout, the formatter check detected drift without editing the file, and `task format` corrected it. The analyzer rejected a null dereference, and QML lint rejected an invalid binding.
- A clean development build passed formatting, licensing, analysis of all 15 translation units, and QML lint. The sandbox blocked the session test. The unrestricted `task test` passed all six tests with the same build.
- The launcher mapped and the region overlay opened in isolated headless Sway. Escape cancelled the overlay with exit status 2 and created no output file.
- Source files in the disposable checkout matched the workspace after the checks. `git diff --check` passed.
- The complete unrestricted `task check` passed, including all six CTest tests.

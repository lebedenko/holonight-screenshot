# Screenshot capture and annotation editor

Status: Verified

## Scope and acceptance

- Launcher persists cursor, target, preset delay and editor preference. Capture copies and returns, or opens the editor directly. Legacy non-preset delays restore as None.
- CLI routes to clipboard, atomic PNG output, or the editor. Capture and copy errors exit 1, and picker and region cancellation exit 2.
- Editor supports ordered marks, selection, properties, history, bounded crop, guides, fit and zoom. Copy and Save share a full-resolution compositor and preserve the capture color profile.
- Keep the installed capture API and portal overlay intact. Update launcher, session, editor and PNG verification alongside the [capture design](../capture.md).

## Construction

- [x] Route successful captures through the [workflow](../../apps/screenshot/workflow.cpp) and simplify the [launcher](../../apps/screenshot/Launcher.qml).
- [x] Implement the [app-local document](../../apps/screenshot/editor_document.cpp) and [Qt Quick editor](../../apps/screenshot/Editor.qml). The editor has its own window so it can close independently and the launcher can restore its geometry.
- [x] Update the [README](../../README.md), [capture design](../capture.md), and regression coverage in [launcher](../../tests/launcher_test.cpp), [session](../../tests/session_test.cpp), [CLI](../../tests/cli.cmake), and [editor](../../tests/editor_test.cpp) tests.

## Verification

- The focused launcher, CLI, session, editor and PNG tests passed. Coverage includes legacy delay and checkbox restoration, keyboard capture, both clipboard process outcomes, direct save, editor save gating, Fit and 100% pointer alignment, crop history and bounds, draw order, and matching Display P3 clipboard and saved PNGs.
- A live, isolated Sway Wayland session confirmed that a 1280 × 800 clipboard PNG could be pasted after CLI exit. Region confirmation saved a 400 × 250 crop, and Window confirmation saved a 1276 × 771 fixture window.
- On the same session, closing the CLI editor exited without creating the supplied destination. Closing the launcher editor restored the original launcher geometry and kept the editor preference. Compared the rendered toolbar, tool rail, canvas, properties panel and footer against the mockup with the active Qt palette.
- The final `task test` run passed all eight suites. `task qml-lint`, `task format-check`, and `git diff --check` also passed.

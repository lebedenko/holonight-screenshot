# Capture parity for the launcher and CLI

Status: Verified

## Inception

Region, Window, and Screen share capture controls and result handling. The launcher adds direct saves and custom delay. Active Window remains CLI-only. Preserve the installed overlay and portal behavior. See [capture design](../capture.md).

### Acceptance criteria

- Both entry points use the same editable region selector, preview, and export.
- Direct saves are atomic and skip the preview. Canceling capture leaves the destination intact.
- The exact custom delay persists, and explicit CLI options are independent.
- The preview supports Pen, Arrow, Rectangle, Undo, and full-resolution color-managed export.
- CLI routing, exit statuses, and headless direct saves match the approved plan.

## Construction

- [x] Unify capture routing and add launcher controls in [Workflow](../../apps/screenshot/workflow.cpp) and [Launcher.qml](../../apps/screenshot/Launcher.qml).
- [x] Extract [Annotations](../../src/capture/annotations.cpp) and reuse it in RegionSelection and the preview editor.
- [x] Extend CLI, session, launcher, and region tests and update the [README](../../README.md) and [capture design](../capture.md).

## Verification and handoff

- Focused CLI, launcher, region, and session tests passed. The session tests exercise Screen preview routing, deferred fixed-path saves, exact custom delay reload, and direct-save dialog cancellation with an existing destination.
- `task qml-lint`, `task format-check`, and `git diff --check` passed. The CLI also built against a temporary installation using `SCREENSHOT_USE_INSTALLED_CAPTURE=ON`.
- On isolated headless Sway with outputs at 1× and 1.5×, both entry points dragged across the seam, resized the crop, moved it, and confirmed it with Enter. The dimension badge and PNGs measured 180 × 150 pixels.
- The resized CLI editor exported the drawn line into the expected pixel range. Clipboard and saved PNGs matched byte for byte. Esc exited with status 2 and preserved the existing PNG.
- The launcher restored a 1234 ms delay after restarting and hid before region selection. Canceling the file chooser returned to the capture controls or preview as appropriate. Preview Save and Save Directly produced PNGs matching the clipboard, and direct save returned to the controls. The CLI captured the focused launcher window with Active Window. The launcher offered Region, Window, and Screen.
- The final `task test` passed all seven suites. Session tests ran outside the sandbox because the sandbox blocks Wayland socket operations.
- An isolated D-Bus monitor observed five notification calls for the two clipboard copies and three saves. The canceled capture and dismissed dialogs sent none.

## Shortcut warning follow-up

Bind every platform key sequence for Copy, Save, and Undo. The launcher should
start without the Qt Quick warnings about binding only one sequence.

- The focused launcher tests and QML lint passed. The rebuilt launcher ran offscreen without the shortcut warnings.

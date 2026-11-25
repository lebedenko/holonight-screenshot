# Keep the screenshot launcher height steady

Status: Verified

## Inception

Move launcher-wide diagnostics into a warning popover on the Include mouse cursor row. See the [capture design](../capture.md).

### Acceptance criteria

- [x] Short and multiline diagnostics leave the window size, minimum height, and Capture position steady, including with the popover open.
- [x] The borderless warning button has the accessible name and tooltip “Show capture details” and a visible keyboard focus indicator. The popover wraps and scrolls and dismisses on Escape, outside click, diagnostic clearing, or launcher closure.
- [x] The glyph follows the live HoloNight warning token and falls back to system window text when the optional integration is unavailable. Target availability and preview messages retain their existing presentation.

## Construction

- [x] Place the warning button in the checkbox row and load the semantic palette optionally in [Launcher.qml](../../apps/screenshot/Launcher.qml).
- [x] Add regression coverage at minimum width in [launcher_test.cpp](../../tests/launcher_test.cpp).

## Verification and handoff

- `task test` passed all seven test targets outside the sandbox. The sandboxed run could not create the Wayland session test’s socket pairs. The launcher test also passed after the final fallback assertion update.
- `task qml-lint`, `task format-check`, and `git diff --check` passed.
- A standalone harness with Qt’s standard QML modules and no Holonight.Core passed the diagnostic regression and confirmed the glyph used the window palette’s WindowText role.
- A launcher test window on Hyprland stayed visible with a diagnostic while its isolated appearance file switched dark → light → dark. The glyph changed from `#f2c46b` to `#c38a1c` and back to `#f2c46b`.

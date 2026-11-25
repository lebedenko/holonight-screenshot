# Keep the launcher out of Hyprland captures

Status: Verified

## Inception

Hyprland can render the launcher's closing animation after the Wayland hide sync. A zero-delay screenshot can include that frame. Preserve the selected delay and the existing hide path for other compositors. See the [capture design](../capture.md).

### Acceptance criteria

- [x] Zero-delay desktop capture on Hyprland excludes the launcher with close animations enabled.
- [x] The existing hide sync remains in place, and headless capture and other compositors retain their behavior.

## Construction

- [x] In the [launcher workflow](../../apps/screenshot/workflow.cpp), disable animation for the app’s PID before hiding it for desktop capture on Hyprland. Wait for the compositor’s reply, with a two-second deadline and legacy command fallbacks.
- [x] Verify the captured pixels and run the native tests.

## Verification and handoff

- Reproduced on Hyprland with the previous build. A real zero-delay Region grab contained the fading launcher at its old location.
- Repeated with the updated build for both Region and Screen using the real `grim`. Hyprland accepted the PID-scoped dispatcher in both runs. The corresponding pixels in the actual capture showed the desktop with no launcher or border.
- `task qml-lint`, `task format-check`, and `git diff --check` passed.
- `task test` passed all seven suites with host socket access.
- Legacy Hyprland command fallbacks still need verification on an older compositor.

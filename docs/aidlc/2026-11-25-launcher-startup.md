# Stabilize launcher startup geometry

Status: Verified

## Inception

Show the launcher at its normal size while capture discovery runs. Preserve the capture API and CLI behavior. See the [capture design](../capture.md).

### Acceptance criteria

- [x] The first frame has stable geometry, disabled capture targets and Capture, and usable delay and cursor controls.
- [x] Discovery selects the first supported target in Region, Screen, Window order. Unavailable targets expose their reasons on hover and to assistive technology.
- [x] Helper discovery leaves the owning event loop responsive, publishes once both inputs are ready, and reaps the probe on shutdown.
- [x] A cold launch on Hyprland has a stable first frame through discovery, focus changes, and workspace switches.

## Construction

- [x] Publish availability and selection together in [Workflow](../../apps/screenshot/workflow.cpp) and keep explanations outside the [launcher layout](../../apps/screenshot/Launcher.qml).
- [x] Move helper discovery to a dedicated Qt thread in [CaptureSession](../../src/capture/capture_session.cpp).
- [x] Extend [launcher](../../tests/launcher_test.cpp) and [session](../../tests/session_test.cpp) coverage and update the [capture design](../capture.md).

## Verification and handoff

- `task test` passed all seven suites with host socket access. The sandbox blocks the Wayland socket pairs used by the session fixture.
- `task qml-lint` passed with zero warnings.
- `git diff --check` passed.

- `task format-check` passed.
- Cold-launched on Hyprland with a 1.6-second wrapper around the real `grim -h` so the pending frame could be inspected before any focus changes. The first mapped frame showed Hyprland’s fade-in. After the fade, the pending frame showed the full launcher with no transparent strip, disabled targets and Capture, and available delay and cursor controls. Once discovery completed, Region was selected and Capture was enabled.
- The Wayland trace showed one `set_window_geometry(0, 0, 460, 224)` before the first buffer attach and no further geometry or minimum-size requests after mapping. Hyprland reported 460 × 224 when the launcher mapped, during and after discovery, after focus moved away and back, and after switching away and returning to the workspace. The captured frames showed no strip following those transitions.

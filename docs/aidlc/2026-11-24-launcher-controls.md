# Redesign the screenshot launcher controls

Status: Verified

## Inception

Match the [showcase launcher](../mockups/showcase.png) using live Qt palette colors and the native title bar. Scope covers the target selector, delay presets, and primary capture button in the [capture workflow](../capture.md).

### Acceptance criteria

- Equal-width target segments show distinct icons and keep unavailable targets disabled with their reasons directly beneath them.
- The delay label and four presets share a rounded bar. None is the default and the selection persists when returning to the launcher.
- Capture is a full-width camera button with a static label and forwards the selected delay and cursor flag.
- Buttons support keyboard activation and retain their selections when the palette changes live.
- On Wayland, the launcher hides before delayed acquisition.

## Construction

- [x] Style the three controls in [Launcher.qml](../../apps/screenshot/Launcher.qml).
- [x] Add an [offscreen test of the production launcher](../../tests/launcher_test.cpp).

## Verification and handoff

- `task test` passed all seven CTest targets with Unix socket access. The sandboxed run could not create the Wayland session test connections.
- `task qml-lint` and `git diff --check` passed.
- On labwc, switched an isolated HoloNight appearance file from dark to light while the launcher stayed open. The bars, selections, text, camera and target icons recolored live. The focused Region segment had a visible contrasting outline in both themes.
- Selected the 5 s preset and activated Capture with the keyboard. The launcher disappeared during the delay, and the frozen Region overlay appeared afterward without the test launcher in the captured frame. The Wayland trace showed destruction of the launcher toplevel before the overlay was created.
- Live compositor coverage was labwc. Sway and Hyprland were covered by the session test doubles. Live checks on those compositors remain outstanding.

## Follow-up

See the separate [countdown OSD investigation](2026-11-24-countdown-osd.md).

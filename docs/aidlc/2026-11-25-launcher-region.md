# Launcher Region selection UI

Status: Verified

## Inception

Implement the Launcher Region panel from the [showcase](../mockups/showcase.png) on the frozen desktop. Scope is the SelectionOnly overlay.

### Acceptance criteria

- Draw, resize from eight handles, move, and replace crops in shared desktop coordinates within the virtual desktop bounds.
- Show a lavender outline and a single live PNG pixel-size badge on the output with greatest overlap, preferring the primary on ties.
- Capture via the action strip or Enter after release and final layout validation. Cancel via the strip or Esc.
- Anchor a single Capture and Cancel strip to the badge output after a valid release. Place it below the visible crop or inside near the bottom when space is limited. Keep both controls on-screen and lift the badge when they overlap and there is room.
- Hide the strip during canvas gestures. During final layout validation keep it visible with Capture disabled and Cancel available.
- Preserve CLI Region and portal area selection behavior.

## Construction

- [x] Add editable gestures and share crop mapping with the badge in [RegionSelection](../../src/capture/region_selection.cpp).
- [x] Render the selection in [RegionOverlay](../../src/capture/region_overlay.cpp) and add the Launcher action strip and badge in [RegionPreview](../../src/capture/RegionPreview.qml).
- [x] Extend the [region tests](../../tests/region_test.cpp) and update the [capture design](../capture.md).

- [x] Anchor and style the Launcher Region strip and badge, retaining canvas pointer grabs.
- [x] Update the capture design and verify the placement and interactions.

## Verification and handoff

### Initial Launcher Region implementation

- `task test -- -R '^region$'` passed. Coverage includes corner and edge resize, clamped movement, replacement, gap-only and empty crops, confirmation gating, and fractional-scale crop dimensions.
- `task qml-lint`, `task test` (7 suites), `task format-check`, and `git diff --check` passed.
- Isolated Sway with one 1× output verified the lavender outline and eight handles, resize and move, the badge above the selection and inside it near the top edge, and the Capture button. The preview exported a 660 × 406 PNG matching the badge.
- Isolated Sway with 1× and 1.5× outputs verified cross-output drawing and moving, a single badge on the output with greatest overlap, the primary-output tie-break, corner, edge, and move cursors, Enter and keypad Enter, and Cancel and Esc. The mixed-scale preview exported an 840 × 405 PNG matching the badge.
- CLI Region saved a 360 × 195 PNG with Enter. A temporary harness linked to the current capture library exercised the portal's Share-mode overlay, including dimming a visible window and sharing a 360 × 195 PNG.
- The portal smoke check covered the overlay and crop result. D-Bus broker integration and appearance on a physical display were outside the headless check.

### Anchored controls

- `task test -- -R '^region$'`, `task qml-lint`, `task format-check`, and `git diff --check` passed. All seven suites passed under `task test` with local Wayland socket access. The sandboxed run blocked sockets used by the session tests.
- In isolated Sway at 1×, the strip appeared after release and hid while dragging across its former location. Esc canceled the drag. Resizing moved the strip from the inside fallback to below the crop. At the lower-right edge, a 58 × 12 crop kept both buttons visible and lifted the badge above the strip. Clicking Capture opened the shared preview.
- On 1× and 1.5× Sway outputs, the strip and badge appeared once on the primary output for an overlap tie and moved together to the secondary output when it gained the larger overlap. Keypad Enter opened the preview, and the exported clipboard PNG measured 450 × 300, matching the badge.
- With a delayed layout helper, Return started confirmation and the strip kept Capture visible and disabled. Clicking Cancel returned to the launcher during the check.
- Compared the headless screenshots with the showcase for badge sizing, panel spacing, inset keycaps, and contrast. Appearance on a physical display was outside the headless check.

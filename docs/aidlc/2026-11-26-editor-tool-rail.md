# Match the editor tool rail to the showcase

Status: Verified

## Inception

Style the editor's left rail to match the [showcase](../mockups/showcase.png) using the active Qt palette. Scope includes the rail and its bundled Crop icon. The top toolbar, footer, shortcuts, and document APIs keep their existing behavior.

### Acceptance criteria

- [x] Select, Rectangle, Ellipse, Arrow, Text, Number, Blur, Pen, and Crop appear in order above a divider and a shared Undo/Redo group.
- [x] The document determines the active tool highlight, including during Crop and after Apply or Cancel.
- [x] Each button has an accessible name, tooltip, and visible focus and disabled styling.
- [x] Buttons use a consistent width and fit at the 760 × 520 minimum editor size. Annotation tool buttons are 44 px tall when space allows.
- [x] Session tests cover tool mapping, repeat clicks, Crop transitions and icon loading, history availability, and minimum-size layout.

## Construction

- [x] Update the [editor rail](../../apps/screenshot/Editor.qml) and bundle the [Crop icon](../../apps/screenshot/icons/crop.svg) in the [QML resources](../../apps/screenshot/launcher.qrc).
- [x] Extend the [session test](../../tests/session_test.cpp) to exercise the rail through the editor window.

## Verification and handoff

- The focused `editorSavesOnlyOnRequest` session check passed with local Wayland socket access.
- `task qml-lint`, `task format-check`, and `git diff --check` passed.
- Compared rendered Qt Quick rails against the showcase at 1080 × 700 and 760 × 520 with the default light palette and a dark palette with a purple highlight. In all four renders, the rail filled the editor body, icons remained centered, and the history group fit beneath the divider. The minimum-size renders showed shorter buttons and retained the same width.
- `task test` passed all 8 CTest targets.

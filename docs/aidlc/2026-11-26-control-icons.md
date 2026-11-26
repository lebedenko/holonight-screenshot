# Bundle Screenshot control icons

Status: Verified

## Inception

Bundle SVGs for the five launcher controls and thirteen editor controls. Preserve
launcher sizing and palette roles, and keep editor labels, shortcuts and actions.
The application icon, native dialogs and toolkit indicators are outside this change.

### Acceptance criteria

- [x] All eighteen icons validate and load with icon-theme search paths cleared.
- [x] Light and dark rendering at 1× and 2× preserves state contrast and layout.
- [x] QML lint and the Screenshot suite pass. A staged install contains the
  desktop entry and the matching hicolor icon.

## Construction

- [x] Bundle the [control artwork](../../apps/screenshot/icons) in
  [launcher.qrc](../../apps/screenshot/launcher.qrc). The
  [launcher](../../apps/screenshot/Launcher.qml) uses Qt Quick Controls' tinted
  image item in its existing caption layout. The
  [editor](../../apps/screenshot/Editor.qml) uses the active style's ToolButton
  icon rendering.
- [x] Cover resource loading in the [launcher test](../../tests/launcher_test.cpp)
  and [editor session test](../../tests/session_test.cpp).

### Artwork provenance

The source repositories' REUSE metadata attributes the reused files to
2026 Andrii L <lebeden@gmail.com> under GPL-3.0-or-later. Screenshot's
[REUSE metadata](../../REUSE.toml) retains that attribution.

| Bundled artwork | Source |
| --- | --- |
| Warning and Close | Copied byte for byte from holonight-icons `icons/status/24/dialog-warning.svg` and `icons/actions/24/window-close.svg`. |
| Region | Adapted the four corners from holonight-shell `assets/bar-icons/window-fullscreen-symbolic.svg`. |
| Pen | Adapted the pencil outline from holonight-qt `qml/controls/assets/edit.svg`. |
| Plus and Minus | Adapted holonight-ai `assets/icons/plus.svg`. Minus uses its horizontal stroke. |
| Window, Screen and Capture | Converted the existing Screenshot launcher paths to a 24-unit canvas. |
| Select, Rectangle, Ellipse, Arrow, Text, Number, Blur, Undo and Redo | Drawn as vector paths for Screenshot. |

## Verification and handoff

- The holonight-icons `validate_svg` validator passed all eighteen resources
  with `native=24`. Every control SVG is listed in the Qt resource manifest.
- `cmake --build --preset dev --target qml-lint`, the changed C++ files' format
  check, and `reuse --no-multiprocessing lint` passed.
- Reviewed rendered launcher and editor screenshots offscreen with Qt Quick
  Controls Basic, HoloNight light and dark palette values, and 1× and 2× scale
  factors. Selected Region and Select remained distinct, the disabled Window
  and Redo icons stayed readable, and the launcher warning used the warning
  color in both palettes. The launcher captions and both editor toolbars had
  room for the icons without clipping or overlap.
- A staged `cmake --install` passed. `desktop-file-validate` accepted the staged
  desktop entry and `cmp` confirmed that the hicolor SVG named by its `Icon=`
  field matched the bundled application icon byte for byte.
- `task test` passed all eight CTest targets, including the launcher and editor
  session checks with icon-theme and fallback search paths cleared.

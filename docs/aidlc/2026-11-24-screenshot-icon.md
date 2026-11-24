# HoloNight Screenshot application icon and desktop launcher

Status: Verified

## Inception

Use the supplied HoloNight Screenshot artwork for the Qt application and the installed desktop launcher. Preserve the desktop entry ID and its no-argument launcher command.

### Acceptance criteria

- [x] The application sets its window icon from the bundled, unmodified SVG, including when run from the build directory.
- [x] A staged install contains the executable, the valid desktop entry and the byte-identical SVG in the hicolor scalable application icon directory.
- [x] Launching the staged desktop entry on Wayland opens the graphical launcher, and desktop icon lookup resolves the supplied SVG.

## Construction

- [x] Copy and bundle the [SVG](../../apps/screenshot/holonight-screenshot.svg) in [launcher.qrc](../../apps/screenshot/launcher.qrc) and set the window icon in [main.cpp](../../apps/screenshot/main.cpp).
- [x] Update the [desktop entry](../../apps/screenshot/org.holonight.Screenshot.desktop) and install the artwork through [CMake](../../apps/screenshot/CMakeLists.txt).

## Verification and handoff

- `task test` passed all six tests outside the sandbox. The sandboxed run passed five tests and failed at local socket setup in the session test.
- A temporary offscreen Qt check used the dev build's generated resource collection. The embedded SVG matched the supplied artwork byte for byte, and the application window icon rendered a 64-pixel pixmap.
- `desktop-file-validate` passed for both the source and staged desktop entries. `cmake --install build/dev --prefix <temporary-prefix>` installed the executable, desktop entry and hicolor scalable icon. `cmp` confirmed the installed SVG matched the supplied artwork byte for byte.
- On labwc Wayland, `gtk-launch org.holonight.Screenshot.desktop` with the staged directories prepended to `PATH` and `XDG_DATA_DIRS` ran the staged executable. The Wayland trace showed the expected launcher title and application ID, compositor configuration and rendered buffers. GTK icon lookup resolved the desktop entry's icon to the staged SVG.

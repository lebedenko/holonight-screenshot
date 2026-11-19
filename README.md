# HoloNight Screenshot

A standalone Qt command-line utility that captures a Hyprland desktop to a PNG. The default saves the full desktop without opening a window. Live capture requires `hyprctl` and `grim` on PATH in the Hyprland session. The raster geometry follows grim 1.5.0.

Build on the host with CMake 3.25+, Ninja, Task, a C++23 compiler and Qt 6.11+ (Core, Gui, Widgets, Quick, Quick Controls, Wayland Client, and Test for the development preset). Install the Wayland component of HoloNight Qt so CMake can resolve `HolonightQt::Wayland`. If several installs are present, set `HolonightQt_DIR` to the package directory containing the Wayland component.

```sh
task build
task test
build/dev/apps/screenshot/holonight-screenshot --help
build/dev/apps/screenshot/holonight-screenshot --version
task build PRESET=release
```

Full-desktop capture requires `--output PATH`. Capture starts immediately unless you pass `--delay-ms N` (0–2147483647 milliseconds). Add `--include-cursor` to draw the pointer. The destination is replaced atomically after a successful capture. Failures are reported on stderr and return a nonzero exit status.

Add `--region` to select an area from one frozen capture. Drag with the left mouse button, including across outputs, and release to review the selection. Drag again to replace it. The primary overlay offers **Copy** (Ctrl+C) and **Save** (Ctrl+S, Enter, or Return). Copy requires `wl-copy` from the `wl-clipboard` package and places a PNG on the Wayland clipboard. Save writes directly when you pass `--output PATH`, or opens the desktop’s configured PNG save dialog when you omit it. On HoloNight, this uses the configured FileChooser portal when available.

Copy and Save end the capture and exit with status 0 on success. Press Esc or dismiss the save dialog to cancel with status 2. Copy, cancellation, and failures leave an existing destination intact.

The default asks grim to exclude the pointer. On the tested Hyprland 0.57.2 session, the pointer appeared in both the tooltip capture and the equivalent direct grim capture without `-c`.

To try a delayed capture on Hyprland with Lua configuration, replace the executable and destination with absolute paths and choose a free key combination.

```sh
hyprctl eval 'hl.bind("SUPER + SHIFT + F10", hl.dsp.exec_cmd("/path/to/holonight-screenshot --region --delay-ms 3000 --output /tmp/holonight-region.png"))'
```

Press the shortcut and open a menu within three seconds, then repeat while hovering over a control until its tooltip appears. Check that the source application keeps focus until the overlay appears. Drag around the frozen menu or tooltip and press Enter. Inspect each PNG before taking the next capture. Remove the temporary binding after the check.

```sh
hyprctl eval 'hl.unbind("SUPER + SHIFT + F10")'
```

The controller contract, coordinate convention and subsequent milestones are described in the [capture design](docs/capture.md). Licensed under GPL-3.0-or-later.

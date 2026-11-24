# HoloNight Screenshot

A Qt Quick screenshot launcher and CLI for Hyprland, Sway, and labwc. Run `holonight-screenshot` without arguments to open the launcher. Choose **Region**, **Window**, or **Screen**, then set a delay and optionally include the mouse cursor. Region captures the desktop first and opens a frozen selection overlay. Drag a rectangle, then press Enter to confirm or Esc to cancel. Window opens a live picker. Use the arrow keys to select a window and Enter to confirm. Screen captures the entire desktop across all outputs.

The result preview offers **Copy**, **Save…**, and **New Capture**. Copy and Save leave the preview open. The default delay is **None**, and the cursor is excluded.

Build on the host with CMake 3.25+, Ninja, Task, C and C++23 compilers, `wayland-scanner`, the `wayland-protocols` XML files, and Qt 6.11+ (Core, Gui, Widgets, Quick, Quick Controls, Wayland Client, and Test for the development preset). Install the Wayland component of HoloNight Qt so CMake can resolve `HolonightQt::Wayland`. If several installs are present, set `HolonightQt_DIR` to the package directory containing the Wayland component.

```sh
task build
task test
build/dev/apps/screenshot/holonight-screenshot --help
build/dev/apps/screenshot/holonight-screenshot --version
task build PRESET=release
```

## Command line

| Command | Result |
| --- | --- |
| `holonight-screenshot --output PATH` | Capture the entire desktop without opening a window. Also accepts `--target screen`. |
| `holonight-screenshot --target area [--output PATH]` | Open the frozen region overlay with annotation tools. `--region` is an alias. |
| `holonight-screenshot --target window [--output PATH]` | Open the window picker and save to the given path or open the result preview. |
| `holonight-screenshot --target active-window --output PATH` | Capture the window focused when the delay expires. Intended for Hyprland and Sway shortcuts. |
| `holonight-screenshot --list-targets` | Print the session’s supported target names as a JSON array. |

Use `--delay-ms N` for a delay in milliseconds (0–2147483647) and `--include-cursor` to request the pointer. For Window, the delay starts after the picker closes. PNG output replaces the destination atomically. CLI exit statuses are 0 for success, 1 for error, and 2 for cancellation. Help and version work outside a Wayland session.

Screen and Region require `grim` and a layout helper. Hyprland uses `hyprctl`, while Sway and labwc use `wlr-randr` with JSON support. Window requires `grim -T` and the compositor’s foreign toplevel list and image-copy protocols. Active Window also requires `hyprctl` on Hyprland or `swaymsg` on Sway. labwc offers Window when the protocols are available.

## CLI region annotation

Drag with the left mouse button, including across outputs, and release to review the selection. Use **Select** to replace the crop and clear its marks. Choose **Pen**, **Arrow**, or **Rectangle**, then drag from inside the crop to draw. A Pen click makes a dot. Red is the default, with yellow, white, and black also available. **Undo** or Ctrl+Z removes the most recent mark.

After releasing the drag, the primary overlay offers **Copy** (Ctrl+C) and **Save** (Ctrl+S, Enter, or Return). Both include the committed marks. Copy requires `wl-copy` from the `wl-clipboard` package and places a PNG on the Wayland clipboard. Save writes directly when you pass `--output PATH`, or opens the desktop’s configured PNG save dialog when you omit it. On HoloNight, this uses the configured FileChooser portal when available.

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

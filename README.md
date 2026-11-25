# HoloNight Screenshot

A Qt Quick screenshot launcher and CLI for Hyprland, Sway, and labwc. Run `holonight-screenshot` without arguments to open the launcher. Choose **Region**, **Window**, or **Screen**, then set a delay and optionally include the mouse cursor. Region captures the desktop first and opens a frozen selection overlay. Drag a rectangle across one or more outputs. Drag a handle to resize it or drag inside to move it. Watch the pixel dimensions, then press Enter to confirm or Esc to cancel. Window opens a live picker. Use the arrow keys to select a window and Enter to confirm. Screen captures the entire desktop across all outputs.

**Capture** opens a preview with **Edit**, **Copy**, **Save…**, and **New Capture**. **Save Directly…** asks for a PNG destination before capture and saves without opening a preview. The default delay is **None**, and the cursor is excluded. Choose **Custom…** to enter any whole number of milliseconds from 0 through 2147483647. The launcher remembers the exact value.

Build on the host with CMake 3.25+, Ninja, Task, pkg-config, C and C++23 compilers, libwayland-client, `wayland-scanner`, the `wayland-protocols` XML files, LayerShellQt 6.6+, and Qt 6.11+ (Core, D-Bus, Gui, Widgets, Quick, Quick Controls, Wayland Client, and Test for the development preset). On Arch Linux, `qt6-base` provides Qt Wayland Client. Screenshot has no HoloNight package dependencies. Region selection also requires the compositor's `zwlr_layer_shell_v1` protocol.

```sh
task build
task test
task run
task run -- --help
task run PRESET=release -- --version
```

## Code quality

Run `task check` for C and C++ formatting, REUSE metadata, Clang static
analysis, QML lint, and the full development test suite. Install Python 3,
Git, `clang-format`, `clang-tidy`, and `reuse` alongside the build tools.
QML lint uses `qmllint` from the Qt SDK selected by CMake.

| Task | What it does |
| --- | --- |
| `task format` | Format owned C and C++ files in place. |
| `task format-check` | Check C and C++ formatting without editing files. |
| `task tidy` | Build and analyze owned translation units in the selected preset. |
| `task qml-lint` | Lint the launcher and region overlay. |
| `task license-check` | Validate REUSE metadata. |

`tidy` and `qml-lint` default to `dev` and accept `PRESET=release`.
Clang-tidy runs up to four translation units concurrently by default, limited
by available CPUs. Set `TIDY_JOBS=2 task tidy` or `TIDY_JOBS=2 task check`
to change the limit.
The aggregate gate always uses `dev` to include tests. The session test
requires local Unix socket access.

## Command line

| Command | Result |
| --- | --- |
| `holonight-screenshot --target screen` | Capture the entire desktop and open the preview. |
| `holonight-screenshot --target area` | Use the same region selector as the launcher, then open the preview. `--region` is an alias. |
| `holonight-screenshot --target window` | Open the window picker, then open the preview. |
| `holonight-screenshot --output PATH` | Save a Screen capture directly. Combine with a target to save that target directly. |
| `holonight-screenshot --editor [--output PATH]` | Open the preview with annotation tools visible. Defaults to Screen. With a path, Save writes to it when invoked. |
| `holonight-screenshot --target active-window --output PATH` | Save the window focused when the delay expires. Active Window is CLI-only and also supports `--editor` with an optional path. |
| `holonight-screenshot --list-targets` | Print the session’s supported target names as a JSON array. |

Use `--delay-ms N` for a delay in milliseconds (0–2147483647) and `--include-cursor` to request the pointer. The delay starts after any launcher or window picker hides. Screen and Active Window direct saves use no GUI. PNG output replaces the destination atomically. CLI exit statuses are 0 for success, 1 for error, and 2 for cancellation. Help and version work outside a Wayland session.

Screen and Region require `grim` and a layout helper. Hyprland uses `hyprctl`, while Sway and labwc use `wlr-randr` with JSON support. Window requires `grim -T` and the compositor’s foreign toplevel list and image-copy protocols. Active Window also requires `hyprctl` on Hyprland or `swaymsg` on Sway. labwc offers Window when the protocols are available.

## Review and annotate a capture

Choose **Edit** to draw with **Pen**, **Arrow**, or **Rectangle** in red, yellow, white, or black. A Pen click makes a dot. **Undo** or Ctrl+Z removes the most recent mark. **Done** returns to the preview with marks visible, and **New Capture** discards the result and marks.

**Copy** (Ctrl+C) and **Save** (Ctrl+S) export committed marks at full resolution and keep the preview open. Copy uses `wl-copy` from the `wl-clipboard` package. **Save…** opens the configured PNG save dialog unless the CLI supplied a destination. Completed actions send desktop notifications. Dismissing the save dialog leaves the result available. Canceling a capture leaves any existing destination intact.

The default asks grim to exclude the pointer. On the tested Hyprland 0.57.2 session, the pointer appeared in both the tooltip capture and the equivalent direct grim capture without `-c`.

To try a delayed capture on Hyprland with Lua configuration, replace the executable and destination with absolute paths and choose a free key combination.

```sh
hyprctl eval 'hl.bind("SUPER + SHIFT + F10", hl.dsp.exec_cmd("/path/to/holonight-screenshot --region --delay-ms 3000 --output /tmp/holonight-region.png"))'
```

Press the shortcut and open a menu within three seconds, then repeat while hovering over a control until its tooltip appears. Check that the source application keeps focus until the overlay appears. Drag around the frozen menu or tooltip and press Enter. Inspect each PNG before taking the next capture. Remove the temporary binding after the check.

```sh
hyprctl eval 'hl.unbind("SUPER + SHIFT + F10")'
```

The controller contract, coordinate convention and subsequent milestones are described in the [capture design](docs/capture.md). The [AI-DLC workflow](docs/aidlc/README.md) explains how to spec, implement, and verify changes.

Licensed under GPL-3.0-or-later.

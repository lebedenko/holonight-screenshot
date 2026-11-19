# HoloNight Screenshot

A standalone Qt command-line utility that captures the full Hyprland desktop to a PNG without opening a window. Live capture requires `hyprctl` and `grim` on PATH in the Hyprland session. The raster geometry follows grim 1.5.0.

Build on the host with CMake 3.25+, Ninja, Task, a C++23 compiler and Qt 6.11+ (Core, Gui and Test for the development preset).

```sh
task build
task test
build/dev/apps/screenshot/holonight-screenshot --help
build/dev/apps/screenshot/holonight-screenshot --version
task build PRESET=release
```

Pass `--output PATH` to choose the destination. Capture starts immediately unless you pass `--delay-ms N` (0–2147483647 milliseconds). Add `--include-cursor` to draw the pointer. The destination is replaced atomically after a successful capture. Failures are reported on stderr and return a nonzero exit status.

The default asks grim to exclude the pointer. On the tested Hyprland 0.57.2 session, the pointer appeared in both the tooltip capture and the equivalent direct grim capture without `-c`.

To try a delayed capture on Hyprland with Lua configuration, replace the executable and destination with absolute paths and choose a free key combination.

```sh
hyprctl eval 'hl.bind("SUPER + SHIFT + F10", hl.dsp.exec_cmd("/path/to/holonight-screenshot --delay-ms 3000 --output /tmp/holonight-desktop.png"))'
```

Press the shortcut and open a menu within three seconds, then repeat while hovering over a control until its tooltip appears. Inspect each PNG before taking the next capture, and check that the source application keeps focus. Remove the temporary binding after the check.

```sh
hyprctl eval 'hl.unbind("SUPER + SHIFT + F10")'
```

The controller contract, coordinate convention and subsequent milestones are described in the [capture design](docs/capture.md). Licensed under GPL-3.0-or-later.

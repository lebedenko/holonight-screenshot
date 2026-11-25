# Make Screenshot independent of HoloNight packages

Status: Implemented

## Inception

Screenshot must build, install, and run as a standalone C++/Qt/QML application with zero dependencies on other HoloNight packages. Screenshot code must use public Qt APIs. The proposed implementation uses KDE's public LayerShellQt API. LayerShellQt owns its internal integration with Qt Wayland and is an allowed external dependency under this design.

Before this change, the capture target linked publicly to `HolonightQt::Wayland` and resolved HolonightQt from its installed CMake package. The interactive region overlay used `LayerSurfaceHost` to place a Qt Quick surface on every captured output and receive configure, dismissal, and failure notifications. The CLI and capture session received libwayland-client through the same link. See the [overlay implementation](../../src/capture/region_overlay.cpp) and [capture design](../capture.md).

### Design

Add a private Screenshot overlay adapter around `QQuickView` and `LayerShellQt::Window`. Apply layer-shell to overlay windows individually so the launcher and Qt save dialog retain their normal window roles. Before showing a view, assign its output, overlay layer, four edge anchors, exclusive zone −1, and Screenshot scope. Give the primary output exclusive keyboard input and the remaining outputs no keyboard input. Keep the adapter out of the installed capture headers.

LayerShellQt's public API exposes no first-configure signal. Prototype readiness using public Qt exposure and geometry notifications. Recheck exposure and expected logical dimensions for every view before enabling selection, including when the view is already exposed at the initial check. Treat this as a candidate readiness check until compositor testing confirms it. Keep the existing timeout and group teardown on dismissal, output changes, or QML and rendering errors. Detect an unavailable layer-shell protocol and fail region selection cleanly. If readiness cannot be established reliably through the public APIs, record the evidence and revisit the design before shipping. [LayerShellQt public API](https://api.kde.org/legacy/plasma/layer-shell-qt/html/window_8h_source.html)

### Acceptance criteria

- [x] Build and run `task test` from a standalone checkout with only system dependencies installed. Use no HoloNight sibling checkout or provider prefix.
- [x] Install into a fresh prefix and rebuild the CLI with `SCREENSHOT_USE_INSTALLED_CAPTURE=ON` against that prefix. The exported targets and package config resolve their own dependencies without HolonightQt. The installed executable has no unresolved libraries and starts without a HoloNight platform theme or icon theme.
- [ ] On Sway, labwc, and Hyprland, verify output placement, panel coverage, keyboard focus, and input gating with multiple outputs, including mixed scale. Verify selection, cancellation, compositor dismissal, output changes, and clean failure when layer-shell is unavailable. Verify the launcher and save dialog still open as ordinary windows.
- [x] Use umbrella `task install -- holonight-screenshot --yes --root <disposable-root>` with build dependencies available and no HoloNight provider packages. Confirm the executable, desktop entry, and fallback icon are installed and recorded as Screenshot-owned.

- [x] Build the opted-in portal against the same installed package and run its focused screenshot tests.

## Construction

- [x] Prototype per-window integration and exposure-based readiness against the compositor acceptance criteria.
- [x] Replace `LayerSurfaceHost` in `RegionOverlay` with the private adapter and preserve the overlay lifecycle and input behavior.
- [x] Declare Qt Quick, Wayland client, and `LayerShellQt::Interface` directly where used. Update the installed CMake config for the imported targets referenced by the static capture export. Remove all HolonightQt discovery, headers, and links.
- [x] Update the README and capture design with the standalone prerequisites and overlay behavior.
- [x] In the umbrella, remove Screenshot’s dependency on `holonight-qt` and declare the distribution packages it directly needs, including LayerShellQt and Qt Wayland. Retain desktop refresh and source-installer ownership handling. Publication and the umbrella pin update are deferred pending authorization.

## Verification and handoff

- The temporary Sway prototype used Qt 6.12.1, LayerShellQt 6.8.3 and Sway 1.12 with headless outputs at scales 1 and 2. The Wayland trace showed a layer-surface request for each matched output, followed by first configure and acknowledgement at 1280 × 720 and 960 × 540 logical pixels. Each Qt view reported its expected size and became exposed after its first configure. The initial show checks reported both views unexposed.
- The fresh standalone source snapshot passed `task test` (7/7) without sibling checkouts or provider prefixes. The test namespace hid host HoloNight CMake packages, headers, libraries, and QML imports. The session test required local Unix socket access.
- The separate `SCREENSHOT_USE_INSTALLED_CAPTURE=ON` build passed its CLI test against the fresh prefix. The installed executable had no unresolved libraries or HoloNight library dependencies in `ldd`. The installed launcher opened as an XDG toplevel with the default Qt and icon themes. The opted-in portal built against the same prefix and passed `portal-screenshot-tests` in a private D-Bus session.
- Mixed-scale Sway and labwc each produced a 400 × 400 PNG from a drag spanning outputs. The traces showed the overlay role, four anchors, exclusive zone −1 and exclusive keyboard input on the primary view. Escape canceled with status 2 on both. Launcher and save dialogs used XDG toplevels, and output removal failed the selection with status 1. Pixel checks confirmed that the frozen selection shade covered a top panel on both outputs in Sway and labwc.
- A controlled Wayland compositor without layer-shell exercised the installed package through the portal-style direct `RegionOverlay` entry point. Discovery failed before creating any windows. A second fixture delayed the first configure for one view. It confirmed that drag input was ignored until both views were exposed at their expected sizes and that Escape worked during the wait. A compositor dismissal and a layer-shell registry removal each hid both views before failure was emitted.
- Focused installer suite passed (138 run, 2 skipped). The requested installer command expanded the selection to Screenshot alone in an empty root with HoloNight build providers hidden. The executable, desktop entry, and fallback icon matched the SHA-256 hashes in the Screenshot ownership manifest.
- Nested Hyprland 0.57.2 initially failed at `CBackend::create()` in the GPU-less headless setup. It started with native labwc as its Wayland parent and access to the host DRM render node. The test used a private runtime directory, D-Bus session, and Hyprland configuration. Its Wayland output and a virtual headless output ran at scales 1 and 2. The installed CLI produced a 400 × 400 cross-output PNG, Escape returned status 2, and both the launcher and the canceled save dialog used XDG toplevels. The trace confirmed exclusive keyboard input on the primary overlay. A repeat panel test with animations disabled waited for both configure acknowledgements and confirmed the selection shade over the top panel on both outputs. Removing the virtual output failed the active selection with a layout error and left no PNG.

- During a partial native labwc check, the Save Screenshot portal rejected the request. The FileChooser backend was still running the replaced executable and mapping deleted Qt 6.12.0 libraries. The journal showed the chooser failing to load QtQuick Controls against the installed Qt 6.12.1 modules. Restarted only the D-Bus-activated HoloNight backend and confirmed it loaded Qt 6.12.1 without deleted Qt mappings. On a private D-Bus session with the installed backend and the HoloNight style, an offscreen SaveFile request stayed open and returned cancellation after Request.Close. Native retesting of the Save Screenshot dialog is deferred.

### Deferred guided acceptance in native labwc

Use the staged Screenshot executable from the local acceptance build. If the temporary prefix is gone, rebuild and install to a fresh temporary prefix. Keep the current desktop running and use a temporary directory for output and traces. Confirm the current monitor arrangement and scales with `wlr-randr --json`. Exercise the existing arrangement first. If mixed scales are unavailable, agree on a reversible display change and capture the original settings before making it.

1. Open the launcher without arguments. Confirm it behaves like a normal desktop window and can be focused, moved, and closed.
2. Open Region with a disposable output path. Verify that the frozen previews align with each output and cover its panels. Move the pointer onto a secondary output before pressing Escape, and confirm the entire group closes, the previous application regains focus, and the path has not been written.
3. Capture a rectangle spanning outputs. Add one mark and undo it, add a different mark, then press Enter. Open the PNG and inspect the crop boundary and mark alignment across the scale seam.
4. Open Region without an output path, select a rectangle, and press Enter. Confirm the PNG save dialog behaves like a normal window. Cancel and confirm that the overlays stay closed.
5. With the tester ready to restore the display, start Region and change one output through the display settings or disconnect a spare monitor. Confirm the whole group closes with a layout error and no PNG. Restore the original arrangement and verify that a new selection succeeds.
6. Record the output names and scales, observed results, and any trace needed to explain a discrepancy. Track any unavailable physical setup as pending.

Native labwc manual testing is deferred until two monitors are connected. Recheck the save dialog after the portal restart when that session resumes. Native manual testing on Sway and Hyprland is also deferred.

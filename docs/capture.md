# Desktop acquisition

A delayed screenshot should preserve an open menu or a stationary tooltip. The application starts a single-shot precise timer before creating any surface. At expiry it asks its provider for the desktop with the requested cursor policy. Immediate capture uses a zero-duration timer and enters the same event-loop path.

## Operation lifetime

Each CaptureController represents one request and owns its timer. Its caller supplies a dedicated DesktopAcquisition that outlives the controller. Provider calls and result delivery take place on the controller thread. A provider may report a startup failure synchronously. The executable chooses its runtime application type after parsing the command line. Headless Screen and Active Window CLI requests use QCoreApplication. CLI Region uses QApplication and opens its first surface after a validated frame is delivered.

Expiry moves the request from waiting to acquiring. Only an acquiring request can publish a frame or failure. Cancellation retires the request before stopping the timer or active provider, so a helper error during shutdown and a late frame both have no effect. Destroying an unfinished controller cancels the operation. A fresh capture uses a new operation.

## Live provider

The executable requires `--output PATH` for full-desktop capture. It accepts `--delay-ms N` (0 through `INT_MAX` milliseconds), and enables pointer capture with `--include-cursor`. The defaults are immediate capture and cursor exclusion. It uses QSaveFile to encode and commit the PNG at the given path. Help and version use QCoreApplication and work outside a Wayland session.

The Grim provider queries output layout at expiry using `hyprctl -j monitors` on Hyprland and read-only `wlr-randr --json` on Sway and labwc. It runs `grim -s SCALE -t png [-c] -` once at the highest active output scale, and queries the monitor layout again. Each layout query has a two-second deadline and grim has 15 seconds. The asynchronous children keep diagnostics separate from the PNG bytes. Cancellation kills the current child and discards the buffered PNG.

The two monitor snapshots must agree on active output identity, mode dimensions, logical positions, transforms and scales. The wlroots adapter also compares the current mode refresh rate and rejects overlapping outputs. Hyprland rejects active mirrors and powered-off outputs. The provider decodes the PNG in memory after validating the snapshots and publishes the frame only if its size matches the expected canvas.

## Session capabilities and window capture

CaptureSession discovers globals from the actual Wayland display and checks the helpers on PATH. The Qt Quick launcher uses Qt’s display. Headless CLI requests open a Wayland connection with an event-driven registry listener. Capability discovery also checks the installed grim help for `-T`. Screen needs an output capture protocol and a layout backend. Area adds layer shell. Window needs the foreign toplevel list, foreign toplevel image source, and image copy capture globals. The CLI and launcher use these same capability results.

The session keeps foreign toplevel handles alive, publishes title and app ID updates on their done events, and removes closed handles. The picker selects by the compositor’s opaque identifier. After confirmation, the UI hides and waits for a Wayland sync on Qt’s display before starting the timer. The launcher uses the same hide barrier for Screen and Region. At expiry, a selected window must still exist. Window capture runs `grim -t png -T ID [-c] -` and publishes an image-local frame. For Active Window, the provider resolves the identifier at expiry with Hyprland’s `activewindow` `stableId` or Sway’s focused tree node and `foreign_toplevel_identifier`. An empty window list or absent focus leaves session capabilities intact. labwc has no focus lookup in this workflow.

The launcher holds one capture at a time. A failure returns to the launcher with a diagnostic, and cancellation restores its prior options. Launcher Region uses the selection-only overlay and returns the crop to the shared Screen, Window, and Region preview. The CLI Region workflow retains its annotation and direct Copy and Save actions.

## Frame coordinates

The provider supplies upright decoded pixels and their QColorSpace. An invalid color space represents unknown encoding. CaptureFrame takes a private copy of the pixels at acquisition, including when the provider wraps externally owned memory. Consumers receive read-only access to the image. An image-local frame uses top-left image coordinates.

A desktop frame also carries the logical output rectangles as displayed, their transforms and native scales, their actual raster rectangles, and the common capture scale. Raster coordinates start at the top-left pixel edge. Subtract the logical desktop bounding box origin and multiply both axes by the capture scale. The mosaic already incorporates output rotation and resampling. Hyprland applies the output transform to mode dimensions before dividing by scale and rounding the logical size. The wlroots output layout uses the integer part of the effective size. Grim truncates the scaled canvas dimensions and composites each output from its rounded-down start edges to its rounded-up end edges, clipped to the canvas. [Hyprland monitor sizing](https://github.com/hyprwm/Hyprland/blob/v0.57.2/src/output/Monitor.cpp) · [grim 1.5.0 renderer](https://sources.debian.org/src/grim/1.5.0%2Bds-1/render.c) For a desktop beginning at x = −1280 with capture scale 1.5, the seam at x = 0 is raster edge 1920.

## Frozen region selection

`--region` opens a Qt Quick overlay on each captured output through the installed `HolonightQt::Wayland` component. The application matches output names and checks logical geometry before opening. Each surface is anchored on four sides with exclusive zone −1 to cover panels. The primary surface has exclusive keyboard input. Drags are enabled after all surfaces configure within two seconds. Output changes, configuration or rendering failures, and unexpected closure retire all overlays together. Qt screen notifications detect visible topology changes, and asynchronous monitor checks compare the full acquisition layout during selection and again after confirmation.

A left-button drag follows the Wayland implicit pointer grab. Originating-surface coordinates are translated into desktop logical coordinates, so a single rectangle spans outputs. Each view paints its portion of the frozen mosaic with the area outside the selection dimmed. Release leaves the selection visible. Another drag in Select mode replaces the crop and clears all marks. The primary overlay shows Copy and Save for a released rectangle with nonempty area intersecting an output and the raster canvas. Ctrl+C copies. Ctrl+S, Enter, and Return save. Presses on the action buttons go to the controls, while a canvas drag retains its originating surface through release. Esc closes the overlays and exits with status 2.

Pen, Arrow, and Rectangle gestures start inside a valid logical selection and use the same pointer grab, including across output seams. Private selection state stores the in-progress gesture and committed marks in capture-raster coordinates, in drawing order and with their original palette colors. The stroke width is three logical pixels at the common capture scale. A Pen click draws a dot, while zero-length arrows and rectangles with no area are discarded. Undo and Ctrl+Z remove the latest committed mark. Copy and Save are disabled throughout a drag.

The crop maps the normalized rectangle through `DesktopMapping::rasterEdge()`. Leading edges round down and trailing edges round up, then the crop clips to the canvas. Copying the image preserves captured gap pixels and the image color description. The buttons and shortcuts request the same final layout check before copying the crop once and painting the committed marks. The preview uses the same annotation painter and includes the in-progress gesture. The painter clips to the rounded raster crop and transforms the palette from sRGB into the capture color space when known. After the overlays close, both actions use the same PNG encoder. Save streams to QSaveFile for atomic replacement. Without `--output`, a PNG save dialog starts in Pictures (or the home directory), suggests a timestamped filename, and confirms overwrites. QFileDialog lets the HoloNight Qt platform theme route Save through the configured FileChooser portal, with Qt’s built-in dialog available as a fallback. Copy sends the encoded PNG through stdin to `wl-copy --type image/png` and waits up to five seconds for the initial process. The background helper serves paste requests after the screenshot application exits. Success exits 0, cancellation exits 2, and failures exit 1.

## Acceptance and subsequent milestones

The tests deliver timer expiry directly and use a colored grid to check frame delivery, cursor forwarding, cancellation, failure and late events. A mixed-scale mosaic checks pixels on either side of the recorded output boundary. The same negative-origin, rotated mixed-scale fixture verifies the crop across raster edge 1920 and color metadata retention. Selection tests exercise redraw, release, cancellation, clips and gap-only rectangles. Annotation tests cover gesture commit and discard, Undo, redraw, confirmation gating, preview/export alignment at a mixed-scale seam, clipping and profile retention.

Fake helpers exercise the live provider stages, including a rotated mixed-scale layout, fractional canvas clipping, PNG decoding, child errors and deadlines, layout changes and cancellation. The [README](../README.md) gives a temporary Hyprland shortcut for checking that the delayed capture preserves an open menu and a stationary tooltip while the source application keeps focus.

When the portal becomes a second consumer, extract capture and selection into an installed package and test both consumers against it. The portal milestone owns authorization before acquisition and advertising the Screenshot interface once Screenshot and PickColor both work.

## Installed capture package

Install this project to expose `HolonightScreenshot::Capture` through
`find_package(HolonightScreenshot CONFIG REQUIRED)`. The target provides the
cancellable controller, compositor layout adapters and Grim acquisition, the frozen
desktop overlay, the annotation renderer, and PNG encoding. Consumers need
Qt 6.11 or newer and the installed HolonightQt Wayland component.

The CLI handles arguments, clipboard transfer, and the save dialog. Its
installed-package build uses `-DSCREENSHOT_USE_INSTALLED_CAPTURE=ON` and a
`CMAKE_PREFIX_PATH` pointing to the provider installation.

The portal opens this overlay with a frozen frame and the requesting app’s
name. Area confirms with **Share** and uses the same crop and annotation
compositor as the CLI. Interactive requests start in Area and can switch to
Desktop. Desktop shows and shares the full captured frame. The point picker
maps a click into the capture-scale raster, checks the layout again, and
uses the image profile to convert the pixel to sRGB. Untagged pixels are
treated as sRGB. Escape and **Cancel** dismiss every review mode. Closing
the overlays releases their references to the captured pixels.

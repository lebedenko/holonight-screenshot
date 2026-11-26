# Desktop acquisition

A delayed screenshot should preserve an open menu or a stationary tooltip. The application starts a single-shot precise timer after any visible capture controls have hidden. At expiry it asks its provider for the desktop with the requested cursor policy. Immediate capture uses a zero-duration timer and enters the same event-loop path.

## Operation lifetime

Each CaptureController represents one request and owns its timer. Its caller supplies a dedicated DesktopAcquisition that outlives the controller. Provider calls and result delivery take place on the controller thread. A provider may report a startup failure synchronously. The executable chooses its runtime application type after parsing the command line. Headless Screen and Active Window CLI requests use QCoreApplication. CLI Region uses QApplication and opens its first surface after a validated frame is delivered.

Expiry moves the request from waiting to acquiring. Only an acquiring request can publish a frame or failure. Cancellation retires the request before stopping the timer or active provider, so a helper error during shutdown and a late frame both have no effect. Destroying an unfinished controller cancels the operation. A fresh capture uses a new operation.

## Live provider

The executable accepts `--target screen|area|window|active-window` and `--region` as an Area alias. A CLI capture copies the PNG and exits by default. `--output PATH` saves directly, and `--editor` opens the editor and retains an optional path until Save. Either option implies Screen when the target is omitted. Active Window is CLI-only. Screen and Active Window clipboard and direct-save captures use QCoreApplication. The executable accepts `--delay-ms N` (0 through `INT_MAX` milliseconds) and `--include-cursor`. Help and version work outside Wayland.

The Grim provider queries output layout at expiry using `hyprctl -j monitors` on Hyprland and read-only `wlr-randr --json` on Sway and labwc. It runs `grim -s SCALE -t png [-c] -` once at the highest active output scale, and queries the monitor layout again. Each layout query has a two-second deadline and grim has 15 seconds. The asynchronous children keep diagnostics separate from the PNG bytes. Cancellation kills the current child and discards the buffered PNG.

The two monitor snapshots must agree on active output identity, mode dimensions, logical positions, transforms and scales. The wlroots adapter also compares the current mode refresh rate and rejects overlapping outputs. Hyprland rejects active mirrors and powered-off outputs. The provider decodes the PNG in memory after validating the snapshots and publishes the frame only if its size matches the expected canvas.

## Session capabilities and window capture

CaptureSession discovers globals from the actual Wayland display and checks the helpers on PATH. The Qt Quick launcher uses Qt’s display. Headless CLI requests open a Wayland connection with an event-driven registry listener. A dedicated Qt worker thread resolves the helper executables and probes grim help for `-T` with a two-second deadline. Helper results are cached atomically in the private XDG runtime directory by boot, login session when available, connected Wayland socket, and helper search path. Cold launches use a bounded lock to share the probe. Registry discovery and the window subscription run on every connection. The session publishes once both helper and Wayland discovery finish. Session teardown stops the worker and reaps any running probe. Screen needs an output capture protocol and a layout backend. Area adds layer shell. Window needs the foreign toplevel list, foreign toplevel image source, and image copy capture globals. The CLI and launcher use these same capability results.

The session keeps foreign toplevel handles alive, publishes title and app ID updates on their done events, and removes closed handles. The picker selects by the compositor’s opaque identifier. After confirmation, the UI hides and waits for a Wayland sync on Qt’s display before starting the timer. The launcher uses the same hide barrier for Screen and Region. On Hyprland, it first uses `hyprctl` to disable animation for its own window so the compositor cannot include a closing snapshot in the desktop image. The selected capture delay starts after the hide sync. If Hyprland does not acknowledge the animation change, the launcher reports the failure before taking a screenshot. At expiry, a selected window must still exist. Window capture runs `grim -t png -T ID [-c] -` and publishes an image-local frame. For Active Window, the provider resolves the identifier at expiry with Hyprland’s `activewindow` `stableId` or Sway’s focused tree node and `foreign_toplevel_identifier`. An empty window list or absent focus leaves session capabilities intact. labwc has no focus lookup in this workflow.

The launcher disables target selection and Capture during discovery. Delay, cursor and editor controls are usable during discovery. The launcher remembers these settings and the target in `$XDG_STATE_HOME/holonight-screenshot/launcher.ini`, defaulting to `~/.local/state` when the variable is unset, empty, or relative. The four delay presets are None, 3 s, 5 s and 10 s. Legacy values outside the presets restore as None. Explicit CLI captures use their own flags. The launcher restores the saved target when supported and otherwise selects and saves the first available in Region, Screen, Window order. Capture takes focus and handles Return or keypad Enter once per press. With no supported targets, Capture stays disabled and the saved target is preserved. Unavailable targets show their reason in a tooltip and accessible description.

The launcher holds one capture at a time. Failure restores it with a scrollable diagnostic popover opened from the warning button beside Include mouse cursor. Cancellation restores the prior settings. The shared result handler opens the editor when requested, saves direct CLI output atomically, or copies the PNG. Launcher clipboard success restores the launcher and submits a desktop notification. Closing the editor returns to the launcher or exits for CLI captures.

Editor Copy and Save keep the editor open and send one [desktop notification](https://specifications.freedesktop.org/notification/latest/protocol.html) per completed action. Failure notifications include the output error. Canceling the dialog preserves edits. The executable submits notifications asynchronously over Qt D-Bus and logs delivery errors as warnings.

## Frame coordinates

The provider supplies upright decoded pixels and their QColorSpace. An invalid color space represents unknown encoding. CaptureFrame takes a private copy of the pixels at acquisition, including when the provider wraps externally owned memory. Consumers receive read-only access to the image. An image-local frame uses top-left image coordinates.

A desktop frame also carries the logical output rectangles as displayed, their transforms and native scales, their actual raster rectangles, and the common capture scale. Raster coordinates start at the top-left pixel edge. Subtract the logical desktop bounding box origin and multiply both axes by the capture scale. The mosaic already incorporates output rotation and resampling. Hyprland applies the output transform to mode dimensions before dividing by scale and rounding the logical size. The wlroots output layout uses the integer part of the effective size. Grim truncates the scaled canvas dimensions and composites each output from its rounded-down start edges to its rounded-up end edges, clipped to the canvas. [Hyprland monitor sizing](https://github.com/hyprwm/Hyprland/blob/v0.57.2/src/output/Monitor.cpp) · [grim 1.5.0 renderer](https://sources.debian.org/src/grim/1.5.0%2Bds-1/render.c) For a desktop beginning at x = −1280 with capture scale 1.5, the seam at x = 0 is raster edge 1920.

## Frozen region selection

Region selection asynchronously checks for `zwlr_layer_shell_v1` on Qt's Wayland display before mapping any windows, including when the portal opens `RegionOverlay` directly. The overlay matches captured output names and logical geometry to Qt screens. It initializes every frozen Qt Quick preview before showing the group. Each view gets its own LayerShellQt overlay role, four anchors, and exclusive zone −1 to cover panels. The primary view gets exclusive keyboard focus and secondary views receive no keyboard input. The launcher and save dialog use ordinary Qt window roles.

Escape remains active while the group is opening. Selection starts only after every view is exposed at its captured logical size within the two-second configure deadline. Qt screen notifications and asynchronous layout polling detect topology changes, and confirmation runs a final layout check before returning a result. Protocol removal, output changes, unexpected closure, and QML or rendering failures hide the whole group before the terminal signal and defer destruction of the views.

A left-button drag follows the Wayland implicit pointer grab. Originating-surface coordinates are translated into desktop logical coordinates, so a single rectangle spans outputs. Each view paints its portion of the frozen mosaic with the area outside the selection dimmed. Release leaves the selection visible. Presses on the action buttons go to the controls, while a canvas drag retains its originating surface through release. Esc cancels the selection. CLI cancellation exits with status 2.

Launcher and CLI Region use SelectionOnly mode. The lavender outline has eight resize handles. Drag a handle to resize while keeping the opposite edges fixed, drag inside to move the rectangle, or drag outside to replace it. Coordinates are clamped to the virtual desktop bounds, so the crop can span seams and gaps. The dimension badge follows the visible crop on the output with the most overlap, preferring the primary output on ties. Its dimensions use the same floor, ceil, and raster clipping rules as the exported PNG.

Capture and Cancel appear after a valid release on the same output as the badge. The strip centers below that output’s visible crop or sits inside near the bottom when space runs out. It can extend beyond small crops to keep both buttons on-screen. If the inside placement overlaps the badge, the badge moves above the strip when space permits. The strip hides during drawing, movement, and resizing, while Esc stays available. Capture and Enter (including keypad Enter) confirm the crop. During the final layout check, Capture remains visible but disabled and Cancel stays available. After a successful check, the result handler routes the crop like other captures.

The app-local editor document stores the original capture, an ordered list of marks in source-image coordinates, and a committed pixel crop. The Qt Quick canvas maps pointer positions through zoom and crop origin. One compositor renders marks in order and clips to the crop, with blur sampling the pixels already rendered underneath. Text boxes wrap and clip to source-image rectangles. An inline draft stays outside the committed document until applied, then creates one undo entry. Undo and redo store document states at completed edits. The canvas adds selection outlines, crop handles, and thirds guides as separate Qt Quick items. Installed region overlay modes retain their own capture-scale annotations and cross-output pointer grab.

Region selection maps the rectangle through `DesktopMapping::rasterEdge()` and clips the crop to the canvas. The editor rounds its pending crop outward to the nearest source pixels and clips it to the original image. The compositor transforms annotation colors from sRGB into the capture color space, with an sRGB display conversion for Qt Quick. Both editor exports render committed edits at full resolution and preserve the capture profile. Export is disabled during gestures, text drafts, and pending crops. The shared PNG encoder streams saves to QSaveFile for atomic replacement. The dialog uses QFileDialog so the platform theme can use the FileChooser portal. Clipboard output sends PNG data to `wl-copy --type image/png`, whose background helper serves paste requests after application exit. CLI success exits 0, capture or clipboard failure exits 1, and picker or region cancellation exits 2.

## Acceptance and subsequent milestones

The tests deliver timer expiry directly and use a colored grid to check frame delivery, cursor forwarding, cancellation, failure and late events. A mixed-scale mosaic checks pixels on either side of the recorded output boundary. The same negative-origin, rotated mixed-scale fixture verifies the crop across raster edge 1920 and color metadata retention. Selection tests exercise redraw, release, cancellation, clips and gap-only rectangles. Annotation tests cover gesture commit and discard, Undo, redraw, confirmation gating, preview/export alignment at a mixed-scale seam, clipping and profile retention.

Fake helpers exercise the live provider stages, including a rotated mixed-scale layout, fractional canvas clipping, PNG decoding, child errors and deadlines, layout changes and cancellation. The [README](../README.md) gives a temporary Hyprland shortcut for checking that the delayed capture preserves an open menu and a stationary tooltip while the source application keeps focus.

When the portal becomes a second consumer, extract capture and selection into an installed package and test both consumers against it. The portal milestone owns authorization before acquisition and advertising the Screenshot interface once Screenshot and PickColor both work.

## Installed capture package

Install this project to expose `HolonightScreenshot::Capture` through
`find_package(HolonightScreenshot CONFIG REQUIRED)`. The target provides the
cancellable controller, compositor layout adapters and Grim acquisition, the frozen
desktop overlay, the annotation renderer, and PNG encoding. Consumers need
Qt 6.11+, libwayland-client, and LayerShellQt 6.6+. The installed CMake config resolves all targets needed by the static capture library.

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

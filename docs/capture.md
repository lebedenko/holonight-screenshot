# Desktop acquisition

A delayed screenshot should preserve an open menu or a stationary tooltip. The application starts a single-shot precise timer before creating any surface. At expiry it asks its provider for the desktop with the requested cursor policy. Immediate capture uses a zero-duration timer and enters the same event-loop path.

## Operation lifetime

Each CaptureController represents one request and owns its timer. Its caller supplies a dedicated DesktopAcquisition that outlives the controller. Provider calls and result delivery take place on the controller thread. A provider may report a startup failure synchronously. The executable chooses its runtime application type after parsing the command line. Full-desktop capture uses QCoreApplication. Region capture uses QGuiApplication and opens its first surface after a validated frame is delivered.

Expiry moves the request from waiting to acquiring. Only an acquiring request can publish a frame or failure. Cancellation retires the request before stopping the timer or active provider, so a helper error during shutdown and a late frame both have no effect. Destroying an unfinished controller cancels the operation. A fresh capture uses a new operation.

## Live provider

The executable requires `--output PATH`, accepts `--delay-ms N` (0 through `INT_MAX` milliseconds), and enables pointer capture with `--include-cursor`. The defaults are immediate capture and cursor exclusion. It uses QSaveFile to encode and commit the PNG at the given path. Help and version use QCoreApplication and work outside a Wayland session.

The executable's private provider starts `hyprctl -j monitors` at expiry, runs `grim -s SCALE -t png [-c] -` once at the highest active output scale, and queries the monitor layout again. Each layout query has a two-second deadline and grim has 15 seconds. The asynchronous children keep diagnostics separate from the PNG bytes. Cancellation kills the current child and discards the buffered PNG.

The two monitor snapshots must agree on output IDs and names, mode dimensions, logical positions, transforms and scales. Active mirrors and powered-off outputs are unsupported. The provider decodes the PNG in memory after validating the snapshots and publishes the frame only if its size matches the expected canvas.

## Frame coordinates

The provider supplies upright decoded pixels and their QColorSpace. An invalid color space represents unknown encoding. CaptureFrame takes a private copy of the pixels at acquisition, including when the provider wraps externally owned memory. Consumers receive read-only access to the image. An image-local frame uses top-left image coordinates.

A desktop frame also carries the logical output rectangles as displayed, their transforms and native scales, their actual raster rectangles, and the common capture scale. Raster coordinates start at the top-left pixel edge. Subtract the logical desktop bounding box origin and multiply both axes by the capture scale. The mosaic already incorporates output rotation and resampling. Hyprland applies the output transform to mode dimensions before dividing by scale and rounding the logical size. Grim truncates the scaled canvas dimensions and composites each output from its rounded-down start edges to its rounded-up end edges, clipped to the canvas. [Hyprland monitor sizing](https://github.com/hyprwm/Hyprland/blob/v0.57.2/src/output/Monitor.cpp) · [grim 1.5.0 renderer](https://sources.debian.org/src/grim/1.5.0%2Bds-1/render.c) For a desktop beginning at x = −1280 with capture scale 1.5, the seam at x = 0 is raster edge 1920.

## Frozen region selection

`--region` opens a Qt Quick overlay on each captured output through the installed `HolonightQt::Wayland` component. The application matches output names and checks logical geometry before opening. Each surface is anchored on four sides with exclusive zone −1 to cover panels. The primary surface has exclusive keyboard input. Drags are enabled after all surfaces configure within two seconds. Output changes, configuration or rendering failures, and unexpected closure retire all overlays together. Qt screen notifications detect visible topology changes, and asynchronous monitor checks compare the full acquisition layout during selection and again before saving.

A left-button drag follows the Wayland implicit pointer grab. Originating-surface coordinates are translated into desktop logical coordinates, so a single rectangle spans outputs. Each view paints its portion of the frozen mosaic with the area outside the selection dimmed. Release leaves the selection visible and another drag replaces it. Enter or Return confirms a released rectangle with nonempty area intersecting an output. Esc closes the overlays and exits with status 2.

The crop maps the normalized rectangle through `DesktopMapping::rasterEdge()`. Leading edges round down and trailing edges round up, then the crop clips to the canvas. Copying the image preserves captured gap pixels and the image color description. After the overlays close, the existing atomic PNG writer saves the selection. Success exits 0 and failures exit 1.

## Acceptance and subsequent milestones

The tests deliver timer expiry directly and use a colored grid to check frame delivery, cursor forwarding, cancellation, failure and late events. A mixed-scale mosaic checks pixels on either side of the recorded output boundary. The same negative-origin, rotated mixed-scale fixture verifies the crop across raster edge 1920 and color metadata retention. Offscreen selection tests exercise redraw, release, cancellation, clips and gap-only rectangles.

Fake helpers exercise the live provider stages, including a rotated mixed-scale layout, fractional canvas clipping, PNG decoding, child errors and deadlines, layout changes and cancellation. The [README](../README.md) gives a temporary Hyprland shortcut for checking that the delayed capture preserves an open menu and a stationary tooltip while the source application keeps focus.

Copy and Save will share one renderer, followed by annotation. When the portal becomes a second consumer, extract capture and selection into an installed package and test both consumers against it. The portal milestone owns authorization before acquisition and advertising the Screenshot interface once Screenshot and PickColor both work.

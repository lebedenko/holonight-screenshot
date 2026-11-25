# Screenshot preview notifications

Status: Verified

## Inception

Copy and Save currently put a status label below the screenshot, taking space
from the image. Move feedback to desktop notifications in the shared preview.
Keep launcher capture diagnostics and direct CLI output behavior as described in
the [capture design](../capture.md).

### Acceptance criteria

- [x] Copy and Save each produce a valid PNG and one notification with the approved success text.
- [x] Copy and Save failures each produce one notification with an action-specific title and the original error details. Canceling Save produces no notification.
- [x] The image and buttons retain their layout and the preview stays open throughout.
- [x] A private session bus without a notification service produces a warning and leaves the preview usable.

## Construction

- [x] Remove the preview status row in [Launcher.qml](../../apps/screenshot/Launcher.qml) and send results over session D-Bus from [workflow.cpp](../../apps/screenshot/workflow.cpp).
- [x] Link Qt D-Bus to the [executable](../../apps/screenshot/CMakeLists.txt) and update the [capture design](../capture.md) and [build dependencies](../../README.md).

## Verification and handoff

- `task test` passed all seven suites with local Unix socket access. The sandboxed run blocked socket creation for the session suite.
- `task qml-lint`, `task format-check`, and `git diff --check` passed.
- An automated Hyprland run drove the actual preview buttons and Qt fallback save dialog. Qt decoded both the clipboard and saved PNGs. The notification service returned one ID for each successful action and no request followed cancellation.
- Hiding `wl-copy` from the app and saving into `/proc` produced one notification each with the expected action title and underlying error. The image and all three buttons kept their geometry, and the preview stayed open throughout.
- On a private session bus with service activation disabled, two successive Copy actions logged one delivery warning each. The clipboard PNG decoded and the preview remained usable with the same geometry.
- Notification delivery was verified through service replies. Visual banner rendering and the native portal dialog were outside the automated check.

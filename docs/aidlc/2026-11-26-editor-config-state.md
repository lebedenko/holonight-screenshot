# Screenshot configuration and editor window state

Status: Verified

## Inception

Load screenshot defaults from TOML and remember the editor's normal size and maximized state between launches.

### Acceptance criteria

- [x] Resolve XDG paths, validate config values and apply defaults to every new capture.
- [x] Restore and fit editor geometry while leaving placement to the compositor.
- [x] Respect close settings on successful outputs and region cancellation. Preserve the editor on errors and dialog cancellation, and preserve CLI statuses and output precedence.

## Construction

- [x] Add app-local [TOML configuration](../../apps/screenshot/config.cpp) and [editor geometry persistence](../../apps/screenshot/workflow.cpp). Include toml++ in the installer catalog.
- [x] Apply per-capture annotation defaults and share the annotation font across inline editing, measurement and export.
- [x] Cover config, geometry, successive captures and output behavior in the [session tests](../../tests/session_test.cpp). Document settings in the [README](../../README.md).

## Verification and handoff

- The focused CLI, launcher, editor, PNG output and session tests passed. All eight development suites passed with `ctest --preset dev --output-on-failure`. The session suite used local Wayland sockets outside the sandbox.
- The session tests exercised unset, empty and relative XDG paths, config validation and fallback, editor geometry, annotation defaults across two captures, Copy and Save with both close settings, export failures, Save dialog cancellation and explicit output paths.
- QML lint, `task format-check`, `git diff --check` and all 16 installer dependency tests passed.
- The CLI built against a staged installed capture package with `SCREENSHOT_USE_INSTALLED_CAPTURE=ON`. The staged build's capture and CLI tests passed.
- In isolated headless Sway, the editor opened at 1600 × 900 and restored the compositor-reported 1098 × 713 client size after resizing and relaunching. After editor Copy and launcher quick capture exited, `wl-paste` retrieved readable 1920 × 1080 PNGs. Disabling close after copy returned quick capture to the launcher.
- On Sway, Escape returned to the launcher or exited according to the region-cancel setting. Direct CLI region cancellation returned status 2 and created no output.
- In isolated headless labwc, relaunch applied the saved maximized state. Unmaximizing restored the saved 1000 × 600 normal size, and the state file recorded `maximized=false` while retaining those dimensions.

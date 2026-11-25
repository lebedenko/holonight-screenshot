# Remember capture settings and focus Capture at startup

Status: Verified

## Inception

Restore launcher preferences, make keyboard capture available after discovery, and reuse stable helper probe results across launches. See the [capture design](../capture.md).

### Acceptance criteria

- [x] Launcher edits persist across restarts, with XDG state fallback, supported-target fallback, and explicit CLI isolation.
- [x] Capture is disabled during discovery, then receives focus and accepts Return and keypad Enter without repeating.
- [x] Cold, warm, concurrent, and changed-identity launches handle helper probes correctly while window discovery remains live.

## Construction

- [x] Save launcher preferences field by field through [Workflow](../../apps/screenshot/workflow.cpp) and bind the controls and Capture focus in [Launcher.qml](../../apps/screenshot/Launcher.qml).
- [x] Cache helper results on the [discovery worker](../../src/capture/capture_session.cpp). The key includes the boot, `XDG_SESSION_ID` when provided, the connected socket's path and inode, the peer PID and start time, and the helper search path.
- [x] Verify startup and persistence with [launcher tests](../../tests/launcher_test.cpp), [session tests](../../tests/session_test.cpp), and a Hyprland smoke test.

## Verification and handoff

- `task test` passed all seven suites with Wayland socket access. The sandboxed run could not create the test sockets.
- The launcher tests verified that Return and keypad Enter do nothing during discovery, Capture receives focus after discovery, auto-repeat is ignored, and capture receives the configured delay and cursor settings.
- The session tests verified unset, empty, and relative XDG state paths, unsupported-target replacement, CLI preference isolation, one probe across concurrent cold launches and a warm launcher, a live window subscription on the warm launch, and fresh probes for a changed login session or listening socket.
- `task qml-lint`, `task format-check`, and `git diff --check` passed.
- The Hyprland smoke test used isolated state and runtime directories with the live Wayland connection. The cold launcher saved Region as its initial target. Keyboard edits saved a 10-second delay and the cursor preference before capture. The warm launcher restored the cursor preference. The cold launcher, CLI listing, and warm launcher shared one `grim -h` probe.
- New Capture from a CLI preview and end-to-end capture timing after a real Hyprland restart were not exercised manually.

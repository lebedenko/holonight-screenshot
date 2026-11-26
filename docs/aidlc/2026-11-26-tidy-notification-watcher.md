# Notification watcher tidy diagnostic

Status: Verified

## Inception

Clang's leak checker flags the asynchronous D-Bus notification watcher in the screenshot workflow. Qt's parent owns the watcher and its completion callback schedules early cleanup.

### Acceptance criteria

- [x] `task tidy` passes with the leak checker enabled.
- [x] All eight test targets pass and the notification watcher retains its existing lifetime.

## Construction

- [x] Document Qt ownership and suppress the diagnostic at its reported location in [workflow.cpp](../../apps/screenshot/workflow.cpp).

## Verification and handoff

The initial `task tidy` run reported a single `clang-analyzer-cplusplus.NewDeleteLeaks` error in `notifyOutput`.

- `task tidy` passed across all 20 translation units.
- `task test` passed seven targets. The sandbox blocked the Unix sockets needed by the mock Wayland server in the session target.
- `task test -- -R '^session$'` passed outside the sandbox.
- `task format-check` and `git diff --check` passed.
- The source diff changes only comments, preserving Qt parent ownership and deferred cleanup.

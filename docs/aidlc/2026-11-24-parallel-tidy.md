# Run clang-tidy in parallel

Status: Verified

## Inception

The quality runner passes all sources to a single clang-tidy process. Run independent translation units concurrently for the standalone tidy task and the quality gate.

### Acceptance criteria

- The runner analyzes owned translation units concurrently and supports a configurable job limit.
- The filtered compilation database and header filter apply to every worker.
- Diagnostics from each translation unit stay together and a failing worker fails the task.

## Construction

- [x] Parallelize the shared [quality runner](../../scripts/quality.py) and document the job limit for both entry points in the [README](../../README.md).
- [x] Verify concurrency and failure propagation.

## Verification and handoff

- `task tidy` passed with four workers across all 16 owned translation units.
- A fake clang-tidy confirmed the two-worker limit, filtered database and header filter, grouped output, and propagation of exit status 9. A zero job limit was rejected.
- `task test` passed all seven tests with Unix socket access.
- `task qml-lint` and `git diff --check` passed.

# Implementation baseline

These milestones were implemented before the AI-DLC workflow was introduced. The commits record what landed. The CTest targets identify available automated coverage and the results below record what was run during the backfill.

| Shipped milestone | Implementation commits | Verification references |
| --- | --- | --- |
| Delayed Hyprland desktop capture | `a1d3f92` | `capture`, `grim`; live menu and tooltip procedure in the [README](../../README.md) |
| Frozen region selection and clipboard or PNG output | `a6211e7`, `5484af8` | `region`, `png-output`, `cli` |
| Pen, Arrow, Rectangle, and Undo for CLI region captures | `dd408b0` | `region` |
| Installed capture package and frozen review for portal consumers | `d85693d`, `e578f23` | `region` covers crop logic and `capture` covers sRGB pixel sampling |
| Sway and labwc desktop acquisition | `db2d272` | `grim` uses fake layout and capture helpers |
| Native capture targets, launcher, and window picker | `e6311b2` | `session`, `cli`; launcher and picker interactions require a live compositor |

See the [capture design](../capture.md) for current behavior and the [CTest definitions](../../tests/CMakeLists.txt) for the test entry points.

## Backfill verification

- `task test` passed `capture`, `cli`, `grim`, `region`, and `png-output`. `session` failed during local Wayland socket setup in the sandbox.
- `task test -- -R '^session$'` passed outside the sandbox.

Live compositor, installed-package, and portal integration checks are pending for this backfill.

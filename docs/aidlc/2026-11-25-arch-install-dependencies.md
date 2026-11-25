# Screenshot dependencies on Arch Linux

Status: Verified

## Inception

Screenshot needs Qt Wayland Client. The Arch host supplies it through `qt6-base`, so requiring the separate `qt6-wayland` package blocks installation.

### Acceptance criteria

- [x] Screenshot preflight succeeds with `qt6-base` installed and `qt6-wayland` absent.
- [x] The standalone build documentation identifies the Arch package that supplies Qt Wayland Client.

## Construction

- [x] Update the umbrella dependency catalog and the standalone README.

## Verification and handoff

The umbrella `test_install_commands.py` fixtures passed with merged Qt packaging and confirmed that a missing Screenshot dependency still blocks preflight. The umbrella `task install -- --check` passed on the Arch host with `qt6-base` installed and `qt6-wayland` absent. Reviewed the standalone README and ran `git diff --check` in both repositories.

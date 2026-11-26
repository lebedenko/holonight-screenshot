# Inline annotation text

Status: Verified

## Acceptance criteria

- A Text click focuses an empty box sized for two lines. A drag sizes the box in source-image coordinates.
- Multiline text can be committed, canceled, and edited again. The committed box wraps and clips its text on canvas and in exports. A completed edit is one undo step.
- Editor controls use point sizes or inherit the theme font. Annotation glyphs use source pixels to keep their size fixed in exports.

## Construction

- Add text placement and draft state to the editor document.
- Add the canvas text input and update editor typography.
- Verify text gestures and document history in focused tests.

## Verification

- Focused editor, session, and launcher tests passed. The document test covers the default two-line box, reverse drag geometry, cancel, re-edit, and undo/redo. The Qt Quick session test verifies initial focus, typing across lines, Ctrl+Enter, export gating, and absence of the mixed font-size warning.
- QML lint, C++ formatting, and `git diff --check` passed.
- The full `task test` run passed all eight suites.

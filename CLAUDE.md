# Working agreements for this repository

These instructions apply to automated/agent contributions to this checkout.

## Branching and commits

- **Work on `master`.** Do not create feature branches; commit directly to `master`.
- **Commit the changes you make.** Group related changes into focused commits with a
  descriptive message, following the existing commit style (see `git log`). End commit
  messages with the `Co-Authored-By` trailer.

## Code style

- **Match the surrounding code.** Follow the conventions of the file and module you are
  editing (naming, spacing, comment density, the `clang-format` layout, the
  `Mumble::<Area>` header-only helper pattern, etc.) rather than introducing a new style.

## Warnings and CI

- **No new warnings.** Builds are configured with `warnings-as-errors` **ON** by default
  (`CMakeLists.txt`), so any new compiler warning fails CI. Review your changes and build
  before committing to confirm a clean build.

## Tests

- **Write unit tests for your changes.** Prefer extracting pure, Qt-free logic into a
  header-only helper (e.g. `src/mumble/AudioInput*.h` in a `Mumble::<Area>` namespace) and
  testing it with QtTest, mirroring the existing tests under `src/tests/` (for example
  `TestAudioAmplification` and `TestAudioInputSilence`). Register new tests in
  `src/tests/CMakeLists.txt`.

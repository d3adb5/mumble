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
- **Do not wait for CI builds.** You may push to `fork/master`, but do not sit
  around waiting for CI builds to go through. You will be directed to
  failed build logs if necessary.

## Tests

- **Write unit tests for your changes.** Prefer extracting pure, Qt-free logic into a
  header-only helper (e.g. `src/mumble/AudioInput*.h` in a `Mumble::<Area>` namespace) and
  testing it with QtTest, mirroring the existing tests under `src/tests/` (for example
  `TestAudioAmplification` and `TestAudioInputSilence`). Register new tests in
  `src/tests/CMakeLists.txt`.

## Configurability

- **When adding new features, aim for configurability** by adding settings
  instead of hardcoding values. Do this where it makes sense. Add toggles, add
  sliders, prefer flexibility alongside simplicity where it makes sense.

## Platform Compatibility

- **Changes should be compatible with all target platforms,** but where
  impossible, target Linux and Windows primarily.
- **Avoid protocol changes unless benefits outweigh the cons.** The client
  should ideally remain compatible with 1.5.x servers and older.

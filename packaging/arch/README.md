# Arch Linux / CachyOS package

`PKGBUILD` builds the Mumble client of this fork as the `mumble-git` package (it replaces,
and conflicts with, Arch's `mumble`).

## Installing a prebuilt package

Every push to `master` publishes a [release](https://github.com/d3adb5/mumble/releases) with
one package per CPU target. Download the one for your CPU and run `sudo pacman -U <file>`.

| File suffix  | For                                                                      |
|--------------|--------------------------------------------------------------------------|
| `x86-64`     | any x86-64 CPU                                                           |
| `x86-64-v2`  | CPUs with SSE4.2/POPCNT (roughly 2009 onwards)                           |
| `x86-64-v3`  | CPUs with AVX2 (Intel Haswell/Skylake and later, AMD Zen and later)      |
| `x86-64-v4`  | CPUs with AVX-512                                                        |
| `znver3`     | AMD Zen 3 (Ryzen 5000)                                                   |
| `znver4`     | AMD Zen 4 (Ryzen 7000/9000)                                              |

On CachyOS, take the one that matches your repositories (`grep cachyos /etc/pacman.conf`):
`znver4` for `cachyos-*-znver4`, `x86-64-v3` for `cachyos-*-v3`, and so on.
`/lib/ld-linux-x86-64.so.2 --help` lists the x86-64 levels your CPU supports.

## Building from this clone

```sh
cd packaging/arch
makepkg -si                          # flags from /etc/makepkg.conf (CachyOS: -march=native)
MUMBLE_MARCH=znver4 makepkg -si      # or build for a CPU target like the releases
```

makepkg packages the commit checked out in this clone, so commit your changes first.
Submodules come from this clone when they are checked out (`git submodule update --init`),
from their upstream URLs otherwise. `MUMBLE_BUILD_NUMBER` overrides the build number, which
otherwise comes from the latest `v1.7.<n>` tag.

makepkg keeps its working files (`src/`, `pkg/`, the `mumble/` mirror and the packages)
in this directory; they are ignored by git. `makepkg -C` starts from scratch.

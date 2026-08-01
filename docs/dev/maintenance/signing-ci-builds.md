# Distributing the CI client builds

The client builds produced by `.github/workflows/build.yml` are not signed with a certificate that Windows
or macOS recognizes, because obtaining one costs money on both platforms and gains a fork nothing that a
one-line install command does not. This document records what signing would cost, and how the macOS build
is installed without it.

## Why the builds are left unsigned

### macOS

Signing requires an **Apple Developer Program** membership at 99 USD per year, which issues the
*Developer ID Application* certificate. A signature on its own is not even sufficient: macOS refuses to
launch a Developer ID signed application it has not seen before unless the build has also been
**notarized**, i.e. uploaded to Apple for scanning, which is gated behind the same paid membership. There
is no free tier for either.

What the build does instead is *ad-hoc* signing (`osxdist.py --ad-hoc-sign`), which is a signature with no
identity attached. That is not a trust statement — it exists because Apple Silicon refuses to execute code
carrying no signature whatsoever, so without it the binaries would not run at all.

### Windows

Since June 2023 the CA/Browser Forum baseline requirements oblige certificate authorities to keep code
signing private keys on certified hardware, so a publicly trusted `.pfx` file is not something one can buy
and drop into a repository secret any more. Signing means paying for a *service* and storing its
credentials: Azure Trusted Signing (~10 USD/month) is the cheapest, SignPath.io is free for open source
projects, and Certum issues onto a physical token for around 100 EUR/year.

Note that upstream's SignPath integration in `.github/actions/sign-binaries/action.yml` hardcodes
upstream's `organization-id` and `project-slug`, so it does nothing for a fork until those are replaced.
Also note that an OV certificate does not immediately silence SmartScreen — reputation accrues over
downloads — while an EV certificate is trusted on sight.

## Installing the macOS build via Homebrew

A quarantine flag is what actually triggers Gatekeeper. It is set by whatever program downloads the file,
so an image fetched with a browser is quarantined and the same image fetched with `curl` is not. Clearing
it is what any of the approaches below ultimately do.

**Homebrew's own casks are not a way around signing, and are moving in the opposite direction.** The
`--no-quarantine` flag was deprecated in Homebrew 5.1 and support for casks that fail Gatekeeper checks
ends on 1 September 2026, so the official `homebrew/cask` tap now effectively requires exactly the paid
notarization described above.

A **personal tap** is a different matter, since it is only bound by what `brew` itself permits. Two
approaches work without paying anything:

### A cask that clears the quarantine flag itself

Homebrew no longer offers a flag for this, but a cask may still do it in a `postflight` block. Create a
repository named `homebrew-mumble`, and in it `Casks/mumble-fork.rb`:

```ruby
cask "mumble-fork" do
  version "1.7.123"
  sha256 "<shasum -a 256 of the .dmg>"

  url "https://github.com/d3adb5/mumble/releases/download/v#{version}/Mumble-#{version}.dmg"
  name "Mumble"
  desc "Low-latency, high quality voice chat"
  homepage "https://github.com/d3adb5/mumble"

  depends_on macos: ">= :big_sur"

  app "Mumble.app"

  # The build is only ad-hoc signed, so macOS would otherwise refuse to launch it.
  postflight do
    system_command "/usr/bin/xattr",
                   args: ["-dr", "com.apple.quarantine", "#{appdir}/Mumble.app"]
  end

  zap trash: [
    "~/Library/Preferences/net.sourceforge.mumble.Mumble.plist",
    "~/Library/Application Support/Mumble",
  ]
end
```

Users then install with `brew install --cask d3adb5/mumble/mumble-fork`. This requires publishing the
`.dmg` at a stable URL — a GitHub release rather than a workflow artifact, since artifacts expire and are
not downloadable without authentication.

### A formula that builds from source

Nothing built on the user's own machine is ever quarantined, so a formula sidesteps the problem entirely
rather than working around it. It also removes the need to publish a `.dmg` at all. The cost is that
users compile Mumble, though its dependencies all have Homebrew bottles:

```ruby
class MumbleFork < Formula
  desc "Low-latency, high quality voice chat"
  homepage "https://github.com/d3adb5/mumble"
  url "https://github.com/d3adb5/mumble.git", using: :git, branch: "master"
  version "1.7.0"

  depends_on "cmake" => :build
  depends_on "ninja" => :build
  depends_on "rust" => :build
  depends_on "opus"
  depends_on "openssl@3"
  depends_on "poco"
  depends_on "protobuf"
  depends_on "qt"

  def install
    system "cmake", "-S", ".", "-B", "build", "-G", "Ninja",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-Dserver=OFF", "-Dtests=OFF", "-Doverlay=OFF",
                    "-Ddeepfilternet=ON",
                    *std_cmake_args
    system "cmake", "--build", "build"
    prefix.install "build/Mumble.app"
  end
end
```

### The option that needs no tap at all

For a single machine, downloading with `curl` never sets the flag in the first place:

```
curl -LO https://github.com/d3adb5/mumble/releases/download/v1.7.123/Mumble-1.7.123.dmg
```

and if an image was fetched with a browser, `xattr -dr com.apple.quarantine /Applications/Mumble.app`
after copying it over has the same effect as the cask's `postflight` block.

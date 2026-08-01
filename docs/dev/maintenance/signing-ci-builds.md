# Signing the CI client builds

The client builds produced by `.github/workflows/build.yml` are installable as they are, but neither
Windows nor macOS considers them trustworthy: both warn the user, in a way that is deliberately hard to
click past, that the software comes from an unidentified developer. Getting rid of that warning requires a
signature from a certificate that the operating system's vendor recognizes, which in turn requires an
identity that has been verified by a certificate authority. This document describes how to obtain such an
identity and how the workflow consumes it.

Signing is entirely optional. Without any of the secrets described below, macOS builds still produce a
`.dmg` (ad-hoc signed, see the last section) and Windows builds still produce an installer.

## macOS

### What to obtain

1. An **Apple Developer Program** membership (<https://developer.apple.com/programs/>), currently 99 USD per
   year. An individual membership is enough; an organization membership additionally requires a D-U-N-S
   number. Free Apple IDs cannot issue the certificate type needed here.
2. A **Developer ID Application** certificate. In Keychain Access, use
   *Certificate Assistant → Request a Certificate From a Certificate Authority* to produce a certificate
   signing request, then upload it under *Certificates, Identifiers & Profiles → Certificates → +* and pick
   *Developer ID Application*. Download the issued certificate and open it, so it lands in your keychain
   next to its private key.
3. Export that keychain entry (the certificate **and** its private key) as a `.p12` file, protected by a
   password of your choosing.
4. An **app-specific password** for notarization, created at <https://appleid.apple.com> under
   *Sign-In and Security → App-Specific Passwords*. Your regular Apple ID password will not work.

### Repository secrets

| Secret | Value |
| --- | --- |
| `MACOS_CERTIFICATE` | The `.p12` file, base64 encoded: `base64 -i certificate.p12 \| pbcopy` |
| `MACOS_CERTIFICATE_PASSWORD` | The password the `.p12` was exported with |
| `MACOS_DEVELOPER_ID` | The identity name without its prefix, e.g. `Jane Doe (A1B2C3D4E5)`. `security find-identity -v -p codesigning` prints the full string as `Developer ID Application: <this>` |
| `MACOS_NOTARIZATION_APPLE_ID` | The Apple ID the membership belongs to |
| `MACOS_NOTARIZATION_PASSWORD` | The app-specific password from step 4 |
| `MACOS_NOTARIZATION_TEAM_ID` | The ten character team ID, shown on the developer portal's membership page (it is also the part in parentheses above) |

The certificate secrets and the notarization secrets are independent. With only the former, the build is
signed but not notarized, which is *not* enough: macOS refuses to launch a Developer ID signed application
it has not seen before unless it carries a notarization ticket. Set both.

### What the workflow does with them

`Import signing certificate` unlocks a throwaway keychain holding the certificate, `Create disk image` then
passes `--developer-id` to `macx/scripts/osxdist.py` instead of `--ad-hoc-sign`, and `Notarize disk image`
submits the result to Apple and staples the returned ticket onto it.

## Windows

Windows is not a matter of adding a key to the repository. Since June 2023 the CA/Browser Forum baseline
requirements oblige certificate authorities to keep code signing private keys on certified hardware, so a
plain `.pfx` file for a publicly trusted certificate is no longer something you can buy. What you sign with
instead is a *service*, and what goes into the repository secrets are its credentials.

The realistic options are:

- **Azure Trusted Signing** — the cheapest at roughly 10 USD per month. Individual (rather than
  organization) identity validation is available; organizations need three years of verifiable history.
- **SignPath.io** — free for open source projects under their Foundation tier, and what upstream Mumble
  uses. Note that `.github/actions/sign-binaries/action.yml` hardcodes upstream's `organization-id` and
  `project-slug`, so a fork has to replace those with its own before the existing
  `SIGN_WINDOWS_INSTALLER` workflow input does anything useful.
- **Certum Open Source Code Signing** — around 100 EUR per year, issued onto a hardware token that is
  posted to you. Being physical, it does not lend itself to unattended CI.
- **DigiCert KeyLocker** or **SSL.com eSigner** — full featured, priced for organizations.

Whichever is chosen, note that an OV certificate does not immediately silence SmartScreen: reputation is
built up over downloads, so early users still see a warning. An EV certificate is trusted on first sight.

## What an unsigned build looks like to a user

The macOS image is ad-hoc signed, i.e. signed with no identity at all. That is what lets the binaries run on
Apple Silicon, which refuses to execute code carrying no signature whatsoever, but it tells Gatekeeper
nothing about who produced them. A user who downloads such an image gets *"Mumble.app is damaged and can't
be opened"* on first launch, because the download carried a quarantine flag. They can get past it with
*Control-click → Open*, by allowing the application under *System Settings → Privacy & Security*, or by
clearing the flag themselves:

```
xattr -dr com.apple.quarantine /Applications/Mumble.app
```

That is a reasonable thing to ask of a developer testing a CI build, and an unreasonable thing to ask of
anyone else — which is the point at which the certificates above become worth their price.

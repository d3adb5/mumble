# Webcam sharing: handoff

Written 2026-10-26 when the session that built the desktop side ("mumble-9f") ran out of room.
Start here to continue it.

## What it is

End-to-end encrypted webcam sharing between Mumble (desktop) and Mumla (Android) through a
MediaMTX relay. The protocol is **v1**. The spec is owned by the Mumla side and lives at
`~/projects/mumla/docs/design/webcam-share.md`, which is authoritative. Read it before changing
anything that goes over the wire:

- **Signalling:** `PluginDataTransmission` to single sessions, data ids `webcam/<type>`, JSON.
- **Secrets:** stream URLs, read login and content key travel only in a box sealed to the
  viewer's X25519 key (HKDF-SHA256 → AES-256-GCM), so Murmur relays them without learning them.
- **Video:** H.264 constrained baseline, every slice NAL encrypted (AES-256-GCM, AAD
  `hdr || kid`, a 0x80 stop byte, then emulation prevention). RTSPS to MediaMTX on a fresh
  random `mcam/<32 hex>` path per share.
- **Revoking:** a revoke, and any viewer leaving, moves the stream to a new path, key and kid.
  The camera opens only while someone watches (setting, on by default).
- **Relay logins:** the relay allows no anonymous read (secretlab 25eb481). A viewer reads with
  its own relay login (the "Your login" setting, the same one that shares); a read login in
  the grant still wins. "Watch camera" asks for a login when none is set. Watching never needs
  a camera or a share. The mtx-gate (`~/projects/mtx-gate/DESIGN.md`) adds per-viewer tickets
  in v1.1 grants; not implemented yet.

The peer sessions were `mumla-f5` (Android, spec owner) and `retroid-bb` (relayed the user's
requirements and took status reports). They may be gone. The spec and the git logs carry
everything they agreed.

## Done (master, not pushed; the user pushes)

| commit | what |
| --- | --- |
| c044d0d36 | `WebcamCrypto.h`: per-NAL AES-GCM, EPB, AU framing, key ring, X25519 key wrap, plus tests |
| 247e76004 | `WebcamProtocol.h`: the v1 messages and the sealed grant/rekey, plus tests |
| b53aa969f | `WebcamMode.h`: camera mode choice (ported from teevee-camera), presets, plus tests |
| 8f96cea03 | explicit GCM tag-length casts (MSVC) |
| a5886f22a | `WebcamStream.*`: GStreamer publisher and viewer; `webcam` CMake option; CI deps; .deb Recommends |
| 24184110f | `Webcam.*` (manager), `WebcamWindow.*` (floating viewer), `WebcamConfig.*` (settings page), menus, tree icon, settings |
| 5c69cadaf | viewer reads MediaMTX's RTSPS as plain RTP/AVP (it offers SRTP/MIKEY and refuses GStreamer's SETUP) |

Tests: `TestWebcamCrypto`, `TestWebcamProtocol` and `TestWebcamMode`, all passing (31/31 in
ctest). They include the RFC 7748, RFC 5869 and spec vectors, cross-checked against Python's
`cryptography`.

## Measured

- **Scratch harness, no relay:** encrypt → rtph264pay (MTU 400, FU-A) → depay → decrypt →
  decode. 150/150 frames with OpenH264 and with VA-API, and no slice in the clear.
- **Local MediaMTX v1.21.3, test pattern:** 168/168 received units carried every encrypted
  slice byte-identical, and all of them decrypted and decoded. mumla-f5 measured the same from
  Mumla, including 16 KB IDRs over FU-A and SPS/PPS prepended to every IDR.
- **Local MediaMTX, Mumble's own classes:** `WebcamPublisher` with the C920 (864x480 at 24 fps,
  MJPG/raw chosen by `WebcamMode`, OpenH264) and `WebcamViewerStream` produced 172 frames in
  8 s. No error text contained the path.
- **The whole client, 2026-10-26** (Xvfb, a local `mumble-server`, three Mumble instances, the
  C920 through PipeWire, local relay): share-state and the tree icon (also for a user who joins
  later), the ask dialog, allow, deny ("declined"), picture, revoke, leave by Escape and by the
  window's close button, a revoke moving the stream for the remaining viewer (back in about 6 s),
  share stop closing the window, two shares from one camera at once, and the camera going idle
  when nobody watches. No log contained a path, URL or login.
- **Interop, desktop shares and the RP6 watches** (Mumla viewer, Wi-Fi, local relay on the LAN
  address, 2026-10-26): indicator, request, allow, decoded live picture (864x480, pillarboxed,
  not squashed), following the move after another viewer's revoke (about 4.5 s gap), revoke
  (keys wiped), deny, leave (the desktop logged it), re-watch and share stop. For a phone, the
  relay setting must be the LAN address, not 127.0.0.1: the grant carries it.
- **Interop, the Flip7 shares and the desktop watches** (Mumla's Camera2 sender, 480x640 portrait
  at 24 fps, Exynos encoder, local relay, 2026-10-26): request, allow, decode at good quality,
  upright and unmirrored (after a fix on Mumla's side), front/rear flips inside the same stream
  with no reconnect, a second viewer's revoke (the desktop followed the move in about 1-2 s),
  leave, re-watch on a fresh path and stop. A mid-share resolution change (864x480 to 480x640 and
  back, new SPS before the next IDR) was also checked with a scratch harness through the same
  viewer chain: decoding follows it without a restart.

## Not verified yet, in the order to do it

1. **Test clients must be isolated.** Run extra Mumble instances with their own `HOME`,
   `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME` and `XDG_RUNTIME_DIR` (keeping
   `PIPEWIRE_RUNTIME_DIR`, `PULSE_RUNTIME_PATH` and `DBUS_SESSION_BUS_ADDRESS` on the real ones).
   `-m` alone is not enough: a starting Mumble deletes and re-creates
   `$XDG_RUNTIME_DIR/MumbleSocket` and takes the running Mumble's RPC socket.
2. **The settings page.** `WebcamConfig.cpp` has not been driven through the UI; the UI tests
   set the camera options in `mumble_settings.json` instead.
3. **Production** (`rtsps://stream.d3adb5.ca:8322`, Let's Encrypt, no anonymous read).
   Publishing and watching need the user's own `beaver` login; ask the user for it and never commit it.
   Use production for the real-network checks only.
4. **Windows/macOS.** The option is OFF there because the static dependency bundles have no
   GStreamer. Enabling it means adding GStreamer to those bundles (and the device-monitor source
   path in `makeSource()`, which is untested).
5. **Optional:** a WHEP read path (`pickUrl()` only takes `rtsps://` today); a global shortcut to
   toggle sharing; a self-preview.

## Dependencies

- **Build:** GStreamer (`gstreamer-1.0`, `-app`, `-video`, `-sdp`) and `gio-2.0`, through
  pkg-config. OpenSSL and nlohmann_json were already there.
- **Runtime:**
  - required: gst-plugins-good (v4l2src, rtspsrc, rtph264depay, jpegdec), gst-plugins-bad
    (h264parse, openh264), gst-rtsp-server (rtspclientsink), glib-networking (TLS);
  - optional: gst-plugins-ugly (x264enc), gst-plugin-va (VA-API) and gst-libav.
- **Packaging:** `~/projects/packages/mumble-git/PKGBUILD` already has these and `-Dwebcam=ON`,
  uncommitted, for the user. CI's Ubuntu .deb and CodeQL jobs install
  `libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev`.

## Local test relay

`~/projects/webcam-test-mtx/`: the official MediaMTX v1.21.3 release (checksum verified), with
`mediamtx.yml`, a test CA (`ca.crt`) and a server cert for localhost, 127.0.0.1 and
192.168.1.237.

- **Start it:** `cd ~/projects/webcam-test-mtx && ./mediamtx mediamtx.yml`. **The user starts
  it.** Agents' auto mode is not allowed to run the downloaded binary; don't work around that.
- **Listeners:** RTSPS only, on `:18322` (`rtspEncryption: strict`, TCP).
- **Logins:** `desk` publishes for the desktop and `mumla` for Mumla, each limited to
  `~^mcam/[0-9a-f]{32}$`. Read is anonymous on the same regex (production no longer is; to
  mirror it, give `desk` and `mumla` read on that regex and drop `any`). Passwords are in the
  config file; read them from there and never print them.
- **In Mumble:** Settings → Camera → Relay `rtsps://127.0.0.1:18322`, your login `desk`,
  Trusted CA `~/projects/webcam-test-mtx/ca.crt`.

## Privacy rules (the user's, binding)

1. Publish only spec-encrypted streams to production: no cleartext test patterns, not even one
   frame. Plaintext tests go to the local relay only.
2. Use a fresh random 128-bit path for every share. Never reuse or hard-code one.
3. Paths, keys, grants and logins never go into logs, debug output, commits or peer messages.
   (The client redacts them from GStreamer errors; keep it that way.)
4. Unpublish as soon as a test ends. Leave nothing running on production. Keep tests short.

## Gotchas found on the way

- moc cannot parse a raw string literal that contains `"`. Spell such regexes out.
- GLib's headers must be included before Qt's, because GLib uses `signals` as a name.
- This machine has no x264enc and no avdec_h264. openh264 and vah264enc are there, and
  decodebin finds a decoder.
- `rtph264depay` in AVC output moves SPS/PPS into codec_data, so don't expect them in-band
  after it.
- The `User.h` `-Wsfinae-incomplete` warning under GCC 15 was already there before this work.

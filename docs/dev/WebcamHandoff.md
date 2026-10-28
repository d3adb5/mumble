# Webcam sharing: handoff

Written 2026-10-26 when the session that built the desktop side ("mumble-9f") ran out of room.
Start here to continue it.

## Status 2026-10-27 (read first)

- **Unpushed on master:** 4cc882209, ddf38bd77, 6a9d6b027, 5775871a7 (table below). **The user
  pushes master and rebuilds mumble-git.** The PKGBUILD in `~/projects/packages/mumble-git` is
  the user's to commit; agents don't touch it. The user's installed Mumble was 256ded1bc (v1):
  it watches without a login, so it cannot watch on production, and it publishes to port 554
  when the relay setting has no port. Both are fixed only once the new build is installed.
- **Production bug (fixed in 5775871a7):** the user's relay setting is `rtsps://stream.d3adb5.ca`
  without a port. GStreamer resolves that to 554 (not 322: `GstRtsp.rtsp_url_parse` says so);
  production listens on 8322 (554 and 322 closed). The camera opened, the publish could not
  connect, the share stopped ("camera light flashed once"), and grants carried the portless URL.
  Rule agreed with Mumla (cf845e7) and in the spec: a portless relay means 8322, every grant URL
  carries a port; normalised on save and on use (`withDefaultPort`).
- **Preview (5775871a7):** see "Preview" below. Production can now be retried by the user.
- **Next:** the end-to-end run through the gated relay waits for the user's go (coordinator
  `retroid-fc`), together with Mumla's plan (`~/projects/mumla` HANDOVER.md). Untested so far:
  a picture through MediaMTX with the gate enforcing for both directions, 410, the 45 s retry
  window running out, a MediaMTX 401 → `open`, a move with two viewers, smooth preview motion
  with a real camera (the PipeWire test camera stalls; see Gotchas).

### How to test (isolated, never the user's Mumble)

The user's real Mumble is often on a call: never take over its socket, config or Murmur.
- **Clients:** `isolated.sh <binary> <profile dir> mumble://Name@127.0.0.1:<port>`: a wrapper
  that sets its own `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`,
  `XDG_RUNTIME_DIR` and keeps `PIPEWIRE_RUNTIME_DIR`, `PULSE_RUNTIME_PATH`,
  `DBUS_SESSION_BUS_ADDRESS` real, then runs `<binary> -m`. Write the webcam keys into
  `<profile>/cfg/Mumble/Mumble/mumble_settings.json` (category `webcam`) before starting; read
  passwords from files, never print them. Running the installed 256ded1bc on a profile drops
  the v1.1-only keys (`webcam_gate_url`) when it exits.
- **Server and display:** own `mumble-server -ini <scratch ini>` on 127.0.0.1 and an own
  `Xvfb :98`. First start: cancel the certificate wizard (Cancel button), accept the server
  certificate (Yes), then copy that client's `mumble.sqlite` to the other profile so it trusts
  the server too.
- **Driving the UI:** QMenus ignore xdotool clicks on the menu bar and accelerators inside
  menus: open with `alt+s` (Self) / `alt+o` (Configure) and pick with Down/Return; context menus
  open with a right click after `windowfocus`, and their items take clicks. Capture with
  `xwd -id <win> | magick`, never ImageMagick `import` (its server grab closes popups).
- **Camera:** use the real webcam only if no app holds it (Discord often does), and never
  capture a viewer window fed by it: it shows the user. Otherwise a PipeWire test camera:
  `gst-launch-1.0 videotestsrc is-live=true pattern=ball ! video/x-raw,format=YUY2,width=1280,height=720,framerate=30/1 ! pipewiresink mode=provide client-name=GateTestCam stream-properties="props,media.class=Video/Source,node.description=GateTestCam,node.name=GateTestCam"`
  and set `webcam_device` to `GateTestCam`. Stop it afterwards (it shows up in the user's
  PipeWire graph).
- **Gate:** build `~/projects/mtx-gate` yourself (`go build -o <scratch>/mtx-gate .`), run it
  `-enforce` on loopback ports of your own with throwaway owners (a password file per owner in a
  scratch dir), and delete them afterwards. A small TLS proxy that logs only method, masked
  route and status shows the call sequence. MediaMTX (and any gated test relay) is the user's
  or the coordinator's to start; with none, a silent TCP listener stands in for it.

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
  the grant still wins. The own login goes only to a grant URL on the viewer's own relay
  (same host and port as the Relay setting), so a sender can't collect viewers' logins by
  pointing a grant elsewhere. "Watch camera" asks for the relay and a login when either is
  unset. Watching never needs a camera or a share.
- **The gate (v1.1, `~/projects/mtx-gate/DESIGN.md`):** the grant and rekey boxes carry
  `gate: {id, secret}`, never a URL. The gate API is `https://<own relay host>/gate/v1`, or the
  "Relay gate" setting (HTTPS only), and only the user's own login goes there
  (`WebcamGate`, rules in `WebcamGateApi.h`). Sender: PUT the share at start, every 30 s and
  before every publish connection; one ticket per grant; a move PUTs the new path, then
  DELETEs the old one and re-tickets the remaining viewers in a rekey with urls; a 201 on a
  renewal re-tickets everyone in a rekey without urls; stop DELETEs the share. Viewer: redeem
  before every connection; 401 asks for the login; 410 sends watch-request again; 403/404/429
  retry for 45 s; a MediaMTX 401 after a good redeem calls `open` to tell a bad login apart.
  No gate (production today): anything without `X-Mtx-Gate`, a 2xx without JSON, or a timeout,
  and the client behaves as in v1, without tickets.

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
| 4cc882209 | viewers read with their own relay login; "Watch camera" asks for one |
| ddf38bd77 | the own login goes only to a grant URL on the viewer's own relay (host:port) |
| 6a9d6b027 | protocol v1.1: gate tickets (`WebcamGate*`, `TestWebcamGate`), "Relay gate" setting |
| 5775871a7 | portless relay means 8322 (the 554 production bug); clearer publish errors; sender preview |

Tests: `TestWebcamCrypto`, `TestWebcamProtocol`, `TestWebcamMode` and `TestWebcamGate`, all
passing on 2026-10-27 (25, 28, 22, 20 cases). They include the RFC 7748, RFC 5869 and spec vectors, cross-checked against Python's
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
5. **The gate with a real relay.** Tested 2026-10-27 against mtx-gate 8f1a1c2 (`-enforce`,
   throwaway owners) with a silent stand-in for MediaMTX: claim, renewals, ticket, PUT before
   publish, redeem per connection, gate restart (201 → new ticket → redeem), bad login
   (401 → login dialog → saved), revoke (ticket DELETE, new PUT, old DELETE), no gate (v1).
   Still open: a picture through the gated relay, 410, the 45 s retry window, a MediaMTX 401
   → `open`, and a move with two viewers. The gated relay is the user's to start.
6. **Optional:** a WHEP read path (`pickUrl()` only takes `rtsps://` today); a global shortcut to
   toggle sharing.

## Preview (2026-10-27)

The sender sees its own camera, locally only. The publish pipeline tees the scaled picture
before the encoder into a leaky queue and a small RGBx appsink (`addPreviewBranch`, at most
640 px wide, `previewSize`), so the preview never reaches the relay and never holds the encoder
back. "Your camera (only you see this)" opens when sharing starts (setting "Show my camera
while I share it", on by default) and from Self → Show Camera Preview; with the camera opened
on demand it says the camera is off until someone watches. The settings page has a Preview
button: `WebcamCameraPreview`, the same camera chain without encoder or relay, at the chosen
quality; leaving the page lets the camera go.

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

(Since 2026-10-27 there is also a gated variant: `mediamtx-gate.yml` behind mtx-gate, RTSPS
`rtsps://127.0.0.1:18322` via the gate's proxy, gate API `https://127.0.0.1:19443/gate/v1`
(set "Relay gate" to it), owners `desk`/`mumla` with passwords in `gate-passwords/`. Started by
the user or the coordinator, not by agents.)

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

- **A relay URL without a port:** GStreamer's RTSP client dials 554 for `rtsps://host`, where
  MediaMTX does not listen (it uses 8322). That broke production sharing on 2026-10-27: the
  camera opened, the publish could not connect, the share stopped. Relay settings and grant URLs
  are now completed with 8322 (`withDefaultPort`); grants always carry a port (spec rule).
- The PipeWire test camera (`pipewiresink mode=provide`) stalls after ~18 frames for any
  consumer: fine to see that a picture arrives, useless to judge motion. Test pipeline shapes
  with `videotestsrc is-live=true` in gst-launch instead.

- moc cannot parse a raw string literal that contains `"`. Spell such regexes out.
- GLib's headers must be included before Qt's, because GLib uses `signals` as a name.
- This machine has no x264enc and no avdec_h264. openh264 and vah264enc are there, and
  decodebin finds a decoder.
- `rtph264depay` in AVC output moves SPS/PPS into codec_data, so don't expect them in-band
  after it.
- The `User.h` `-Wsfinae-incomplete` warning under GCC 15 was already there before this work.

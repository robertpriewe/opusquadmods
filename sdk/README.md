# OQ Mods SDK 1.7.0 (API version 1)

**Start with [API-REFERENCE.md](API-REFERENCE.md)**: every API call, who may call it and
from where, the mod folder files and the symbols the firmware exports.

What is in this folder:

| Folder | Contents |
|---|---|
| `include/` | The headers a mod builds against (`oqmod.h` and its companions) |
| `mods/` | Source of the feature mods: 4-Deck Waveforms, Side Screen Browser, Stems, Light Mode, Quick USB, Beat FX Sidebar, FX Frequency, FX Knob Selector, Slip Mode, Playlist Editing, Hide Track Name, Continuous Zone Playback, Side Screen Waveforms (Legacy), plus the diagnostics tools |
| `reference-mods/` | Source of Party Mode, Gate Cues, Jog Ring Brightness, Bluetooth Waveform and Pro DJ Link |
| `examples/` | Small example mods to start from (BPM difference, set list, master meter, party mode) |
| `tests/` | Host-side tests for the mod engines |
| `build.sh`, `Dockerfile` | Build any mod folder into `build/<name>/mod.so` (needs Docker) |

New in 1.7 (firmware R121): `ring_brightness(percent)` dims both jog rings; see
`reference-mods/ring-brightness`.

New in 1.6 (R118): a **read-only master feed** for mods, a copy of the final stereo mix
for meters, analysis or recording. See [MASTER-AUDIO.md](MASTER-AUDIO.md) and
`examples/master-meter`.


New in 1.5: a mod folder can carry its own `menu.title`, `version.txt` and
`description.txt` (R116), and `choices.order` to reorder choice buttons without changing
saved values (R117); see [PUBLIC-MENU.md](PUBLIC-MENU.md). Use plain ASCII in those files.
The firmware also exports `opusmod_title_hidden(deck)` (R115).

## Earlier releases


SDK 1.4 / R114 added scrollable mod-option pages and USB-owned Runtime Diagnostics and LED
Diagnostics tools, alongside the existing twenty feature add-ons. The API-v1
prefix is unchanged. New tools check the appended fields before using them;
older API-v1 binaries keep their existing field offsets.

Register named menu actions during init with `menu_action(label, callback)`
(maximum nine). They are queued from touch/knob input and executed only while
your mod is enabled, on the settings tick. `menu_action_label(index, label)`
can rename an action safely without changing its index. Long option/action/status
pages now scroll by touch or wheel; main-knob action focus is kept visible.

The shared `diagnostic_save()` service queues a runtime report; use
`diagnostic_status(out, capacity)` for a bounded system/save-status snapshot.
See `mods/diagnostics` and `mods/led-diagnostics` for complete examples. A regular
`diagnostics.only` file in an add-on folder opts that package into startup gating
by `enable_diagnostics.txt` on a mounted USB. This is a menu visibility convention,
not a security boundary: add-ons are trusted native code.

**External-engine branch.** This copy adds append-only
reserved overlay surfaces, USB snapshots/actions, a retained native loader registry,
and USB-owned FX Frequency, Quick USB, ZONE, Light Theme, Smooth Waveforms,
FX Target Knob, Slip, Hide Track Names, Beat FX Sidebar, Legacy Now Playing,
Four-Deck Layout, Side Browser, Playlist Editing and Stems
implementations. It is
not the frozen R112 SDK archive. All twenty feature implementations are now
external in this branch; native primitives remain shared in the foundation.
See [engine extraction](ENGINE-EXTRACTION.md) for what
is implemented, what remains in firmware, and the offline-only validation limits.

SDK 1.2 / firmware R111 introduced named native services, individually selectable option
buttons, USB-folder icons, menu ordering and support for 32 add-ons. The original
API prefix is unchanged, so existing compiled API-v1 mods remain compatible.
SDK 1.2.1 / R112 adds apply acknowledgements, settings generations, atomic USB
preferences and a standalone FPS renderer. Its native clients require service
protocol 2; the public function-table layout is unchanged from SDK 1.2.
See [Native services and migration](NATIVE-SERVICES.md) for the 15 new add-ons,
every property, settings import, and which shared native backends remain in the host.

Write add-on mods for Robs Opus Quad Mods without the firmware or its source. A mod is
a small shared library that the custom firmware loads from the settings USB. It shows up
in the **Mod Menu** by its plain mod name, with its own on/off
switch, its options and its status lines.

## What a mod can do (API v1)

- **Read each deck:** loaded, playing, position, length, BPM, pitch, title, artist, key.
- **Draw a box on the WAVEFORM screen and react to taps in it:** filled and outlined
  rectangles, and text in the deck's own fonts.
- **Keep its own settings** (saved on the USB) and **write log lines** (shown in System
  Info > Save diagnostics).
- **Run a short callback about 30 times a second** (`tick`); it must not block.
- **Options on its page:** on/off rows (`option`), multi-value rows (`choice`) and
  explicit value buttons (`choice_buttons`, R111), up to
  four, saved on the USB; and a button in the Beat FX box (`quick_toggle`).
- **Light the deck's LEDs** (`led_set`, firmware R104 and later): jog ring, jog centre,
  inner circle, deck selectors, the eight hot cue pads, play, cue and the button lights
  (vinyl, master tempo, slip reverse, quantize, loop in/out, ...), each side. A colour
  holds for up to 10 s; refresh it from `tick` to keep it. The deck's own lights come
  back as soon as the mod stops. Names: `OQMOD_LED_*` in the header.
- **Use the screens** (R104): move or resize its overlay box at run time (`set_area`),
  know which screen and deck layout is showing (`layout`), and put a line of text on a
  deck's side screen in place of the track title (`side_text`).
- **Advanced:** hook the deck software's functions (`hook`, `patch`; for one build of
  the deck software only, check `ep136_ok`), and press PLAY on a deck (`deck_play`).

## Master audio (unreleased interface)

For master EQ/audio processing, see [Master audio status and optional extension
contract](MASTER-AUDIO.md). **Current firmware does not provide final-master
samples or a writable master insert.** The new optional header is unreleased SDK
groundwork; discovery safely reports unavailable on R112. It is not a working
master EQ, and the AZ's read-only pre-mixer deck taps are not a substitute.

## Named events (SDK 1.1)

Most mods want to react to what happens on the decks. Rather than comparing deck state
yourself, include [`oqmod_events.h`](include/oqmod_events.h), fill in the handlers you need
and call `oq_events_poll()` from `tick()`:

```c
#include "oqmod_events.h"
static void started(unsigned deck, const struct oqmod_deck *d) { /* deck 1..4 began playing */ }
static const struct oq_events events = { .play = started };
static void tick(void) { oq_events_poll(api, &events); }
```

| Event | When |
|---|---|
| `track_loaded(deck, now)` | a track appears on a deck, or the deck changes to another track |
| `track_unloaded(deck)` | the deck has no track any more |
| `play(deck, now)` / `pause(deck, now)` | the deck starts or stops playing |
| `bpm_changed(deck, old, new)` | the playing BPM moves by 0.05 or more (values x100) |
| `key_changed(deck, key)` | the key shown for the track changes |
| `near_end(deck, now)` | the track passes its last `near_end_seconds` |
| `layout_changed(old, new)` | the screen layout changes (see `layout()`) |

The first poll reports what is already loaded and playing. Also in that header:
`oq_track_path(deck, buf, size)`, the full path of the audio file on a deck (tracks from a
rekordbox library on USB), and `oq_mod_dir(buf, size)`, the mod's own folder on the USB,
for files it writes. `examples/setlist` uses all of these.

These work on the current firmware: no update is needed.

## Raw hooks

`hook()` and `patch()` take addresses inside the deck software (EP136 1.32): the mods in
`reference-mods/` use them for things the API doesn't cover yet. They are fine to use and share,
with two things to keep in mind: the addresses only fit this deck-software version (check
`ep136_ok()`), and a mistake crashes the deck. When something becomes a common need, it can
also become a **named hook** in the firmware and the header.

## The mods in `mods/`

The 12 native-service clients, independent FPS renderer, and extracted FX Frequency
and Quick USB engines live in `mods/`,
catalogued in `native-mods.json`. The generator leaves custom implementations alone.
Their native host boundaries and all properties are described in
[NATIVE-SERVICES.md](NATIVE-SERVICES.md); [HOOK-NOTES.md](HOOK-NOTES.md) records
the integration and extension rules.

Standalone reference implementations from the existing external mods are in
`reference-mods/`:
- `bt-wave`: Bluetooth Waveform and the live Bluetooth deck (records the phone audio, serves
  it to the deck as a growing track, makes the deck analyse and redraw it live, and routes
  Bluetooth on deck 3/4 to that track).
- `gated-cues`: Gated Hot Cues.
- `prodjlink`: Pro DJ Link (an XDJ/CDJ on the network as deck 3).
- `party`: the six jog-ring colour effects and four speeds.

TYTO's separate renderer/host project is not duplicated into the public SDK.
Its binary is unchanged in the R111 test bundle. Its icon/order pack is supplied
in `assets/tyto-sidescreen/`.

`sh tests/run.sh` runs SDK-only event, deferred service-client and FPS drawing tests with a local C
compiler, without firmware or keys. Linux additionally tests `/proc/self/maps`
mod-folder lookup; that target-specific check is not expected to succeed on macOS.
The larger private host suite separately tests native adapters, real add-on loading,
menus and the FUSE live-track path.

## Coming in the firmware (named hooks)

From the live Bluetooth deck work, planned for the next firmware:
- **input source changed**: a deck's channel switched between the deck and Bluetooth;
- **load a file on a deck** by its path;
- **live Bluetooth audio** as it arrives (PCM, for meters, recording, analysis).

## Compatibility

A mod built with this header refuses to load on older firmware (it checks `api->size`).
`examples/party-mode` shows the LED calls and `choice`; `examples/setlist` shows the
named events.
Everything is in [`include/oqmod.h`](include/oqmod.h). The API grows on request: ask for
what your mod needs.

## Build

You need Docker. From this folder:

    ./build.sh examples/bpm-diff

The first run builds the SDK image, a stock Debian ARM64 cross compiler. The result is
`examples/bpm-diff/build/bpm-diff/mod.so`. The script refuses mods that don't export
`oqmod_init`, or that use C library functions newer than the deck's.

## Install and run

1. Copy the `bpm-diff` folder to `MODS/bpm-diff/` on the settings USB (the one with
   `OPUSMOD_SETTINGS.cfg`).
2. Restart the deck (or use **Reload Mods** in the diagnostics/maintenance section of
   the Mod Menu). Every mod in `MODS/` is loaded when the app starts; there is no signing.
3. The mod is listed in the Mod Menu by its plain name. Switch it on (mods start off).

To try a new version, copy the new `mod.so` over the old one and use the central
diagnostics/maintenance **Reload Mods** action. Individual mod pages contain no reload
button. No firmware update is needed.

## Writing a mod

Start from `examples/bpm-diff/mod.c`. The rules:
- **Export one function:**

      int oqmod_init(const struct oqmod_api *api, struct oqmod_info *info)

  Check `api->version` and `api->size`, keep `api`, then fill in `info`: name,
  description, version, overlay area, callbacks. Return 0.
- **Keep `tick()` short.** On the current firmware it runs on EP136's GUI message
  thread as part of the settings poll. Never block or sleep in it. Rendering and
  touches run on the separate overlay thread.
- **`draw()` only paints, inside your area.** Call `api->redraw()` when what you show
  changes, rather than repainting all the time.
- **Settings and log calls only work inside callbacks.**
- **A crash in a mod stops the app.** The deck then restarts in stock mode until it is
  powered off and on; the firmware's `OPUS-CRASH.txt` on the USB tells you where it
  crashed. Check pointers and lengths.

## Install with the patcher

Planned: the OQ Mods Patcher will list available mods and copy the chosen ones into
`MODS/`.

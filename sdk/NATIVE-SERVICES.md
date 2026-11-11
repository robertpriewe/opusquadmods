# Named native services — SDK 1.2.1 / firmware R112

This source copy is an unreleased SDK 1.3 extraction branch. FX Frequency,
Quick USB, ZONE, Light Theme, Smooth Waveforms, FX Target Knob, Slip, Hide Track Names, Beat FX Sidebar and Legacy Now Playing own their implementations and require `engine_external=1`, an explicit
engine claim and the new SDK interfaces. Four-Deck Layout, Side Browser,
Playlist Editing and Stems now also own their implementations. No settings-only
feature clients remain in this development bundle. The frozen
R112 archive is unchanged. See [ENGINE-EXTRACTION.md](ENGINE-EXTRACTION.md).

## What moved, and what did not

The 15 add-ons in `mods/` replace the remaining built-in feature **menu
entries, switches, option definitions, activation policy and per-feature settings**.
Each is a separately compiled `MODS/<folder>/mod.so` with `icon.pgm` and `menu.order`.
The five already-external mods keep their implementations: TYTO now playing,
Gated Hot Cues, Party Mode, Pro DJ Link and Bluetooth Waveform.

The FPS badge's formatting, geometry, theme accents and redraw/drawing implementation
now live entirely in its add-on; the host supplies only read-only measurement data.
A legacy host badge path remains available for older compiled clients, but the new
FPS add-on does not activate or depend on it.

Feature policy, UI/rendering, browser state, playlist transactions and the stems
decoder/processing now live in USB libraries. The foundation retains version-checked
JUCE/input/native loader adapters, passive metadata/catalog caches, the side-display
serializer, and early typed audio passthrough hooks. A new native/controller/audio
primitive can still require firmware; this is not an unlimited hardware abstraction.
Replacing an existing feature implementation requires a software restart, not
live unloading. Hardware equivalence remains unverified for these extractions.

The core must retain the R110 Info/LinkInfo/EditRating visibility protection. Every
external FX window must yield to a native popup; moving the native widget alone
does not put an independent X11 window behind a JUCE component.

## API / compatibility

API version remains **1**; new function pointers are appended to `oqmod_api`.
The layout of the original API prefix and of `oqmod_info` is unchanged. Old
compiled API-v1 add-ons still work. Current service clients require R112 protocol 2:
check `api->size >= sizeof(struct oqmod_api)` before accessing the new tail.

* `native_bind(service)` — call during init; returns 1 for a supported, unowned
  service. One service per add-on, one owning add-on per writable service. The
  read-only `telemetry` service permits multiple readers. A failed init
  releases its claim. Unknown/duplicate claims fail without replacing anything.
* `native_get(property, fallback)` — read the bound service's live property.
* `native_get("protocol", 0)` — require 2 for the current helper/client behavior.
* `native_get("generation", 0)` — the settings epoch; changes when settings are
  re-imported or authorization is lost. An older queued request must not cross it.
* `native_get("applied.<property>", 0)` — changes when the GUI consumes a queued
  write of that property, including a no-op write. This acknowledges consumption,
  not a framebuffer/audio result. It is a wrapping counter, not a request ID.
* `native_set(property, value)` — validate a property/value, queue it for EP136's
  message thread, return 1 if accepted. This is an accepted request, not a promise
  of a visible result. There is no JUCE pointer in this interface.
* `native_get("initial.<property>", fallback)` — original authorized USB global
  preference captured before external activation; use only to seed a missing
  per-add-on preference. Do not overwrite an existing add-on setting with it.
* `default_enabled(on)` — init only. A saved `on.<folder>=0` or `=1` always wins.
* `menu_order(priority)` — init only, 0–2000; smaller numbers appear first.
* `choice_buttons(...)` — like `choice()`, but every value is an individual
  touchable button. Up to four options, each with 2–8 values. Use the menu tests
  to ensure a combination fits the right pane; the shipped clients fit at 1280x800.

Use native configuration from init, enabled or tick callbacks. Writes are queued
even if made by an overlay callback; never call fixed native functions from the
overlay thread. The existing callback-context settings rule still applies.
`tick()` on this firmware is driven by EP136's GUI-message-thread settings poll,
roughly every 30 ms: it must be short and must not block. `draw()` and `touch()`
run on the separate X11 overlay thread.

`oqmod_native_feature.h` is an optional reusable client. It registers the options,
imports missing defaults, applies activation/preferences and mirrors changes made
with main-screen tabs or FX range buttons back to the add-on's settings. It waits
for write consumption before treating a stale read-back as a real native user edit.
On a new settings epoch it reapplies saved preferences instead of overwriting them
with reset native values. Failed requests retry without replacing saved choices.

Read-only measurements, available from any bound service (or the shareable
`telemetry` domain): `waveform_fps_x10` (native repaint rate ×10; zero unknown)
and `light_theme` (0 dark, 1 light). `telemetry` accepts no writes. The standalone
FPS mod is a small complete example of external rendering without native hooks.

## Property reference

All writable services have `enabled` (0/1). `available` is used during binding.
Only the properties in this table are accepted; invalid names/ranges are refused.

| Service | Extra properties | Values |
| --- | --- | --- |
| `waveform-layout` | `layout`, `order`, `overviews` | layout 0=4 DECK, 1=2/4 DECK, 2=2 DECK; order 0=3/1/2/4, 1=1/2/3/4; overviews 0/1 |
| `side-browser` | `navigation` | 0=Deck Select navigation, 1=Track Search navigation, 2=Beat Loop wheels |
| `quick-usb` | — | named native USB browser shortcuts |
| `light-theme` | `palette` | 0=Original Light, 1=Blue/Gray |
| `smooth-waveforms` | `mode` | 0=Stock, 1=Smooth 30, 2=Experimental 60, 3=Native Height 30 |
| `fx-frequency` | `selection` | 1=LOW, 2=HIGH, 3=ALL |
| `fx-sidebar` | — | native readout and shared FX-control area |
| `fx-target` | `sensitivity` | 1–5 detents, default 3; SHIFT retains effect TIME |
| `slip-mode` | — | per-deck SLIP REVERSE swap |
| `hide-titles` | — | native main/side title privacy hooks |
| `zone-continuous` | — | continue only the ZONE-assigned deck |
| `stems` | — | existing pre-generated-stems backend |
| `playlist-editor` | — | only available in a host built with playlist reorder support |
| `side-now-playing-legacy` | `zoom` | 0=Normal, 1=Wide, 2=Close, 3=Tight; needs side-browser |
| `fps-overlay` | read-only `waveform_fps_x10`, `light_theme` | external badge does not set native `enabled`; its switch belongs to the add-on |

Smooth Waveforms: set `mode` before `enabled=1`. Enabling does not force a mode;
Stock is a valid selection. Disabling restores mode 0 while the add-on retains its
preferred mode. Native Height 30 is the recommended four-deck configuration.

FX Frequency and the Gated Cues quick toggle need Beat FX Sidebar for their main
screen controls. Disabling a service must restore its stock/pass-through behavior;
hooks themselves remain installed until the process restarts.

## Icons and order files

`icon.pgm` is a fixed 48×48 monochrome coverage image. Exact file structure:

```text
P5\n48 48\n255\n
<2304 binary coverage bytes, row-major>
```

Coverage 0 is transparent; 255 uses the menu's theme-aware icon colour. Use black
for the transparent background and white for the strokes when creating the mask.
The loader bounds the size, refuses symlinks, special files, wrong dimensions and
bad headers, and falls back to the puzzle icon. It never invokes an image decoder.
These are original code-generated pictograms, not graphics extracted from firmware.

`menu.order` is optional: an integer 0–2000 plus newline. It also works for an old
compiled mod that does not know about the new API. A successful init can override
it using `menu_order()`. Equal priorities are ordered by folder name without moving
the live mod IDs. The SDK build script copies both files into the build output.

## USB preferences and migration

* `OPUSMOD_SETTINGS.cfg` remains the authorization/compatibility file. No file,
  no native feature activation or Mod Menu. It is not stored on internal flash.
* `MODS/state.txt` contains only each add-on's enabled state (`on.<folder>=0|1`) by
  stable folder name; it is the central state file, not a place for mod options.
* `MODS/<folder>/settings.txt` saves that add-on's options.
* Existing settings are imported only as missing defaults; an explicit saved OFF
  is respected. Native main-screen changes synchronize back to relevant options.
* Settings and state are flushed to same-directory temporary files and atomically
  renamed over the last committed file. Failed commits keep the old file and retry
  only while the same owner USB is present. This is not permission to eject during
  writes or a guarantee against every filesystem/power-loss failure.
* Unchanged settings do not trigger writes. State updates preserve valid enabled flags
  for absent/failed add-ons rather than silently resetting them, and discard unrelated,
  comment and option lines. Oversized, malformed, symlink or special-file inputs are
  bounded/refused, not blindly truncated.
* The host still mirrors applied native state to the global compatibility file
  for older consumers such as TYTO's theme reader. Missing add-ons cannot be
  activated just because an old global flag remains set.
* Existing raw hooks are resident until the deck software restarts. Reload means
  a software restart, not `dlclose()` of a library with live function pointers.
* Removing the authorizing USB pauses resident add-ons as well as native features.
  The same owner directory/device resumes them. Switching to a different settings
  USB needs a software restart to load its libraries; old hooks cannot safely be
  unloaded live. No switches are saved as OFF merely because a stick was removed.

The host supports 32 add-ons. Each has an independent option-request mask;
settings for add-on 9 or later no longer alias/overflow a shared 32-bit option mask.

## Build / test / ship

Run `python3 tools/generate_native_addons.py` after editing the catalog. Build a
client with `./build.sh mods/four-deck`. Copy the resulting whole folder to `MODS/`.
Existing add-ons' icon/order packs are in `assets/`; their source/binaries need not
change just to gain an icon or menu position.

Test the named adapters, state import, option routing, highest index (31), menus,
hitboxes and malformed icons before hardware. Then test the actual feature on the
player: a successful API request or callback is not framebuffer or audio proof.
Preserve a known-good firmware and USB settings backup for rollback.

This SDK/catalog contains no firmware images, native extracted assets, music,
keys, decrypted cabinets or third-party library exports. Do not add them to a
public source repository.

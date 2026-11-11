# Native integration notes and extension rules

Extraction-branch override: FX Frequency no longer uses `native_fx_xy.h` when
the core is compiled with `OQ_EXTERNAL_FX_FREQUENCY`. Its hooks, packet policy,
buttons and state live in `mods/fx-frequency/`. See [ENGINE-EXTRACTION.md](ENGINE-EXTRACTION.md)
for the new popup-aware surface API, USB snapshots/actions and the remaining 4
native engines. Quick USB's renderer is also excluded from the extraction core;
its native USB discovery/navigation bridge is reusable by other add-ons.
ZONE's continuation/autoplay policy is excluded with `OQ_EXTERNAL_ZONE_CONTINUOUS`;
the host keeps read-only early constructor/routing observations for late USB loading.
Light Theme owns its native paint hooks; the host only delegates semantic colours.
Smooth Waveforms owns the timer/paint-clock policy; shared native instrumentation
still measures FPS with no smoothing provider. See the new resident provider APIs.
FX Target Knob's ordering/divider/SHIFT policy and Slip's toggle/held-release/
cleanup/LED policy are USB-owned. Shared native encoder arbitration, absolute ACK,
FX target actions, Slip primitives and short transport leases remain host adapters.

The public configuration reference is [NATIVE-SERVICES.md](NATIVE-SERVICES.md).
This file explains the implementation boundaries and why they matter when adding
new hooks. The R111/R112 migration deliberately retains the native algorithms
already tested on hardware; it does not invent new EP136 addresses.

## Current backends

| Service | Host integration / existing engine | Off behavior |
| --- | --- | --- |
| waveform layout | `wfv_update_hook`, native deck-card visibility and cached geometry repair in `opusmod.c` | stock native layout |
| waveform timing | `render_timing.h`, native timer and native-height rendering | stock repaint/timing mode |
| light theme | `lightmode.c`, semantic fills/glyphs/grid/column colours | byte-preserving native dark drawing |
| FX sidebar | `fx_sidebar_converge`, restore, popup observers and `fx_sidebar_active` | native placement and no separate FX windows |
| FX frequency | `native_fx_xy.h`, guarded mixer receive/serialize policy | original packet/filter behavior |
| FX target knob | `native_fx_target.h`, `fx_shift.h`, `mixer_time_gate.h` | native TIME and FX SELECT |
| Slip | `native_slip.h`, `native_slip_input.h`, `slip_swap.h`, shared LED policy | stock SLIP REVERSE |
| side browser | `browser_runtime.h`, `button_observer.h`, `native_side_load.h`, saved native playlist memory | native Deck Select / Track Search / Beat Loop controls |
| legacy now playing | `deck_view_host.h`, controller side transport, zoom/theme adapters | no legacy takeover |
| quick USB | `native_usb_sources.h` and native Source-model USB identities | no shortcut windows |
| hide titles | `hide_titles.h`, native refresh/visibility adapters | visible native names |
| ZONE continue | `zc_tick` and native playlist order/autoload adapters | stock Continue/Single setting |
| stems | `stems_control.h`, shared `oqstem.so` engine and strip | engine idle, no stem processing |
| playlist editor | `native_playlist_reorder.h`, `playlist_reorder_knob.h`, bundle backup/commit | no reorder interception or writes |
| FPS | read-only `render_timing.h` counters; complete badge in `mods/fps-overlay/mod.c` | add-on window hidden |

Already-external implementations: Gated Hot Cues owns its guarded native cue
hooks; Party owns its colour/effect policy through LED services; Pro DJ Link owns
its protocol logic; Bluetooth Waveform owns its audio decoder hooks/live-track
logic; TYTO owns its host worker and controller renderer module. R111/R112 carry
their R110 binaries unchanged, with separate icon/order assets added by the host.

## How to add a named service

1. Describe the needed observation or action without an EP136 pointer in the API.
   Prefer a small property with an explicit range, units and off behavior.
2. Verify the native integration against the exact executable identity. A matching
   architecture or another model's offset is not sufficient evidence.
3. Add a bounded domain/property entry to the host adapter and a message-thread
   apply path. The request is not applied on the overlay thread.
4. Publish the domain, ranges, dependencies, version floor and limitations in the
   SDK. A service unsupported by an older host must fail cleanly during init.
5. Keep options, defaults, icons and activation policy in an add-on. Preserve an
   explicit saved OFF. Do not use filesystem enumeration order as menu identity.
6. Add tests for invalid values, unbound calls, competing owners, enable/disable,
   removal of the authorizing USB and restoration. Test visible/audio behavior
   separately on the unit; callbacks are not rendering/audio proof.

## ABI and thread rules

- Append to `oqmod_api`; never insert, reorder or resize existing public fields.
  Keep `oqmod_info` unchanged unless a negotiated versioned extension is added.
- Check the table's `size` before touching a newly appended function pointer.
- `native_bind` is a feature ownership protocol, not a security sandbox. An
  arbitrary loaded `.so` runs in-process and can crash or modify the application.
- Native service writes are queued. Only the GUI settings poll mutates native
  objects or the shared compatibility settings. Keep callbacks non-blocking.
- Never `dlclose` a library with installed vtable/inline hooks. Disabling must
  pass through; changing native hook code requires a process restart.
- The 32-add-on limit matches the toggle mask width. Option masks are per add-on;
  do not reintroduce `1u << (mod * option_count + option)`.
- R112 publishes initialized metadata before increasing the atomic visible mod
  count. Do not expose a partially initialized plugin to the overlay thread.
- Wait for protocol-2 `applied.<property>` consumption before syncing native
  read-back. `native_set()` acceptance alone is not an apply acknowledgement.
- Discard queued writes on settings epoch changes. Never replay a previous USB's
  native configuration into a new authorizing USB session.
- Preference saves must preserve unknown/failed add-ons' state. Atomic replace
  and failure injection are tested in the host's plugin_persistence_test.

## Title engine extraction

With `OQ_EXTERNAL_HIDE_TITLES`, native title hooks and eye UI come from the USB
module, not built-in code. The host's waveform callback is a notification after
native layout, not a second layout owner. Title filtering happens inside the
existing stock serializer before its CRC, never by replacing the serializer.
Metadata readers use the shared four-bit mask; only the bound title service may
publish it. UI panels are independently bounded and must yield to native modals.
The historically disabled component scan remains uncalled. Restoring a title
only restores visibility EP136 itself had requested, not a stale empty track.

## Beat FX sidebar extraction

`OQ_EXTERNAL_FX_SIDEBAR` removes built-in compact sidebar geometry and frame drawing.
Shared popup/lifecycle observers remain the single authority for yielding native
and X windows. The provider's native chooser is reparented separately from its
clipped selector; do not "simplify" that into the HELIX-only clipping regression.
The frame consists of small input-transparent panels, not an opaque box over
native BPM/time controls. Never duplicate the shared native update hook in a mod.

## Legacy side-view extraction

`OQ_EXTERNAL_LEGACY_VIEW` removes main-firmware record scheduling and state/grid/
loop presentation. Passive player decoding and the shared ANLZ/database worker
remain common services for stems, privacy, SDK deck readers and other consumers.
The coherent cache lease is TX-only and non-blocking; never acquire its mutex a
second time from the callback. The host carrier checks the original slot and
activation, while the original serializer retains sequence and CRC ownership.
Keep browser/TYTO arbitration untouched. Controller protocol/render primitives
remain firmware capabilities; do not claim the controller code moved to USB.

## R110 visual fix that must survive future extraction

Independent X11 windows always appear above native widgets unless explicitly
hidden. Lowering `BeatFxWidget` to the lowest native child does not lower the
separate border/TIME/frequency/Gate Cues windows. Use the native InfoWidget,
LinkInfoWidget and EditRatingPopupWidget lifetime observers, not only the waveform
PlayingTrackInfoWidget. Observe both virtual and direct base setVisible calls,
guard the original slots, and withdraw pointers before destruction. Publish popup
visibility even while modal panels suspend waveform updates.

The host's `sidebar_test` covers all three layouts, nested panels, deleted and
replaced objects, detachment and stable layout repair. The user confirmed R110's
actual popup result on hardware on 2026-11-07. The external migration still
needs its own hardware regression pass.

## What still needs a firmware update

A new backend property or genuinely new native behavior still needs a host update
until its native implementation is separately extracted behind a more general
stable ABI. R112 does not remove all native feature code from the firmware; it
moves the user-facing feature clients out and exposes the shared adapters. That
distinction prevents a USB policy update from being mistaken for an engine update.

Further extraction should proceed one feature at a time behind passing tests,
starting with independent policies, not by copying the entire preload library
into every mod. Copies would own different globals and compete for the same hook
slots, transport acknowledgements and native objects.

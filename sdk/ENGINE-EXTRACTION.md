# SDK 1.3 development: implementation ownership

This is an unreleased branch. See `../README.md` for the complete inventory and
evidence. All 20 feature implementations are now external in this development
tree. Do not ship these files with R112: the final four need new foundation
bridges, and physical hardware equivalence has not been established.

## Final four: layout, browser, playlist editor and stems

The public `oqmod.h` includes `oqmod_browser.h` and `oqmod_stems.h`; distribute
all three together. Fields are appended; existing API-v1 binary prefixes remain
unchanged. New libraries validate the full sized table and `engine_external`.

### Four-Deck Layout

`layout_provider()` registers a resident singleton. The add-on owns lane/card/
overview/mixer convergence, both lane orders, fast repair and graphics scaling.
The common host owns the native update, visibility and paint dispatch hooks.
`waveform_parts()` copies the validated components; these borrowed GUI pointers
must not escape the message thread. Sized component operations add resize,
repaint, native card refresh and stock card visibility. `waveform_sidebar()`
brokers the separately external sidebar using its original parent bounds; popup
visibility remains authoritative in the common observer.

`oqmod_layout_inputs.layout` retains native numbering **0=4 DECK, 1=2/4 DECK,
2=2 DECK**, unlike the historical waveform observer's 1/2/3 numbering. Native
Height 30 keeps its native 162-pixel detailed drawing area. Paint begin/end are
paired even after disable, and suspension restores borrowed native components.

### Side Browser and shared catalog

`browser_provider()` owns selector holds/replay, Track Search/Beat Loop input,
navigation, per-deck/per-medium bookmarks, plain/rich page construction, query
scheduling and two-step load confirmation. One worker performs catalog work;
only the provider's GUI load pump may call native preflight/dispatch. Disable
closes both screens, invalidates outstanding work and retains held UP ownership.

`browser_services()` supplies copied fixed-width source identities, bounded
read-only catalog queries, the common controller page/record serializer, requested
screen state and guarded native load actions. No database key is returned.
Queries recheck source identity, restrict the native export path and refuse WAL.
The shared library/analysis worker runs independently of this mod, so disabling
browser UI does not stop TYTO/legacy waveform data or native playlist memory.
Load acceptance is not proof of decoder completion. There is no side-browser
playlist editing path.

### Main-screen Playlist Editing

The USB editor contains the established knob hold/turn/drop gesture, row preview,
native library-message hook, ordering planner, native transaction, readback,
rollback, backup and export.pdb mirror. The foundation supplies only passive
native PLAYLIST-position/source snapshots, bounded notices and a GUI cleanup
tick. All original exact EP136 checks, # ascending identity checks and per-boot
backups remain. Authorization is forwarded every GUI tick, including OFF cleanup;
an unclaimed save is cancelled when authorization is withdrawn. A running
transaction must finish/rollback on its original library thread.

### Stems

The USB module includes the GPL azstem OPUS port, MIT/public-domain stb_vorbis,
existing PCM injection/offset/key/tempo handling, file controls, and all main-screen
stem strip/card rendering and touch behavior. LICENSE and NOTICE accompany it.
No cabinet key or firmware executable is included in that source folder.

The foundation installs eleven typed, exact-prologue-checked **passthrough**
audio hooks before main. `stems_audio()` lets the USB engine bind their originals
and atomically publish one complete immutable table only after initialization.
No native audio callback changes until all bindings exist. Position-returning
callbacks retain the native 32-byte AArch64 x8 ABI. Constructors that preceded
USB mounting are handled by the bounded live-unit deck/FGPR identity fallback.
Missing/failed guards keep native audio passthrough. This is **not** the final
master audio hook; the separate master-audio capability still has no backend.

`stems_ui()` publishes only availability/on bits/open state and reads common
layout context. The add-on owns its five small panels and touches. OFF immediately
clears the master injection gate and its panels; resident callbacks continue safe
identity/destructor tracking. There is no live dlclose.

The matched firmware boot wrapper **must remove the old oqstem LD_PRELOAD**.
Running it with the old audio preload makes the new prologue guard refuse the
bridge; never attempt to chain two injectors. Decoders are not built into the
new foundation. The installer also uses a separate RX island page per hook so a
late hook never removes execute permission from an earlier active audio/GUI hook.

### New validation, still not hardware proof

Actual SDK shared libraries are loaded through the real host against simulated
native objects/transport. Tests cover layout/sidebar composition, browser worker/
bookmark/load-lock/confirmation, extracted playlist commit/rollback/backup helpers,
stems panel placement and disable, all eleven audio dispatch signatures, 32-byte
returns, four-deck PCM masks/all-on original/OFF passthrough, lazy deck identity,
and concurrent execution while 64 late hooks are installed. Real side LCD output,
analog audio, startup timing, playlist saves and add-on-only updates on the player
still require physical regression after packaging the matching foundation.

## Independent small panels and title engine

`panel_renderer(draw,touch)` is init-only and mutually exclusive with `set_area`
and reserved surfaces. `panels_set(array,count)` copies up to ten screen-bounded
rectangles. A zero-size entry hides that slot; count zero clears all. Updates are
atomic with respect to geometry readers. Identical geometry does not force redraw.
The host creates extra windows lazily and copies only each small rectangle. Without
`OQMOD_PANEL_TOUCH` the window is input-transparent. Interactive margins are bounded
to 0–16 pixels. Callbacks receive panel-local coordinates (possibly negative within
the declared touch margin), never a full-screen drawing target. The canvas clips
pixel/text writes to the panel. All panels yield to native modals and the menu.

`waveform_observer(callback)` registers a post-native GUI-thread notification with
the current view, layout (1=2 DECK, 2=2/4 DECK, 3=4 DECK) and selected side decks.
The borrowed view must never outlive native objects; do not start tree scans or
dereference it from a worker. The title module uses only the previously working
bounded fallback field read and existing exact overview hooks. It does not enable
the old experimental component-tree scan. Hooks are resident through restart.

`side_text_filter(callback)` runs on the transport thread before native CRC
generation, with at most 256 bytes. Preserve length, do not perform I/O, drawing,
logging, allocation or native navigation there. `deck_text(deck,artist,out,size)`
reads cached title/artist data (deck 1–4, buffer 1–256 bytes).
`metadata_hide(mask)` publishes only the title-owning service's four-bit mask to
other renderers; it is not a title-hiding algorithm. The USB title engine owns the
UTF-8/UTF-16 filter policy and visibility/drawing/touch behavior, including cleanup.
Changing tabs retains per-deck privacy state; disabling or losing USB clears it.

Tests exercise the actual .so with the real host and simulated native components,
plus the USB module's native re-hide/restore paths. This is not physical parity.

## Native component adapters and a resident native-view provider

`components(out)` returns a sized table of GUI-thread operations: borrowed native
RTTI class names, bounds, visibility and reparenting. All required host adapters
must exist. Calls require the current mod's GUI callback context; calls from a
draw/touch/worker callback fail. The production adapter checks exact native app
identity, class/RTTI validity, geometry (-4096..4096 positions, 0..8192 dimensions),
and child index (-1..64, distinct parent/child). These checks are not a substitute
for native object lifetime: never retain a view after native destruction or turn
an unbounded tree walk into a mod. These trusted native add-ons already have hook
access; this is not a security boundary for untrusted code.

`native_view_provider(kind, table)` is init-only, copies its sized callback table,
and rejects duplicate owners. Kind `OQMOD_NATIVE_VIEW_FX_SIDEBAR` belongs to the
bound `fx-sidebar` service, which must claim an external engine. The fixed context
contains the current owner, parent and original parent bounds. The fixed state
contains borrowed root/readout/chooser references and chooser screen geometry,
for shared popup observation and native update arbitration. Do not change these
fixed structures in place in a future SDK: append a new API if more fields are needed.

The host dispatches converge/visibility/restore on the native GUI thread; the USB
module owns geometry, snapshots, format choices, reparenting and decoration. The
module must not suppress shared native popup observations or independently hook
the native update loop. Disabling or losing the authorizing USB synchronously
restores provider-owned native components, then clears the small frame panels.
There is no dlclose/hot replacement: provider callbacks stay resident until restart.

The sidebar fixture runs the actual .so through the real host, using synthetic
JUCE components. It checks all three layouts' Info/EDIT RATING/FX SELECT popup yield,
restoration and foreign-owner protection, 10,000 stable iterations, USB cleanup
and frame rectangle equality against the prior shaped-window renderer (font pixels
are stubbed). It does not prove physical LCD parity, hardware frame rate, or an
X11-server composition result; the container has no offscreen X server.

## Coherent analysis cache and side-record providers

`analysis_snapshot(deck,out)` copies live raw clock anchors, track generation,
column/beat counts, cues/loops, key/title/artist, deck colour and current privacy,
text override, zoom and theme inputs. Set `out->size` before calling. It returns
data, not a pre-rendered legacy state or waveform. No pointers into the cache escape.
`analysis_columns(deck,generation,first,count,out)` copies 1–64 columns, rejecting
overflow, out-of-range windows and track-generation mismatch.
`analysis_beats(deck,generation,before_ms,out)` returns the beat at/before the requested
time and its neighbor (the final pair at EOF). Rendering-specific look-back and
interval thresholds belong to the mod, not the adapter.

These analysis functions currently work **only inside a side-record callback**.
The shared transport is the live decoder's single writer, and the host takes a
non-blocking analysis-cache lease for that callback. All three calls therefore see
one coherent loaded track; the callback cannot mix generations or transiently lose
the grid because a worker swapped analysis between two reads. A busy cache skips a
slot without blocking, incrementing its sequence or changing the original frame.
Do not call these functions from init, a GUI tick, draw/touch or an arbitrary worker.

`side_record_provider(OQMOD_SIDE_RECORD_LEGACY,table)` is init-only, copies the
sized table, rejects duplicate owners and requires a claimed external engine on
`side-now-playing-legacy`. `record(side,now_ms,out64)` runs on the TX thread only
when the shared transport offers an eligible free legacy slot. It must be bounded:
no allocation, file/database I/O, logging, UI calls or additional locking. Return
1 with a complete fixed-size record or 0 for no record. The host releases its cache
lease on every normal return, validates record kind, deck/side, and activation,
then fills only the existing private slot. A disabled/removed owner's in-flight
record is discarded. The provider does not own the original serializer or CRC.

Browser pages and TYTO takeover retain their existing priority, so the legacy
provider cannot seize a display while those own it. Host frame decoding, shared
analysis loading and controller wire/render primitives stay in the foundation;
the legacy add-on owns presentation/sweep/scheduling policy. Changing those host
or controller primitives can still require firmware. Code replacement needs a
restart; raw callback tables are not hot-dlclosed.

The test uses the actual SDK .so through the real host, the established controller
renderer and an independent old-scheduler oracle retained in the TEST ONLY TU.
8,032 emitted records match byte-for-byte across both sides/four decks. Generation
guards, cache contention, occupied slots, malformed provider records, USB lifetime
and activation cancellation are also checked. Synthetic controller output is not
physical-screen, audio, USB-bus, boot or FPS validation.

## What an add-on-only update means

After installing a matching foundation firmware, replace the whole mod folder on
the settings USB and restart the deck software. Native application hooks remain
resident until restart: this does not support arbitrary live dlclose/reload.
Updating code supported by the existing host bridges needs no new UPD. A new
bridge, native app version, controller primitive or DSP capability may still
require a host/controller update.

## Reserved overlay surfaces

The API-v1 prefix and `oqmod_info` are unchanged. Two pointers are appended:

- `surface(id, out)` fills placement and semantic colours, returning 1 only while
  the surface is visible. It still fills the geometry when temporarily hidden.
- `use_surface(id)` claims a slot during init. Only one healthy add-on may own
  each reserved slot. Unknown IDs, duplicate owners and calls after init fail.

`OQMOD_SURFACE_FX_FREQUENCY` is a 180x66 slot at (1090,306), above the Gate Cues
quick control. It follows the host's popup-aware Beat FX Sidebar state in all
waveform layouts. The host checks this directly before drawing and hit testing:
the mod cannot keep a stale window over a native popup while waiting for tick.
Its `set_area` must remain inside the slot; the host refuses out-of-slot geometry.

Colour fields are semantic tokens for the existing theme-aware drawing API:
background, band, selected, text, dim and divider. Do not bypass host theme
mapping or introduce a separate light-theme inversion in an add-on.

Default surface 0 preserves existing waveform-overlay behavior for old add-ons.
The usual API table-size check must precede access to the appended fields.

`OQMOD_SURFACE_USB_SHORTCUTS` reserves the top-bar area beginning at x=410, y=8,
height 40 and ending at x=910 (795 with STEMS). Active top-bar add-on rectangles,
such as the independent FPS badge, move its left edge right. An exhausted slot is
hidden. It yields to native modals and the mod menu without waiting for tick.
Modal changes invalidate the host overlay repaint key. `text_weight()` exposes
bounded native-font drawing with a bold flag; ordinary `text()` is unchanged.

## Shared USB discovery and navigation

`usb_sources(out)` copies a cached snapshot. Initialize `out->size` before calling.
It returns one for an empty-but-valid snapshot, zero for an invalid or undersized
output/context. It does no filesystem/database/device I/O. Fields include up to
three native source keys, port numbers, native ARGB profile colours (zero unknown,
alpha 255 when known, so configured black is distinct from missing colour),
NUL-terminated UTF-8 names, active media and the observed native-list generation.
Active highlighting alone does not bump generation; names/colours/list changes
do. This is not a persistent hardware UUID or a guarantee about unobserved swaps.

`usb_open(key, generation)` queues one native SOURCE -> USB -> Local Library
navigation request. It never loads a track. Only an enabled loaded add-on on an
unobscured waveform page may request it. Unknown keys and stale generations are
refused. At dispatch the host validates owner activation, USB pause, page/modal
state, list generation and key presence again. The native adapter rechecks the
real Source model before each selection/activation step. Acceptance is not proof
navigation completed. Last queued request wins.

Clicks must use the last rendered frame's key/generation, not a newly refreshed
array entry at the same index. Quick USB retains the painted snapshot under a
short mutex, queues from touch, and calls navigation from tick. Disabling/USB loss
clears pending requests and the rendered frame; resume needs a new draw.

`native_engine()` is an init-only handshake after binding a service whose host
advertises `engine_external`. Older settings-only clients are rejected before
activation when that service no longer has a built-in engine. Original API-v1
add-ons and the 4 remaining configuration clients retain compatibility. This is
compatibility negotiation, not a security attestation for arbitrary native code.

## Encoder routing, FX targets and native TIME lease

`encoder_bind(identity, callback)` registers one owner per encoder identity at
init. Native menu and side-browser consumers run first. The shared native adapter
accepts only the exact native encoder/source types and valid push/raw ranges.
A claimed rotation advances the absolute receive baseline and forwards zero
delta with the original push state. It never guesses a physical packet byte.
Callbacks run on the native input thread and may only queue bounded state.
Unknown identities and disabled/USB-paused owners fall through to native behavior.

`fx_target_read(out)` copies a size-checked target/microphone/generation/SHIFT
snapshot. Ready requires the actual native controller and target-setting adapter.
`fx_target_select(value)` posts the checked original native setting operation;
only an enabled FX Target Knob owner's message-thread tick may call it.
The add-on owns sensitivity (1..5), ring order and pending target acknowledgements.

`time_lease(1)` refreshes a 250ms maximum lease from that owner's tick. The
controller transport consumes it only while SHIFT is released and native target
state is known. Owner activation, disabled/USB-paused state and expiration are
revalidated on each frame. `time_lease(0)` releases it. The native serializer/CRC
and matched controller protocol remain common platform adapters, not mod policy.

## Slip actions and disabled cleanup

`object_deck(2, DirectionHandler*)` identifies a raw-hook object through the early
input-provider registry. It does not toggle Slip or change selection. The Slip
library owns its exact DirectionHandler hook and keeps ownership of a claimed
release even if disabled before release. Unclaimed reverse gestures stay native.

`deck_slip_read(out)` copies ready, native-on, input-on and selected-side masks.
`deck_slip_set(deck,on)` uses the existing vtable/registry/flag-checked native
facades on the message thread. A disabled owner cannot turn Slip on.
`background_tick(1)` is an init-only opt-in for bounded cleanup ticks while off
or after USB removal. It must not read/write USB preferences or activate new work.
Only the Slip service may use its native cleanup adapter while disabled, and
only to send OFF. The library cancels/restores its owned state, stops querying
once cleanup completes, and uses ordinary LED leases for all Slip indications.
No Slip toggle/pending/LED algorithm remains in the external-profile host.

## Native theme and render providers

`theme_provider(table)` is init-only and restricted to the `light-theme` service
after its engine claim. The host copies a size-checked table and rejects a second
provider. The library owns palette/set/on/surface/ink behavior. The host owns no
colour algorithm in the external profile; unregistered means exact stock colours.
The shared X11 repaint operation stays in the host. Callbacks remain resident;
disabling/removing the settings USB switches the engine off immediately.

`render_provider(table)` similarly belongs to `smooth-waveforms`. Shared native
instrumentation invokes period/begin/end callbacks without transferring hook
ownership, so FPS measurement works independently. `period` receives the native
timer object, requested milliseconds, call-site identity and monotonic nanoseconds.
The inherited INT_MIN result retains an already-earlier repaint deadline.
Callbacks may only touch the verified native build; no settings, logging or I/O.
The host seeds each zero-initialized per-paint context with its revalidated early
frame-clock identity. This avoids waiting for a new startTimer after USB loading.
Each call owns its context, including nested paints. Missing provider is stock
timing with telemetry; disabling a provider must immediately pass stock behavior.

`extract_light_engine.py` and `extract_render_policy.py` in the development root
record/copy the existing implementation rather than inventing new native offsets.
The light engine retains its GPL source/license and older libm ABI. Actual-library
tests simulate native objects; they do not prove hardware colour/FPS parity.

## Early native lifecycle and ZONE

`object_deck(kind, instance)` is an advanced identity lookup for an object already
received by a checked raw hook. Kind 1 identifies a ContinuousPlayLoader, returning
deck 1..4 or 0 when unknown. The host retains constructor order before add-ons load;
exactly four objects and all three observer hooks are required. A fifth constructor
invalidates identity inference. No native pointer is returned to the caller.

The `zone-continuous` service adds read-only `zone_on` and `zone_assign` telemetry.
Unknown is -1; Zone on is 1; assignment 0/1/2 means channel 3/channel 4/master.
The shared observers never mutate native state or play a track. The add-on owns
tryLoadNext at 0x7b5a70, original flag restoration, routing policy, readiness wait
and tick-only PLAY. All original native PLAY object/vtable guards remain in the
generic deck_play adapter. Pending PLAY is cancelled on disable/removal/reassignment.
No data from settings-only clients may substitute for native loader identity.

## Native packet-hook ownership

FX Frequency uses the existing checked `hook()` service. It owns both the native
feedback and serializer helper hooks, all processing state and the complete policy.
Core `native_get("engine_external",0)` on its bound service reports the matched
transport readiness check. R112 returns no such capability; the engine refuses
to load there rather than chaining over a built-in implementation.

The transport readiness check runs before TIME wraps the common builder vtable.
The add-on loads later and must not expect that shared slot to still contain its
stock value. It verifies its own original helper prologues and call site, never
the TIME-owned slot. Original builder and CRC processing are unchanged.

Original calls are forwarded even while OFF or partially installed. The mod may
replace only Y under the existing selection/ON/object/freshness/mode guards.
It cannot activate the pad, change timing or modify another packet byte. On USB
loss the host calls enabled(0); new activation needs fresh ON feedback. Raw hooks
are not removed live. Guard failure is visible in the mod's status.

Touch stores an atomic request; settings writes happen in tick, not the overlay
touch callback. Diagnostics updates are limited to once a second, not every
packet. Never perform logging, filesystem or UI work in the serializer callback.

The optional `OQ_NATIVE_CUSTOM_ENTRY` helper mode exposes static
`oq_feature_init()` without generating `oqmod_init`. A full engine can wrap menu
and preference setup while owning enabled/tick/draw/touch and the actual behavior.
The generator must leave custom engine source untouched.

## Tests and validation boundaries

`tests/fx_engine_test.c` covers 163,840 policy combinations plus actual engine
wrappers, guard/partial-install safety, selection persistence, bounds, popup draw
suppression and disable/resume. The private host's `addon_surface_test` checks
surface ownership, window/touch lifetime and bounds. `fx_external_load_test`
loads the actual compiled .so through the real host with simulated native memory
and transport. None establishes analog audio response or physical rendering.

Preserve a known-good firmware and settings backup. Package the foundation and
matching add-ons together, verify them independently, and complete hardware
regression before publishing a release.

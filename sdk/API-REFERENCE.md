# OQ Mods SDK 1.6: API reference (firmware R118)

Every entry point of `struct oqmod_api` (API version 1), in table order, plus the mod
folder files and the symbols the firmware exports. Behaviour here is taken from the R117
host (`host-r79-lab/src/plugin_host.h`), not just from the header comments.

The table is append-only. A mod built with a newer header checks `api->size` and refuses
to load on older firmware, so nothing breaks silently.

## Who can call what

Three rules explain most refusals (a call that returns 0 and does nothing):

- **The mod context.** Most calls work out which mod is calling from where they are made:
  `oqmod_init`, `tick`, `enabled`, `draw`, `touch`, menu-action callbacks and the
  provider callbacks below. **Called from your own thread, they fail or fall back.**
  `setting_get()` then returns its fallback and `setting_set()` does nothing. Copy what
  your thread needs into a variable inside `tick()`.
- **Init only.** Registration calls (`option`, `choice`, `menu_action`, the `*_provider`
  calls, `native_bind`, ...) only work inside `oqmod_init`, before it returns.
- **Feature engines only.** Some calls are reserved for the built-in feature that owns a
  native service (shown as *Owner* below). The mod must `native_bind()` that service and
  call `native_engine()` first. Third-party mods normally use only the calls without an
  owner.

Threads: `tick()` runs about 30 times a second on EP136's message thread, starting about
15 s after the app's first event loop (R107 and later; earlier firmware waited for the
Source list). `draw()`/`touch()` run on the overlay thread. Never block in
any callback.

## Core (R101)

| Call | What it does | Notes |
|---|---|---|
| `log(line)` | Adds a line to the mod log in the runtime report | Ring of 16 lines shared by all mods, 120 characters each, prefixed with the folder name |
| `deck(n, out)` | Fills `struct oqmod_deck` for deck 1..4; returns 1 when the deck has data | Any thread |
| `waveform_screen()` | 1 while the WAVEFORM screen shows | |
| `setting_get(key, fallback)` | Reads the mod's setting from `MODS/<folder>/settings.txt` | Mod context only |
| `setting_set(key, value)` | Writes it (atomic file write, only when the value changes) | Mod context only; keys under 32 characters, no `=` or newlines; up to 24 keys |
| `redraw()` | Asks for `draw()` soon | Any thread |
| `fill`, `outline`, `text` | Paint inside `draw()`, coordinates relative to the area | `text` sizes 6..80 px; the text box must lie inside the area |
| `now_ms()` | Milliseconds since the app started | |
| `set_status(text)` | Status text on the mod's page | Mod context; up to 1023 bytes. Release pages may hide it |
| `ep136_ok()` | 1 when the deck software is the build the raw addresses are for | Check before `hook`/`patch` |
| `hook(fn, expected, repl, &orig)` | Replaces a function whose first instruction is `expected` | Stays until restart; logged as installed or refused |
| `patch(slot, expected, repl, &orig)` | Swaps one vtable slot holding `expected` | Same |
| `deck_play(n)` | Presses PLAY on deck 1..4 like the deck's own button | Call from `tick()` |
| `option(key, label, fallback)` | On/off row on the mod's page; returns the value | Init only |
| `quick_toggle(on, off, key)` | Button in the Beat FX box that switches the mod | One mod's button at a time; NULL labels remove it |

## LEDs and screens (R104)

| Call | What it does | Notes |
|---|---|---|
| `led_set(side, led, rgb, hold_ms)` | One LED on side 0/1, held up to 10 s | Refresh from `tick()` to keep it; `OQMOD_LED_*` numbers |
| `led_clear()` | Releases every LED set by mods | Also happens when a mod is switched off |
| `set_area(x, y, w, h)` | Moves or resizes the overlay area (`w`=0 hides it) | Refused for panel mods; must stay inside a bound surface |
| `layout()` | 0 off the WAVEFORM screen, 1 = 2 DECK, 2 = 2/4 DECK, 3 = 4 DECK | |
| `side_text(deck, text, hold_ms)` | Text on a deck's side screen in place of its title | Up to 40 characters, up to 60 s |
| `choice(key, label, names, count, fallback)` | Option row with 2..8 named values | Init only; up to 4 rows per mod in total |

## Native services and menu (R111, R112)

| Call | What it does | Notes |
|---|---|---|
| `native_bind(service)` | Binds the mod to one named native service | Init only; one owner per service, except `telemetry`. Services: [NATIVE-SERVICES.md](NATIVE-SERVICES.md) |
| `native_get(prop, fallback)` / `native_set(prop, value)` | Read or queue a change of a service property | `set` is applied on the GUI thread; `applied.<prop>` changes once consumed |
| `menu_order(priority)` | Position in the Mod Menu (0..2000, default 1000) | Init only; `menu.order` overrides it |
| `default_enabled(on)` | Default switch when no `on.<folder>` is saved | Init only |
| `choice_buttons(...)` | Like `choice()`, every value as its own button | Init only |

## Engines (SDK 1.3, R113)

| Call | What it does | Owner / notes |
|---|---|---|
| `surface(which, out)` / `use_surface(which)` | Reserved overlay slot that hides with native pop-ups | `use_surface`: init only; one mod per slot (FX Frequency, USB shortcuts) |
| `usb_sources(out)` | Cached USB names, ports, colours and a generation | Set `out->size`; no I/O |
| `usb_open(key, generation)` | Queues native SOURCE > USB > library navigation | Only on the unobscured WAVEFORM screen; acceptance, not success |
| `text_weight(...)` | `text()` with bold 0/1 | |
| `native_engine()` | Declares the mod the complete implementation of its bound service | Init only; needed by every owner-only call |
| `object_deck(kind, instance)` | Deck 1..4 of a native loader (1) or direction handler (2) your hook received | |
| `theme_provider(p)` | Registers the theme colour functions | Owner `light-theme` |
| `render_provider(p)` | Registers the waveform timer and paint policy | Owner `smooth-waveforms` or `waveform-layout` |
| `encoder_bind(id, event)` | Claims one rotary encoder's rotation | Init only; first enabled owner wins; no I/O in the callback |
| `fx_target_read(out)` / `fx_target_select(v)` | Reads or sets the Beat FX target | `select`: owner `fx-target`, from `tick()` |
| `time_lease(active)` | Holds back the native TIME knob for 250 ms | Owner `fx-target` |
| `background_tick(allow)` | Keeps `tick()` running while switched off, for cleanup only | Init only |
| `deck_slip_read(out)` / `deck_slip_set(deck, on)` | Reads or sets SLIP | `set`: owner `slip-mode`, from `tick()` |
| `panel_renderer(draw, touch)` / `panels_set(p, n)` | Up to 10 small overlay windows instead of one area | Renderer: init only. Panels inside the screen, hit margin 0..16 |
| `waveform_observer(update)` | Called with the WAVEFORM view, layout and selected decks | Init only |
| `side_text_filter(filter)` | Rewrites side-screen title chunks before they are sent | Init only; no blocking |
| `deck_text(deck, artist, out, size)` | Title (0) or artist (1) text of a deck | `size` up to 256 |
| `metadata_hide(mask)` | Hides titles for decks in the mask | Owner `hide-titles` |
| `components(out)` | Native component operations (class, bounds, visibility, reparent, ...) | Each operation only from `tick()` |
| `native_view_provider(kind, p)` | Takes over the Beat FX sidebar view | Owner `fx-sidebar` |
| `analysis_snapshot`, `analysis_columns`, `analysis_beats` | Cached waveform, cue and beat data | Only inside a side-record callback |
| `side_record_provider(kind, p)` | Supplies the legacy side screen's 64-byte records | Owner `side-now-playing-legacy` |
| `layout_provider(p)`, `waveform_parts`, `waveform_sidebar` | 4-deck layout engine and its native parts | Owner `waveform-layout`; parts and sidebar from `tick()` |
| `browser_services(out)`, `browser_provider(p)` | Library catalog, side-screen pages, guarded loading | Owner `side-browser`; see `oqmod_browser.h` |
| `playlist_services(out)`, `playlist_provider(tick)` | Main browser playlist editing | Owner `playlist-editor` |
| `stems_audio(out)`, `stems_ui(...)` | Early stem audio hooks and the stems strip | Owner `stems`; see `oqmod_stems.h` |

## Tools (R114)

| Call | What it does | Notes |
|---|---|---|
| `menu_action(label, callback)` | Button on the mod's page | Init only; up to 9; runs on the settings tick, only while the mod is on |
| `diagnostic_save()` | Queues a runtime report on the USB | Asynchronous |
| `diagnostic_status(out, cap)` | System info and the last save result | |
| `menu_action_label(i, label)` | Renames action `i` | From `tick()` or callbacks |
| `ring_brightness(percent)` | Dims both jog rings to 1..100 % of what they show (deck colour, Party Mode, mods' LEDs); never brightens. One enabled mod owns it; switching that mod off restores 100 %. R121+: check `api->size` first | From `tick()` or `enabled()` |

## The mod folder

`MODS/<folder>/`, all optional except `mod.so`. Read once at software startup.

| File | Purpose | Limit |
|---|---|---|
| `mod.so` | The mod, exporting `oqmod_init` | glibc 2.29 or older (`build.sh` checks) |
| `settings.txt` | The mod's settings, written by the host | 24 keys |
| `menu.title` | Name in the Mod Menu (R116) | 63 bytes, ASCII |
| `version.txt` | Version on the page heading (R116) | 31 bytes |
| `description.txt` | Text on the page (R116) | 767 bytes, ASCII |
| `menu.order` | Menu position, overrides `menu_order()` | 0..2000 |
| `choices.order` | Display order of choice buttons, e.g. `mode=3,0,1,2` (R117) | Must be a full permutation |
| `icon.pgm` | 48x48 binary PGM (P5, maxval 255) | Falls back to the puzzle icon |
| `diagnostics.only` | Load only when `enable_diagnostics.txt` is on a USB at startup | |

`MODS/state.txt` holds only the switches (`on.<folder>=0|1`). Up to 32 mods are loaded.
Use plain ASCII in titles and descriptions: curly quotes show as garbage.

## Symbols the firmware exports

Look these up with `dlsym(RTLD_DEFAULT, name)` and check for NULL: older firmware lacks
them.

| Symbol | Since | What it is |
|---|---|---|
| `int opusmod_deck_track_path(int deck0, char *out, size_t n)` | R85 | Full path of the audio file on deck 0..3; wrapped by `oq_track_path()` in `oqmod_events.h` |
| `void *opusmod_side_writer_lock(void)` | R106 | A `pthread_mutex_t *`: the lock the firmware holds around side-screen variable writes |
| `void opusmod_side_takeover(int on)` | R106 | Pauses the firmware's own side-screen deck view while another renderer owns the screens |
| `int opusmod_title_hidden(unsigned deck)` | R115 | 1 while Hide Track Names hides deck 1..4's title; used by TYTO's Side Screen View |
| `const struct oq_master_audio_api *opusmod_master_audio_api_v1(uint32_t abi)` | R118 | The read-only master feed; use the helpers in `oqmod_master_audio.h` instead of calling it directly |

## Other headers

- `oqmod_events.h`: named events (`track_loaded`, `play`, `pause`, ...), `oq_track_path()`
  and `oq_mod_dir()`.
- `oqmod_native_feature.h`: a reusable client for a feature bound to a native service.
- `oqmod_browser.h`, `oqmod_stems.h`: the structures used by the side browser and stems engines.
- `oqmod_master_audio.h`: the **read-only master feed** (R118): a copy of the final stereo
  mix for meters, analysis or recording. It cannot change the sound; a writable master
  insert does not exist on OPUS. See [MASTER-AUDIO.md](MASTER-AUDIO.md).

## Examples

- `examples/`: small third-party style mods (BPM difference, party mode, set list, master meter).
- `mods/`: the built-in feature engines, which show the owner-only calls in use.
- `reference-mods/`: Bluetooth Waveform, Gated Cues, Party, Pro DJ Link.

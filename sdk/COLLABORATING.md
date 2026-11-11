# Working on mods together

## The setup

- **This SDK is the shared repo.** Put it in a private GitHub repository and add each
  other as collaborators. Mods go in their own folders next to `examples/`, e.g.
  `mods/<name>/mod.c`.
- **The firmware is not in the repo.** You test mods on a deck running Robs Opus Quad Mods
  (R112 for current native-service clients; already-built older API-v1 mods remain supported), installed as usual. Mods load from
  `MODS/<name>/mod.so` on the settings USB; there is no signing.
- `build/` folders are ignored by git (`.gitignore`): commit sources, not `mod.so` files.

## Never commit

- Firmware images, update files or anything taken from the deck's software (files,
  fonts, images, sounds, screens).
- Keys of any kind.
- Personal music, playlists or USB exports.

## Asking for a hook

When a mod needs something the API doesn't offer (an event, a deck action, some data),
open an issue in the repo: what the mod should do, and what it needs to know or do on the
deck. Hooks are added to the firmware and to `include/`, and the new header is pushed here.
Mods built with a newer header refuse to load on older firmware (`api->size` check), so
nothing breaks silently.

## Workflow

1. `./build.sh mods/<name>` (Docker; the first run builds the compiler image).
2. Copy `mods/<name>/build/<name>` to `MODS/<name>` on the settings USB.
3. On the deck: Mod Menu > diagnostics/maintenance > **Reload Mods**, then switch the mod on from its own page.
4. If something goes wrong: System Info > **Save diagnostics** puts your mod's `log()`
   lines in the report on the USB; a crash leaves `OPUS-CRASH.txt` there.
5. Run the SDK tests with `sh tests/run.sh`, and add tests for logic that doesn't need the
   deck (see `tests/events_test.c`).

Native-service mods: read `NATIVE-SERVICES.md` and `HOOK-NOTES.md` first. Keep their
icons in `icon.pgm`, ordering in `menu.order`, and settings inside the owning mod
folder. The backend/ABI boundaries must be explicit; do not call a settings client
a complete extraction of its native engine.

# Firmware changelog

## R120 (2026-11-09)
## Highlights

- **Add-on mods.** Every feature is now a separate add-on in `MODS/` on your USB stick. You switch each one on or off in ROBS MOD MENU, each has its own settings, and you can add, update or remove them without a firmware update. The firmware itself is now a slim base layer.
- **Mod updates without flashing.** The new OQ Mods Patcher has a **Mods** tab that reads the live mod list from this repo (`mods/`). For every mod it shows the version on your USB stick and the latest version, and you can install, update or pick an older version. **Select updates** ticks everything with a newer version, **Changelog** shows what changed in each version, and **?** describes each mod. Mods are written straight to your stick and checked before use. Mods new to the stick start switched off; your existing settings and switches are kept.
- **Firmware updates from the app.** The OQ Mods Patcher is now one small app for every firmware: its **Firmware** tab lists the released firmware versions with their changelogs, shows which firmware your player runs and whether a newer one is available, and downloads the patch and writes the update straight to your USB drive from your official 1.32 update (chosen once and remembered). Older firmware versions stay available. The player notes its firmware version on the stick at start-up so the app can tell.
- **Online mod catalog.** Every mod has its own folder in `mods/` with a changelog and every released version, so you can also download a mod by hand.
- **Mod SDK.** Other developers can build their own add-ons with the SDK, without the firmware source. It includes events (track loaded, play/pause, BPM/key change, near end of track), LED and screen access, menu actions and a read-only master audio feed.
- **TYTO's Side Screen View.** CDJ-3000-style side screens by TYTO: the selected deck and master waveforms, artwork and track info, plus an orange MASTER PLAYER badge on the master deck. Follows Light Mode and Hide Track Name.
- **Boot label.** The startup screen shows "ROBS MOD 1.32X" and the installed release in the bottom-right corner.
- **ROBS MOD MENU.** The mod menu in the SOURCE list is now called ROBS MOD MENU.

## New and improved features

- **Light Mode** now covers the bottom mixer/MIC panels and the deck input pop-up menu, with a normal or blue waveform palette.
- **Side-screen deck colours** follow SHORTCUT > Deck Color.
- **Party Mode**: animated jog-ring LED colour effects with selectable speed.
- **Slip Mode**: replaces Slip Reverse with Slip on each deck.
- **FX Knob Selector**: turn Beat FX TIME to pick MIC / 1–4 / MASTER; hold SHIFT to change the effect time.
- **Gate Cues**: on a paused deck, hold a hot cue to audition it.
- **Hide Track Name** now also hides artists and works on TYTO's side screens.
- **Side Screen Browser** has three control schemes and remembers where you were.
- **4-Deck Waveforms**: smooth native-height mode, lane order and track overview options in one place.
- **Stems** is no longer experimental.
- **Mod Menu**: one page per mod with icons, scrolling options, choice buttons and status lines. Long names are shortened to fit.
- **Side Screen Waveforms (Legacy)**: the original side-screen view, still available as its own add-on.

## Fixes

- The Beat FX sidebar no longer covers the FX selection list or the track info / Edit Rating panels.
- The FX overlay shows in 4-deck view with the sidebar off (#3).
- Side-screen hot cues, cue colours and loops now update live.
- Fixed a crash on track load and a side-screen controller update failure.
- Light Mode stays light after the deck input menu is opened and closed.
- The add-on page no longer drops the last mod when many are switched on.
- Many smaller stability and layout fixes.

## Notes

- Keep the official stock 1.32 update on a separate USB for recovery.
- The stems engine is a port of Kyle Hosman's AZ-MODS stems engine (GPL-3.0).

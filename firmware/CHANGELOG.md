# Firmware changelog

## R130 (2026-11-20)
## Highlights

- **Eight new mods:** USB Deck Colors, SHIFT Search & Zoom, Fader Start, Keep Tracks On Eject, Jog Ring Brightness, TYTO's Deck Highlight, and two Experimental mods: Pro DJ Link and Mobius FX.
- **Stems 3.0:** set each stem's volume with the channel 3/4 knobs, see which tracks have stems right in the browser, and a fix for stems playing on the wrong deck.
- **Faster ROBS MOD MENU:** hold **SOURCE** for about a second and let go to open it. It reopens on the item you used last (until the OPUS is switched off).
- **Mods work together on the same screen:** Light Mode, TYTO's Deck Highlight and TYTO's side screens can all be on at once.
- **Minimum firmware per mod:** every mod says which firmware it needs. A mod that needs a newer firmware is greyed out in ROBS MOD MENU ("needs firmware Rxxx"), and the OQ Mods Patcher warns before installing it.
- **OQ Mods Patcher 1.3.0:** shows each mod's firmware requirement and offers R130 and all 24 mods. Older patchers show that 1.3.0 is available.

## New mods

- **USB Deck Colors 1.3:** jog rings, deck buttons, the deck numbers on screen and the LOAD buttons take the colour of the USB each deck's track came from (local or LINK USB). Optional brighter selected deck button.
- **SHIFT Search & Zoom 1.3:** hold SHIFT and a TRACK SEARCH button to search through the track, faster the longer you hold. SHIFT + BROWSE or SHIFT + BEAT LOOP zooms the waveforms.
- **Fader Start 1.1:** channel faders and the crossfader start and pause the decks.
- **Keep Tracks On Eject 1.0:** ejecting a USB stick in SOURCE keeps the tracks loaded on the decks playable, as on the XDJ-AZ.
- **Jog Ring Brightness 1.0:** dim both jog rings to 75, 50, 25 or 10 percent.
- **TYTO's Deck Highlight 1.5:** an outline in the deck colour around the selected decks' waveform rows, with adjustable size and transparency, and optional dimming of the decks that aren't selected.
- **Pro DJ Link 2.0 (Experimental):** joins the DJ Link network on the LAN port as a free player. Set channel 3 or 4 to PRO DJ LINK to play a linked player's track on that deck with its waveform, cues, loops and tempo, and browse other players' USB sticks as LINK USBs. Logs are kept in `MODS/prodjlink/logs` for problem reports.
- **Mobius FX 1.0 (Experimental):** MOBIUS in the Beat FX list: select it and press BEAT FX ON for a rising or falling Mobius tone. Choose the sound, direction, channel, beats per octave and level.

## Updated mods

- **Stems 3.0:**
  - The channel 3/4 TRIM, HI, MID and LOW knobs set the stem volumes of decks 1/2 (vocals, instruments, bass, drums), and the stem buttons show each volume.
  - Tracks with prepared stems get a STEMS icon in the main browser.
  - Fixed: a deck could play another deck's stems after a new track was loaded.
- **FX Frequency 3.0:** LOW and HIGH play the Beat FX like holding the bottom or top of the X-PAD and hold until BEAT FX ON is pressed; BEAT < and > change the beat while LOW or HIGH is on.
- **Light Mode 2.4:** loops on the main waveform are light amber instead of a dark box, and tracks without artwork show a light placeholder.
- **TYTO's Side Screen View 3.7.3:** TYTO's new 3.7 build, with smoother side-screen updates and less work for the main screen.
- **Side Screen Browser 2.3:** tracks with stems are marked, and the first page after reopening shows the full track info straight away.
- **Quick USB 2.1:** option to also show the LINK USBs of other Pro DJ Link players.
- **4-Deck Waveforms 2.6:** layout updates for the channel input display in the 4-deck view.

## New for mod developers

- **OQ Mods SDK 1.11**, with the source of every released mod (except TYTO's).
- **Mixer controls:** read the channel faders, crossfader and knobs.
- **Deck buttons and LEDs:** react to deck buttons and read or change the deck LEDs.
- **Jog ring brightness:** dim the jog rings.
- **Pause a deck** the way its PLAY/PAUSE button does.
- **Keep running without the settings USB:** mods can keep working after the stick with the mods is ejected; when the same stick comes back, its mods carry on where they were.
- **Panels above pop-ups:** a mod's on-screen panel can stay visible over the deck input pop-up menu.
- **Deck audio insert:** process a deck's audio before the mixer.

## How to update

1. Open the OQ Mods Patcher 1.3.0, write the R130 firmware to a USB stick on the **Firmware** tab and run the update on the OPUS.
2. On the **Mods** tab, click **Select updates**, then **Install**. Switch new mods on in ROBS MOD MENU.

Keep the official stock 1.32 update on a separate USB for recovery. The stems engine is a port of Kyle Hosman's AZ-MODS stems engine (GPL-3.0).

## R120 (2026-11-09)


## Highlights ()
- **Add-on mods.** Every feature is now a separate add-on in `MODS/` on your USB stick. You switch each one on or off in ROBS MOD MENU, each has its own settings, and you can add, update or remove them without a firmware update. The firmware itself is now a slim base layer.
- **Mod updates without flashing.** The new OQ Mods Patcher has a **Mods** tab that reads the live mod list from this repo (`mods/`). For every mod it shows the version on your USB stick and the latest version, and you can install, update or pick an older version. **Select updates** ticks everything with a newer version, **Changelog** shows what changed in each version, and **?** describes each mod. Mods are written straight to your stick and checked before use. Mods new to the stick start switched off; your existing settings and switches are kept.
- **Firmware updates from the app.** The OQ Mods Patcher is now one small app for every firmware: its **Firmware** tab lists the released firmware versions with their changelogs, shows which firmware your player runs and whether a newer one is available, and downloads the patch and writes the update straight to your USB drive from your official 1.32 update (chosen once and remembered). Older firmware versions stay available. The player notes its firmware version on the stick at start-up so the app can tell.
- **Online mod catalog.** Every mod has its own folder in `mods/` with a changelog and every released version, so you can also download a mod by hand.
- **Mod SDK.** Other developers can build their own add-ons with the SDK, without the firmware source. It includes events (track loaded, play/pause, BPM/key change, near end of track), LED and screen access, menu actions and a read-only master audio feed.
- **TYTO's Side Screen View.** CDJ-3000-style side screens by TYTO: the selected deck and master waveforms, artwork and track info, plus an orange MASTER PLAYER badge on the master deck. Follows Light Mode and Hide Track Name.
- **Boot label.** The startup screen shows "ROBS MOD 1.32X" and the installed release in the bottom-right corner.
- **ROBS MOD MENU.** The mod menu in the SOURCE list is now called ROBS MOD MENU.

## New ()
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

## Fixes ()
- The Beat FX sidebar no longer covers the FX selection list or the track info / Edit Rating panels.
- The FX overlay shows in 4-deck view with the sidebar off (#3).
- Side-screen hot cues, cue colours and loops now update live.
- Fixed a crash on track load and a side-screen controller update failure.
- Light Mode stays light after the deck input menu is opened and closed.
- The add-on page no longer drops the last mod when many are switched on.
- Many smaller stability and layout fixes.

## Notes ()
- Keep the official stock 1.32 update on a separate USB for recovery.
- The stems engine is a port of Kyle Hosman's AZ-MODS stems engine (GPL-3.0).

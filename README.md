# Robs Opus Quad Mods for Pioneer OPUS-QUAD

**The first community custom firmware for the Pioneer OPUS-QUAD.**

Robs Opus Quad Mods (shortened to **OQ Mods** later in this document) is a community custom-firmware project for the OPUS-QUAD, based on official firmware **1.32**.

The current public build is **R70 (1.32X, internal app revision 7709)**. It adds a native four-deck waveform layout and a collection of optional interface, browsing, FX, cue, and jog-ring features. The public release is distributed as a binary patcher; Pioneer firmware files are not included.

> **Use this firmware at your own risk.** I have flashed firmware on my OPUS-QUAD many times—well over a hundred—and have always been able to recover from failed attempts by restoring stock firmware. That is my experience, not a guarantee: a failed flash could still leave your unit unusable, and recovery may not work for everyone. Keep a known-good official stock 1.32 update available on a separate USB, use reliable power, and never interrupt an update. Use only with an OPUS-QUAD and the exact official 1.32 update file accepted by the patcher. I have tested restoring stock 1.32 and installing R70 on a physical unit; this does not guarantee every unit or future build.

## Feature gallery

The following are perspective-corrected, cleaned-up visual renderings based on owner-supplied photos of the physical unit. They show the tested feature set, but are not raw emulator captures; fine visual details may be smoothed during cleanup.

### Four-deck waveform view

Four native detailed waveforms are arranged **Deck 3, Deck 1, Deck 2, Deck 4**, keeping the primary Deck 1/2 lanes centered. The optional FX sidebar sits at the right; the original track overviews can be enabled separately.

![Four-deck waveform layout with FX sidebar](docs/screenshots/four-deck-waveform.png)

### Browse from the side screens

Hold a deck selector to open that deck's side-screen browser. Browse the USB → playlist → track hierarchy, select a track, and confirm loading. The browser supports the beat-loop wheels, deck buttons, or track-search buttons as the navigation scheme.

![Side-screen track selection](docs/screenshots/side-browser-track-select.png)

![Side Browser controls in the mod menu](docs/screenshots/side-browser-settings.png)

### Configure mods on the unit

The on-device menu is currently labeled **OQ Mods**. It is scrollable and exposes the available options and per-mod settings.

![Four-deck mod settings](docs/screenshots/four-deck-settings.png)

### FX and performance controls

The FX Target Knob mod assigns the Beat FX TIME knob to the FX target selector; hold SHIFT while turning it to adjust effect time. FX Frequency provides LOW/HIGH tone isolation controls, and the Beat FX sidebar provides a home for the FX controls and quick Gate Cue toggle.

![FX target knob settings](docs/screenshots/fx-target-knob.png)

![Party Mode in the scrolled mod list](docs/screenshots/party-mode-list.png)

Quick USB Access adds named, color-coded shortcuts near the waveform header. Selecting one opens that USB's native browser rather than loading a track directly.

![Quick USB Access settings](docs/screenshots/quick-usb-access.png)

## Included mods

Enable or configure these from the on-device **OQ Mods** menu. Some are disabled by default and are user-selectable.

- **4-Deck Waveform** — switches the WAVEFORM screen to four full-height native waveform lanes in Deck 3 / 1 / 2 / 4 order. The stock two-deck and 2/4-deck layouts remain available.
- **Smooth Waveforms** — includes the native-height-30 mode, tuned for a steadier four-deck display on the OPUS-QUAD hardware.
- **Light Theme** — alternate light palettes, including a blue-waveform option.
- **Side Browser** — opens a deck-specific browser on the left or right side display. Browse USBs, playlists, and tracks; confirm a selection to load it to that deck. The beat-loop-wheel scheme is the default, with alternative deck-button and track-search-button schemes available. Stock Load Lock is required.
- **Quick USB Access** — named, color-aware on-screen USB shortcuts that open the selected source in its native browser.
- **Beat FX Sidebar** — restores a compact FX panel at the right of the main waveform screen, including in layouts where the four-deck mode is off. It also provides space for the quick Gate Cue toggle and FX controls.
- **FX Frequency** — LOW/HIGH tone-isolation controls for Beat FX.
- **FX Target Knob** — use the Beat FX TIME knob to step through MIC, Deck 3, Deck 1, Deck 2, Deck 4, and MASTER. Sensitivity is adjustable from one to five detents per target; three is the default. Hold SHIFT while turning TIME to retain normal effect-time adjustment.
- **Slip Mode** — repurposes the Slip Reverse control to toggle Slip per deck, with LED feedback.
- **Gated Hot Cues** — hold a hot cue for gated playback; release behavior returns to the cue point unless PLAY was pressed while holding. A quick toggle is available in the FX sidebar when enabled.
- **Party Mode** — jog-ring effects: Rainbow, Color Steps, Breathe, Police, Color Flash, and Ring Chase, with four speed settings. Slip indicators and LED diagnostics take priority. Flashing effects are optional; avoid them if sensitive to flashing lights.
- **Persistent settings** — mod settings are stored in `OPUSMOD_SETTINGS.cfg` on the USB. The patcher preserves an existing settings file and creates a default one only if it is missing.
- **Optional diagnostics** — diagnostic tools are hidden in the public build unless `enable_diagnostics.txt` is present on a USB stick.

## Not included in the public build

Playlist editing, stem processing, and beat-grid editing are not presented as working R70 public features. The development menu may show research labels when diagnostics are enabled, but those labels do not mean the feature is implemented. See the roadmap below for future work.

## Getting started

1. Obtain the official OPUS-QUAD **1.32** update from Pioneer DJ. It is not bundled here.
2. Download the matching Robs Opus Quad Mods patcher for macOS or Windows from the project's **Releases** page. The current v0.6.0 patcher may still display its original “OQ Mods Patcher” branding.
3. Open the patcher, choose the official `OPUSQUADv132.UPD`, and generate the modified update. The patcher validates the input and verifies the output locally; it does not upload firmware or flash the player.
4. Copy the generated `OPUSQUADv132.UPD` to the root of a dedicated update USB. Keep the stock 1.32 update on a separate, clearly labeled recovery USB.
5. With the unit powered off, hold **BEAT SYNC** and **MASTER** on the right deck while powering it on to enter the update screen. Follow the on-screen instructions. Do not power off or remove the USB during the update.
6. After reboot, open the on-device OQ Mods menu and enable the features you want. Keep `OPUSMOD_SETTINGS.cfg` on the USB whose settings you want to use.

The standalone patchers are versioned independently from the firmware. For the current build, the patcher is **0.6.0** and the firmware is **R70 / app revision 7709**. Always use the patcher and release notes attached to the same firmware release.

## Roadmap

- Real-time playlist editing and reordering
- Stem support
- Large moving waveforms on the side displays
- Smaller system-wide font options
- One-touch BPM half/double controls
- Bluetooth-in waveform recording
- Full PRO DJ LINK support

## Credits

- **Kyle Hosman** — creator of the XDJ-AZ Mods project, whose public work provided valuable reference and inspiration.
- **LycheeAPPF and Tratosca** — guidance and knowledge shared during development.
- The OPUS-QUAD community and everyone who tested builds and reported results.

Robs Opus Quad Mods is an independent community project and is not affiliated with or endorsed by AlphaTheta or Pioneer DJ. Product names are used only to identify compatibility.

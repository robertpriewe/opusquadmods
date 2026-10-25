# R53 public-release checklist

Do not publish the current candidate as a tested release until the hardware gates below are complete. This checklist is separate from the README because R53 changes the USB authorization/settings path and has not yet been physically installed.

## Before building the public release

- [ ] Decide what may legally be redistributed. Confirm permission/licensing for the stock firmware payload; do not assume that reverse-engineering access grants redistribution rights.
- [ ] Confirm the exact supported OPUS-QUAD baseline and reject other models/firmware versions clearly.
- [ ] Make the package-specific install and stock-recovery instructions match the exact artifact.
- [ ] Record SHA-256 for every downloadable file and publish the hash beside the download.
- [ ] Ensure no source snapshot, AES/LUKS key material, credentials, user music, USB database, or private diagnostic trace is in the release assets.
- [ ] Replace every QEMU/offline screenshot with a hardware screenshot where possible, or retain an explicit QEMU/offline caption.
- [ ] Add a change log and a known-issues section for the exact release revision.

## R53 hardware gates

- [ ] Install R53 on the owner's stock 1.32 unit only after confirming the update stick contains the intended package and no stale UPD/marker files.
- [ ] Capture the update trace and confirm all components finish; verify a normal cold boot.
- [ ] Confirm System Info identifies **1.32X R53** and that normal audio/playback still works.
- [ ] With `OPUSMOD_SETTINGS.cfg` absent: confirm OQ MODS is hidden and its features are disabled. State clearly that the firmware remains modified.
- [ ] Add a valid settings file to one USB while running: confirm menu appearance. Remove it again and confirm hide/disable behavior.
- [ ] Change one harmless setting, reboot, and verify it persisted to that USB. Repeat with two USB sticks to document which settings file wins.
- [ ] Verify each visible mod individually. Test waveform timing with one and four active decks; check FX, Slip, cue behavior, side browsing/load lock, LEDs, light theme, and USB shortcuts.
- [ ] Test system diagnostics/report writing with `OPUS_DIAGNOSTICS_ENABLE.txt` present and absent; document the distinction from `OPUSMOD_SETTINGS.cfg`.
- [ ] Confirm a stock restore path, including MAIN, DECK, and MIXER components. If only MAIN has been restored, do not claim complete rollback validation.
- [ ] Photograph the final main screen, mod menu, settings panels, and representative feature behavior on hardware.

## Release handoff fields

Fill these in only after evidence is collected:

| Field | Value |
|---|---|
| Release tag | `TBD` |
| Artifact filename | `TBD` |
| SHA-256 | `TBD` |
| Tested device and stock baseline | `TBD` |
| Update completed to 100% | `TBD` |
| Cold boot verified | `TBD` |
| Settings persistence verified on hardware | `TBD` |
| Full stock rollback verified | `TBD` |
| Known issues | `TBD` |

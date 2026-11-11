# LED Diagnostics

USB-owned LED testing for R114 or newer. Requires `enable_diagnostics.txt` on a
mounted USB when the software starts. Enable this mod, select a player, colour
and LED group, then scroll to the named test buttons. Every pulse expires after
two seconds; Stop or disabling the mod restores native output immediately.

The groups cover the measured C00-C12, E00-E03, E05-E07, E09-E13 mappings.
Unmapped E04/E08 are deliberately not sent. Mono LEDs ignore the selected colour.
The existing SDK controller serializer recalculates the normal packet checksums;
the add-on does not modify audio, arbitrary controller bytes or GPIO.

# FPS Overlay

Measured native waveform repaint rate, not LCD scanout.
A diagnostic aid; it does not change frame timing.

Native service: `fps-overlay`. Requires R112 / SDK 1.2.1 (service protocol 2).
This add-on owns its menu, preferences and activation policy. The host supplies
the shared version-checked native backend; see ../../NATIVE-SERVICES.md.

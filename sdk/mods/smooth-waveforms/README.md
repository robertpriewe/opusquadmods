# Smooth Waveforms — USB-owned render policy

Native Height 30 remains the default. Owns period/allowlist and paint-clock policy.
Shared host hooks measure FPS and dispatch callbacks; they do not implement smoothing.
Requires the R113 retained-clock/render-provider interface. See ../../ENGINE-EXTRACTION.md.

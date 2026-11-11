# Four-Deck Waveforms — USB-owned layout engine

Owns lane/card/overview/mixer geometry, fast repair, both deck orders, card refresh and waveform height scaling.
Requires the R113 shared native update/paint provider and checked component primitives.
Cooperates with the separately external sidebar; native app hooks are not duplicated.
Native Height 30 geometry is retained. Physical rendering/FPS still needs regression.
See ../../ENGINE-EXTRACTION.md for lifecycle and restart rules.

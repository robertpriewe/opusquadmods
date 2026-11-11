# Slip Mode — USB-owned engine

Owns DirectionHandler swap, per-deck state convergence, cleanup and LED patterns.
Requires the R113 early input identity, native Slip primitives and disabled-cleanup ticks.
Held releases stay owned across disable; USB removal restores mod-owned Slip.
See ../../ENGINE-EXTRACTION.md for shared/native and hardware-validation boundaries.

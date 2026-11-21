# Deck audio (SDK 1.11)

`include/oqmod_deck_audio.h` lets a mod **change the four decks' audio** on its way to the
mixer. Your callback gets each deck as float left/right arrays (-1.0 to 1.0) and changes
them in place; the mixer receives the result. Example: `examples/deck-tone` (bass and
treble on all decks).

## Where this is in the signal path

```
EP136 (the app)                         mixer hardware (not reachable from a mod)
deck 1-4 playback -> Stems -> [deck audio insert] -> channel trim/EQ/fader -> crossfader
                                                     -> Beat FX -> + LINE/PHONO/MIC
                                                     -> MASTER / BOOTH / ZONE outputs
EP136 <- read-only copy for recording (the "master feed", MASTER-AUDIO.md)
```

- **You can change:** deck music, before the channel's trim, EQ and fader.
- **You cannot change:** anything the mixer does: channel EQ/fader, crossfader, Beat FX,
  LINE/PHONO/MIC, master, booth, zone. The final master/zone mix only exists inside the
  mixer hardware. So a real master or zone EQ that also covers FX and inputs is not
  possible from a mod; processing all four decks alike is the closest you get, and master
  and zone always get the same deck audio.
- A deck whose mixer channel is set to another input (LINE, PHONO, ...) is not heard.

## Use

```c
#include "oqmod_deck_audio.h"     /* include first: it needs _GNU_SOURCE */
static void process(struct oq_deck_audio_block *b, void *user) {
    for (unsigned d = 0; d < OQ_DECK_AUDIO_DECKS; d++)
        for (unsigned i = 0; i < b->frames; i++) { b->left[d][i] *= 0.5f; b->right[d][i] *= 0.5f; }
}
/* oqmod_init: */  oq_deck_audio_install(process, NULL);   /* once; returns 0 when hooked */
/* enabled():  */  oq_deck_audio_enable(on);               /* starts off */
/* tick():     */  oq_deck_audio_status(&s);               /* format, blocks, slowest call */
```

- `left[d]` / `right[d]`: deck `d+1`. Deck 1 = pair 0 is confirmed on hardware; decks 2-4
  = pairs 1-3 is the natural order, not yet confirmed.
- `frames` is small (EP136 writes about 700 times a second), `sample_rate` 44100.
- Results are clipped to the stream's range; NaN becomes silence. The 8 deck channels pass
  through float (24-bit precision, inaudible); other channels of the stream stay bit-exact.
- `oq_deck_audio_status()`: `installed`, `processed`, `skipped` (blocks over 1024 frames),
  `max_ns` (slowest callback including conversion), the stream's `channels` / `format` /
  `sample_rate`, and `previous` (what the hook chained onto: `libasound` or another mod).

## Rules

- The callback runs on EP136's audio thread: no allocation, locks, file or network I/O,
  logging or SDK calls. Keep it well under 1 ms (check `max_ns`). Hand settings over
  without locks, e.g. two coefficient sets and an atomic index (see the example).
- Keep filter state per deck and per channel; it continues across calls.
- There is no remove: the firmware never unloads mods, and several mods chain on the same
  hook. Switch processing with `oq_deck_audio_enable(0)` (the audio then passes untouched).

## Sharing with other mods

Every mod that uses this header gets its own hook, chained in the order the mods start:
each mod receives the previous one's output. Stems, Mobius and Mono Master use the same
hook point and chain the same way.

## How it works

EP136 1.32 (firmware R120-R125) writes the deck audio with one call to `snd_pcm_writei`
(`0x1e6a544`) through GOT slot `0x349a130`: interleaved S32_LE, 10 channels, 44.1 kHz,
pairs 1-4 = decks. The header swaps that slot for its own function (chaining to whatever
was there), reads the stream's format with `snd_pcm_hw_params_current`, takes the first
stream with 8-16 channels in S32, S24 or float, converts the deck pairs to float, calls
you, converts back into a private copy and passes that on (EP136's buffer is never
written). `install()` returns `OQ_DECK_AUDIO_UNSUPPORTED` if the slot cannot be read or
holds neither libasound's `snd_pcm_writei` nor another mod's hook (other firmware).

Tests: `tests/deck_audio/run.sh` (Docker, ARM64): two mods chained, conversion,
clipping, NaN, pass-through when off, other streams untouched, status.

# Master audio

## Current status (R118)

**R118 and later provide a read-only audio feed** from the deck's USB recorder, delivered
to add-ons in 64-frame blocks at 44.1 kHz. **Whether it is the post-fader master is not
confirmed:** in an R119 run with Master Meter 1.2 the level stayed between -12 and -8 dB
for 26 s and never dropped toward silence, although the fader and master test was meant
to happen in that window. Treat it as the recording feed until a controlled test shows
it follows the faders and the master knob.

**It cannot change what you hear.** On OPUS the mixing (faders, EQ, crossfader,
master) happens in the mixer hardware; EP136 only receives a copy for recording. So a
master EQ or limiter is still impossible: `oq_master_audio_discover()` (writable insert)
returns NULL on every firmware.

Use it for meters, spectrum displays, beat or key detection, visualisers, recording or
streaming. Example: `examples/master-meter`.

```c
#include "oqmod_master_audio.h"
static const struct oq_master_audio_api *master;
static void on_block(const struct oq_master_audio_block *b, void *user) {
    /* audio thread: b->frames stereo frames in b->interleaved (L,R,L,R...), a copy */
}
static void enabled(int on) {
    if (on) {
        master = oq_master_audio_discover_tap();          /* NULL before R118 */
        if (master) oq_master_audio_tap_install(master, on_block, NULL);
    } else if (master) {
        oq_master_audio_tap_remove(master, on_block, NULL);
    }
}
```

Rules:
- The callback runs on the audio thread: no allocation, locks, file or network I/O,
  logging or SDK calls. Keep it short; copy what you need into your own buffers.
- Install and remove from your mod's own callbacks (`oqmod_init`, `tick`, `enabled`).
  A tap belongs to the mod that installed it. It is skipped while that mod is switched
  off or its USB is gone, and only that mod can remove it.
- Up to four mods can listen at once; a fifth install returns `OQ_MASTER_AUDIO_BUSY`.
- `remove()` waits until a callback that is still running has finished.
- The block is a copy: writing to it changes nothing.
- Not verified yet: whether the MIC, Beat FX and external inputs are in the feed.

How it works: the firmware wraps `meow::AudioRecorderUnit`'s `processBlock` (vtable
`0x29ace60`, slot 7) and `prepareToPlay` (slot 4). EP136 runs two recorder units with
identical audio, so the feed takes one of them and switches to the other if the first
stops for 50 ms. The report line `master_audio hooks=3 rate=... taps=... blocks=...`
shows its state.

## History: how the feed was found (R117)

EP136 records the master to USB through `meow::AudioRecorderUnit`, a JUCE audio
processor (vtable `0x29ace60`). Its `processBlock(AudioBuffer<float>&, MidiBuffer&)` is
slot 7 (`0x29ace98`, stock `0x1235e00`). If that processor receives the final master
continuously, the firmware can offer a **read-only master feed**: levels, spectrum,
beat detection, visualisers, recording. It still could not change what comes out of
the speakers, because the copy that reaches EP136 is not the one that plays.

`tools/master-probe` (diagnostic mod, no flash) reads that buffer and shows channel
count, block size, rate and per-channel level. The questions for the deck:

1. Does it run when USB REC is idle, or only while recording?
2. Do its levels follow the master knob, channel faders, crossfader, Beat FX and MIC?
3. What are the channel count and sample rate?

Hardware result (R117, probe 0.2, 2026-11-09): it runs continuously, also with nothing
playing or recording. There are two recorder units; both get identical stereo audio at
44.1 kHz in 64-sample blocks (689 blocks a second each). Levels follow the channel
faders and, by the user's observation, the master knob; silence reads -120 dB. So this
is a read-only copy of the post-master mix. Still unverified: MIC, Beat FX and
external inputs, and whether the knob dependence is exact.

Until a provider is built into the firmware, discovery keeps returning NULL. A writable master EQ still needs a writable point in the final stereo
path, which is likely to be in the mixer DSP, not in EP136.

## Why the AZ audio code does not supply a master EQ

The current primary AZ source documents `sdk_audio.c` as a **read-only per-deck**
tap of a ten-channel ALSA output, before the hardware mixer. It identifies four
stereo deck pairs and no final master mix in that stream. Subscribers receive
copies off the audio thread; processing those copies cannot change the output.

Reference checked 2026-11-07:
https://github.com/Kyle-Hosman/xdj-az-mods/blob/main/src/azmod/sdk_audio.c

Do not copy the AZ channel assumptions onto OPUS without verification. In
particular, summing deck streams in an add-on is not a master: native faders,
crossfader, EQ, Beat/Color FX, external LINE/PHONO inputs, microphones, routing and
master/ZONE controls may live elsewhere. Filtering deck playback can change the
dry audio without controlling the actual final stereo bus.

## OPUS evidence, not a claim of final-master access

Read-only inspection of stock 1.32 EP136 on 2026-11-07:

- Binary SHA-256:
  `811a6b7467994ff3e6110c667edd805028cfc43e2e340c2e332ef4a8d3ff4e22`.
- Both interleaved and noninterleaved playback/capture ALSA entry points exist.
  Presence of a symbol does not establish the source or routing of its samples.
- One direct call to `snd_pcm_writei@plt` (`0x414610`) at `0x1e6a544`;
  one to `snd_pcm_writen@plt` (`0x415040`) at `0x1e6a244`.
- Capture calls to `snd_pcm_readi@plt` (`0x4171b0`) at `0x1e6a0cc`, and
  `snd_pcm_readn@plt` (`0x4179d0`) at `0x1e6a2c8`.
- Channel negotiation calls `snd_pcm_hw_params_set_channels@plt` (`0x414a20`)
  at `0x1e6a6cc`, with the channel count passed as a variable. This does NOT
  establish ten channels or a master pair on OPUS.
- These calls lie in the generic JUCE ALSA device region. Its capture branch is
  a research lead only: it has not been established as a master loopback, and a
  read-only return would still not provide an insert point for EQ.
- Existing `port_readiness.h` and oqstem hooks target `DjPlayerUnit` processing
  and `PageBuffer::read`: per-player paths, not a verified final-master bus.

The inspection does not prove that every possible master path is absent. It
establishes that none of the hooks currently mapped can honestly be named a
writable final-master callback. No ALSA/device configuration, mixer/DSP image,
hardware output, USB contents or firmware candidate was modified.

Reproduce the relocation/direct-call inventory without third-party dependencies:

```sh
python3 host-r79-lab/tools/inspect_master_audio.py work/ep136-re/EP136
```

The tool rejects an unknown executable, checks each ALSA PLT entry against its
named ELF relocation and checks all direct calls to those entries. Its report
explicitly keeps channel mapping, master routing and audio behavior unverified.

## Proposed extension contract

No fields are added to `oqmod_api` or `oqmod_info`, preserving their ABI. A future
host may explicitly export `opusmod_master_audio_api_v1(abi)` once a real route is
verified. Discovery checks version, structure size, final-stereo source, stereo
float format, verified-route and writable-insert capabilities, block bounds, and
both installation/removal operations. Any missing requirement means unavailable.

The callback receives an in-place interleaved float stereo buffer and per-block
sample rate. Do not assume EP136's deck render rate equals hardware output rate.
Only samples may be changed; buffer lifetime ends when the callback returns.

This contract supports one exclusive processor, not an unbounded plugin chain.
Registration/removal occur on a control thread, never on an audio callback.
Removing an insert must wait for in-flight callbacks to finish, on the control
thread only. Provider ownership and add-on enable/disable/USB-loss guards are
mandatory; merely exposing a raw callback pointer is not an implementation.

Inside processing: no file/network I/O, allocation/free, mutexes, sleeps, logging,
UI calls, ordinary SDK controls or unbounded work. Allocate DSP state in advance;
use bounded parameter snapshots and smooth gain changes. A native `.so` is not
sandboxed: a bad processor can still crash, distort, mute or overload the player.

Example capability handling (not a complete EQ):

```c
#include "oqmod_master_audio.h"
static const struct oq_master_audio_api *master;
/* During initialization, outside the audio thread: */
master = oq_master_audio_discover();
if (!master) {
    /* Status: "Final-master audio unavailable on this firmware".
       Keep the EQ disabled; do not substitute a per-deck stream. */
}
```

## Native backend acceptance gate

Before implementing or exporting the provider:

1. Observe negotiated OPUS PCM device identity, stream direction, format, rate,
   channel count and caller with bounded read-only instrumentation. Do not guess
   channel identities or log/copy large audio blocks on the realtime thread.
2. Correlate candidate channels with isolated decks and external inputs. Check
   native faders, crossfader, EQ, FX, microphones, master/ZONE and recording routes.
3. Locate a writable point in the final stereo path, not a monitoring copy or a
   pre-mixer deck pair. If it exists only in hardware/DSP, an EP136 SDK change
   alone cannot create it: separate verified mixer/DSP work is required.
4. Establish exact build identity, pointer/buffer ownership, sample format,
   negotiated rate, channel routing and callback execution context.
5. Implement one-owner registration, inactive passthrough, USB loss handling,
   stream reconfiguration/loss, teardown quiescence, bounds and measured deadline
   behavior without realtime allocation or blocking.
6. Bench-test bit-preserving bypass first, then low-level gain and EQ response,
   clipping/headroom and state transitions at safe monitor volume. Report actual
   analog/digital output measurements separately from callback tests.

No active hook, new flash or physical knob experiment is needed just to use or
test this header. `tests/master_audio_test.c` tests only interface validation with
a fake provider; its passing does not establish a master audio path on hardware.

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * @file ep147rate.h
 * @brief EP147's deck engine sample rate: the one place azstem names it.
 *
 * EP147's audio device runs at 96 kHz (the ALSA side negotiates 96 kHz S32), but
 * every deck renders, positions and buffers at 44100 Hz, whatever the file's rate:
 *
 *   - PageBuffer::read positions and the playhead sample counter are 44.1 kHz source
 *     samples for 44.1 kHz and 48 kHz files: EP147 resamples a 48 kHz file to 44.1 kHz
 *     before the page cache (deck survey of 48 kHz files, measured in 44.1 kHz units,
 *     with stems injected at 44.1 kHz staying sample-aligned on them).
 *   - EP147 keeps a {44100, 1/44.1} sample-rate pair in its playback snapshot
 *     (written by the snapshot updater; measured live).
 *   - DjPlayerAudioProcessor::processBlockInternal's output is 44.1 kHz: the drum roll
 *     mixes 44.1 kHz samples into it one frame per frame and they play at their own
 *     pitch, with the hit period matching the deck's BPM.
 *
 * Nothing in EP147 offers a setting that changes this, so it is a constant, not a
 * value read at run time. Data that EP147 builds with a rate of its own is not assumed
 * to use it: the beat grid is checked against its own tempo field and converted
 * (drumroll.c, drum_grid_native), because one of EP147's grid producers converts
 * microseconds to samples with a rate it is handed at run time.
 */
#ifndef AZSTEM_EP147RATE_H
#define AZSTEM_EP147RATE_H

#define AZ_EP147_RATE 44100 /* Hz: deck render rate, playhead and PageBuffer time base */

#endif

/* SPDX-License-Identifier: GPL-3.0-only */
/* aztap.h: a lock-free audio tap: one ring per source, filled on EP147's audio thread and
 * drained by a core thread that hands blocks to the community mods that asked for them.
 *
 * One producer (the audio thread) and one consumer (the tap thread) per ring. The producer
 * does no system call, allocation or lock: when nobody wants the source it returns after one
 * atomic load, and when the ring has no room for a whole block it drops the block and counts
 * it. It never waits, so a slow consumer costs lost blocks, never a late audio deadline.
 *
 * Frames are interleaved stereo float, full scale +-1.0. Header-only. */
#ifndef AZTAP_H
#define AZTAP_H
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#define AZTAP_FRAMES 16384 /* per ring, a power of two: about 0.37 s at 44.1 kHz */

struct aztap_ring {
    _Atomic unsigned w;       /* frames written, free-running (producer) */
    _Atomic unsigned r;       /* frames read, free-running (consumer) */
    _Atomic int want;         /* a mod is subscribed to this source (consumer side sets it) */
    _Atomic unsigned dropped; /* frames dropped because the ring was full */
    float lr[AZTAP_FRAMES * 2];
};

static inline int aztap_wanted(struct aztap_ring *q) {
    return atomic_load_explicit(&q->want, memory_order_relaxed);
}

/* Room for n frames? If not, count them as dropped. Returns the write index or -1. */
static inline long aztap_reserve(struct aztap_ring *q, int n) {
    unsigned w = atomic_load_explicit(&q->w, memory_order_relaxed);
    unsigned r = atomic_load_explicit(&q->r, memory_order_acquire);
    if (n <= 0 || (unsigned) n > AZTAP_FRAMES - (w - r)) {
        if (n > 0)
            atomic_fetch_add_explicit(&q->dropped, (unsigned) n, memory_order_relaxed);
        return -1;
    }
    return (long) w;
}

static inline void aztap_commit(struct aztap_ring *q, unsigned w, int n) {
    atomic_store_explicit(&q->w, w + (unsigned) n, memory_order_release);
}

/* Audio thread: planar float channels (a JUCE buffer). R may equal L for a mono source. */
static inline void aztap_push_planar(struct aztap_ring *q, const float *L, const float *R, int n) {
    if (!aztap_wanted(q) || !L || !R)
        return;
    long w0 = aztap_reserve(q, n);
    if (w0 < 0)
        return;
    unsigned w = (unsigned) w0;
    for (int i = 0; i < n; i++) {
        unsigned k = ((w + (unsigned) i) & (AZTAP_FRAMES - 1)) * 2;
        q->lr[k] = L[i];
        q->lr[k + 1] = R[i];
    }
    aztap_commit(q, w, n);
}

/* Audio thread: interleaved signed 32-bit frames of `stride` channels; channels 0/1 are taken. */
static inline void aztap_push_s32(struct aztap_ring *q, const int32_t *f, int stride, int n) {
    if (!aztap_wanted(q) || !f || stride < 2)
        return;
    long w0 = aztap_reserve(q, n);
    if (w0 < 0)
        return;
    unsigned w = (unsigned) w0;
    const float k32 = 1.0f / 2147483648.0f;
    for (int i = 0; i < n; i++) {
        unsigned k = ((w + (unsigned) i) & (AZTAP_FRAMES - 1)) * 2;
        q->lr[k] = (float) f[(long) i * stride] * k32;
        q->lr[k + 1] = (float) f[(long) i * stride + 1] * k32;
    }
    aztap_commit(q, w, n);
}

/* Consumer: copy up to max frames into lr (interleaved). Returns the frames copied. */
static inline int aztap_pop(struct aztap_ring *q, float *lr, int max) {
    unsigned r = atomic_load_explicit(&q->r, memory_order_relaxed);
    unsigned w = atomic_load_explicit(&q->w, memory_order_acquire);
    unsigned n = w - r;
    if (n > (unsigned) max)
        n = (unsigned) max;
    for (unsigned i = 0; i < n; i++) {
        unsigned k = ((r + i) & (AZTAP_FRAMES - 1)) * 2;
        lr[i * 2] = q->lr[k];
        lr[i * 2 + 1] = q->lr[k + 1];
    }
    atomic_store_explicit(&q->r, r + n, memory_order_release);
    return (int) n;
}

/* Consumer: turn the tap on or off. Turning it on starts from an empty ring, so a block never
 * mixes audio from before the subscription with audio after it. */
static inline void aztap_set_wanted(struct aztap_ring *q, int on) {
    if (on && !aztap_wanted(q))
        atomic_store_explicit(&q->r, atomic_load_explicit(&q->w, memory_order_acquire),
                              memory_order_release);
    atomic_store_explicit(&q->want, on ? 1 : 0, memory_order_relaxed);
}

#endif

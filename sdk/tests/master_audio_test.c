/* Contract tests only. The fake provider is not a hardware audio implementation. */
#include <assert.h>
#include <stdio.h>
#include "oqmod_master_audio.h"

static oq_master_audio_process_fn installed;
static void *owner;
static int calls;
static int install(oq_master_audio_process_fn fn, void *user) {
    if (installed) return OQ_MASTER_AUDIO_BUSY;
    installed = fn; owner = user; calls++; return OQ_MASTER_AUDIO_OK;
}
static int remove_processor(oq_master_audio_process_fn fn, void *user) {
    if (installed != fn || owner != user) return OQ_MASTER_AUDIO_NOT_OWNER;
    installed = NULL; owner = NULL; calls++; return OQ_MASTER_AUDIO_OK;
}
static void halve(const struct oq_master_audio_block *b, void *user) {
    assert(b && b->size >= sizeof *b && b->channels == 2 && b->sample_rate == 48000);
    assert(user == &calls);
    for (uint32_t i = 0; i < b->frames * 2; i++) b->interleaved[i] *= 0.5f;
}
int main(void) {
    assert(!oq_master_audio_discover()); /* no host export in an SDK-only executable */
    assert(!oq_master_audio_supported(NULL));
    assert(oq_master_audio_install(NULL, halve, &calls) == OQ_MASTER_AUDIO_UNAVAILABLE);
    assert(oq_master_audio_remove(NULL, halve, &calls) == OQ_MASTER_AUDIO_UNAVAILABLE);
    assert(oq_master_audio_install(NULL, NULL, NULL) == OQ_MASTER_AUDIO_INVALID);
    struct oq_master_audio_api a = {
        .version = 1, .size = sizeof a, .source = OQ_MASTER_AUDIO_FINAL_STEREO,
        .format = OQ_MASTER_AUDIO_F32_INTERLEAVED, .channels = 2,
        .flags = OQ_MASTER_AUDIO_VERIFIED_ROUTE | OQ_MASTER_AUDIO_WRITABLE_INSERT,
        .max_frames = 4096, .install = install, .remove = remove_processor
    };
    assert(oq_master_audio_supported(&a));
    for (int i = 0; i < 9; i++) {
        struct oq_master_audio_api bad = a;
        switch (i) {
        case 0: bad.version = 2; break;
        case 1: bad.size = offsetof(struct oq_master_audio_api, remove); break;
        case 2: bad.source = 0; break; /* never accept an unspecified/deck source */
        case 3: bad.format = 0; break;
        case 4: bad.channels = 10; break; /* multichannel playback is not a stereo master */
        case 5: bad.flags &= ~OQ_MASTER_AUDIO_VERIFIED_ROUTE; break;
        case 6: bad.flags &= ~OQ_MASTER_AUDIO_WRITABLE_INSERT; break; /* tap is not EQ */
        case 7: bad.max_frames = 65537; break;
        case 8: bad.remove = NULL; break;
        }
        assert(!oq_master_audio_supported(&bad));
        assert(oq_master_audio_install(&bad, halve, &calls) == OQ_MASTER_AUDIO_UNAVAILABLE);
    }
    assert(calls == 0);
    /* A short provider struct must not be read beyond its version/size prefix. */
    struct {uint32_t version, size;} old = {1, sizeof old};
    assert(!oq_master_audio_supported((const struct oq_master_audio_api *)(const void *)&old));
    assert(oq_master_audio_install(&a, halve, &calls) == OQ_MASTER_AUDIO_OK);
    assert(oq_master_audio_install(&a, halve, &calls) == OQ_MASTER_AUDIO_BUSY);
    float lr[] = {1.f, -1.f, 0.5f, -0.5f};
    struct oq_master_audio_block b = {sizeof b, 2, 48000, 2, lr};
    installed(&b, owner);
    assert(lr[0] == 0.5f && lr[1] == -0.5f && lr[2] == 0.25f && lr[3] == -0.25f);
    assert(oq_master_audio_remove(&a, halve, NULL) == OQ_MASTER_AUDIO_NOT_OWNER);
    assert(oq_master_audio_remove(&a, halve, &calls) == OQ_MASTER_AUDIO_OK && !installed);
    assert(calls == 2);
    /* R118 read-only tap: accepted only as a tap, never as a writable insert, and the
     * writable helpers refuse it. */
    assert(!oq_master_audio_discover_tap());   /* no host export in an SDK-only executable */
    struct oq_master_audio_api tap = a;
    tap.flags = OQ_MASTER_AUDIO_VERIFIED_ROUTE | OQ_MASTER_AUDIO_READ_ONLY;
    assert(oq_master_audio_tap_supported(&tap) && !oq_master_audio_supported(&tap));
    assert(oq_master_audio_install(&tap, halve, &calls) == OQ_MASTER_AUDIO_UNAVAILABLE);
    assert(!oq_master_audio_tap_supported(&a));             /* a writable insert is not a tap */
    for (int i = 0; i < 6; i++) {
        struct oq_master_audio_api bad = tap;
        switch (i) {
        case 0: bad.flags = OQ_MASTER_AUDIO_READ_ONLY; break;     /* unverified route */
        case 1: bad.source = 0; break;
        case 2: bad.channels = 10; break;
        case 3: bad.max_frames = 0; break;
        case 4: bad.install = NULL; break;
        case 5: bad.version = 2; break;
        }
        assert(!oq_master_audio_tap_supported(&bad));
        assert(oq_master_audio_tap_install(&bad, halve, &calls) == OQ_MASTER_AUDIO_UNAVAILABLE);
    }
    assert(oq_master_audio_tap_install(&tap, NULL, NULL) == OQ_MASTER_AUDIO_INVALID);
    assert(oq_master_audio_tap_install(&tap, halve, &calls) == OQ_MASTER_AUDIO_OK);
    assert(oq_master_audio_tap_remove(&tap, halve, &calls) == OQ_MASTER_AUDIO_OK);
    puts("PASS: master-audio tap contract (R118): read-only providers accepted only by the tap helpers, writable helpers refuse them, invalid taps refused");
    puts("PASS: master-audio optional contract: absent/short/wrong/unverified/read-only providers refused; fake insert/ownership only, NO physical master backend");
}

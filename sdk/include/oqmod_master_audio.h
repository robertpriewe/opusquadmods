#ifndef OQMOD_MASTER_AUDIO_H
#define OQMOD_MASTER_AUDIO_H
/* Final-master audio for add-ons.
 *
 * R118 and later provide a READ-ONLY master feed ("tap"): a copy of the final stereo mix
 * as EP136's USB recorder receives it, verified on hardware to follow the channel faders
 * and the master knob. Use oq_master_audio_discover_tap(). A tap cannot change what you
 * hear: on OPUS the mixing happens in the mixer hardware.
 * A writable insert (oq_master_audio_discover(), for a master EQ) remains unavailable on
 * every firmware: it returns NULL.
 * No deck audio, source sum or ALSA stream may masquerade as the final master.
 * See MASTER-AUDIO.md before implementing a provider or an audio mod. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>

#define OQ_MASTER_AUDIO_ABI 1u
#define OQ_MASTER_AUDIO_PROVIDER "opusmod_master_audio_api_v1"

enum {
    OQ_MASTER_AUDIO_OK = 0,
    OQ_MASTER_AUDIO_UNAVAILABLE = -1,
    OQ_MASTER_AUDIO_INVALID = -2,
    OQ_MASTER_AUDIO_BUSY = -3,
    OQ_MASTER_AUDIO_NOT_OWNER = -4
};
enum { OQ_MASTER_AUDIO_FINAL_STEREO = 1 };
enum { OQ_MASTER_AUDIO_F32_INTERLEAVED = 1 };
enum {
    OQ_MASTER_AUDIO_VERIFIED_ROUTE = 1u,
    OQ_MASTER_AUDIO_WRITABLE_INSERT = 2u,
    OQ_MASTER_AUDIO_READ_ONLY = 4u      /* R118: blocks are copies; writing them has no effect */
};

/* Provider-owned buffer, valid only during the callback. Exactly frames*2
 * floats: L,R,L,R. Do not retain the pointer or change any metadata. Rate is
 * supplied per block; never assume the deck engine and hardware rates match.
 * No allocation, locks, I/O, logging, GUI/SDK control calls or unbounded work. */
struct oq_master_audio_block {
    uint32_t size, frames, sample_rate, channels;
    float *interleaved;
};
typedef void (*oq_master_audio_process_fn)(const struct oq_master_audio_block *, void *user);

/* Versioned separately; the existing oqmod_api/oqmod_info layouts are unchanged.
 * One exclusive processor. install/remove are control-thread operations.
 * Successful remove must quiesce in-flight callbacks before returning. A
 * provider must also bypass disabled/unplugged/failed add-ons, not rely on a
 * well-behaved add-on to remove itself. No production provider exists yet. */
struct oq_master_audio_api {
    uint32_t version, size;
    uint32_t source, format, channels, flags, max_frames;
    int (*install)(oq_master_audio_process_fn process, void *user);
    int (*remove)(oq_master_audio_process_fn process, void *user);
};
typedef const struct oq_master_audio_api *(*oq_master_audio_provider_fn)(uint32_t abi);

static inline int oq_master_audio_supported(const struct oq_master_audio_api *a) {
    const uint32_t required = OQ_MASTER_AUDIO_VERIFIED_ROUTE | OQ_MASTER_AUDIO_WRITABLE_INSERT;
    return a && a->version == OQ_MASTER_AUDIO_ABI && a->size >= sizeof *a &&
        a->source == OQ_MASTER_AUDIO_FINAL_STEREO &&
        a->format == OQ_MASTER_AUDIO_F32_INTERLEAVED && a->channels == 2 &&
        (a->flags & required) == required && a->max_frames > 0 && a->max_frames <= 65536 &&
        a->install && a->remove;
}

/* No probing of firmware memory, guessed offsets, PCM writes or worker startup.
 * This lookup only checks for a future explicit host export. Absence is normal. */
static inline const struct oq_master_audio_api *oq_master_audio_discover(void) {
    /* POSIX main-program scope avoids requiring glibc's _GNU_SOURCE macro in
     * every add-on, even if another system header was included before this one.
     * No external library is opened; the already-loaded host owns the export. */
    void *scope = dlopen(NULL, RTLD_LAZY);
    if (!scope) return NULL;
    void *symbol = dlsym(scope, OQ_MASTER_AUDIO_PROVIDER);
    dlclose(scope);
    oq_master_audio_provider_fn provider = NULL;
    _Static_assert(sizeof provider == sizeof symbol, "POSIX function-pointer representation");
    if (!symbol) return NULL;
    memcpy(&provider, &symbol, sizeof provider);
    const struct oq_master_audio_api *a = provider(OQ_MASTER_AUDIO_ABI);
    return oq_master_audio_supported(a) ? a : NULL;
}

/* R118 read-only tap. The callback gets a provider-owned COPY of each block (writing it
 * changes nothing), on the audio thread, with the same rules as a writable processor:
 * no allocation, locks, I/O, logging or SDK calls. Up to four taps; each belongs to the
 * add-on that installed it (call install/remove from oqmod_init, tick or enabled), is
 * skipped while that add-on is switched off or its USB is gone, and only that add-on
 * may remove it. remove() waits for a running callback to finish. */
static inline int oq_master_audio_tap_supported(const struct oq_master_audio_api *a) {
    const uint32_t required = OQ_MASTER_AUDIO_VERIFIED_ROUTE | OQ_MASTER_AUDIO_READ_ONLY;
    return a && a->version == OQ_MASTER_AUDIO_ABI && a->size >= sizeof *a &&
        a->source == OQ_MASTER_AUDIO_FINAL_STEREO &&
        a->format == OQ_MASTER_AUDIO_F32_INTERLEAVED && a->channels == 2 &&
        (a->flags & required) == required && a->max_frames > 0 && a->max_frames <= 65536 &&
        a->install && a->remove;
}
static inline const struct oq_master_audio_api *oq_master_audio_discover_tap(void) {
    void *scope = dlopen(NULL, RTLD_LAZY);
    if (!scope) return NULL;
    void *symbol = dlsym(scope, OQ_MASTER_AUDIO_PROVIDER);
    dlclose(scope);
    oq_master_audio_provider_fn provider = NULL;
    if (!symbol) return NULL;
    memcpy(&provider, &symbol, sizeof provider);
    const struct oq_master_audio_api *a = provider(OQ_MASTER_AUDIO_ABI);
    return oq_master_audio_tap_supported(a) ? a : NULL;
}
static inline int oq_master_audio_tap_install(const struct oq_master_audio_api *a,
                                              oq_master_audio_process_fn process, void *user) {
    if (!process) return OQ_MASTER_AUDIO_INVALID;
    return oq_master_audio_tap_supported(a) ? a->install(process, user) : OQ_MASTER_AUDIO_UNAVAILABLE;
}
static inline int oq_master_audio_tap_remove(const struct oq_master_audio_api *a,
                                             oq_master_audio_process_fn process, void *user) {
    if (!process) return OQ_MASTER_AUDIO_INVALID;
    return oq_master_audio_tap_supported(a) ? a->remove(process, user) : OQ_MASTER_AUDIO_UNAVAILABLE;
}

static inline int oq_master_audio_install(const struct oq_master_audio_api *a,
                                          oq_master_audio_process_fn process, void *user) {
    if (!process) return OQ_MASTER_AUDIO_INVALID;
    return oq_master_audio_supported(a) ? a->install(process, user) : OQ_MASTER_AUDIO_UNAVAILABLE;
}
static inline int oq_master_audio_remove(const struct oq_master_audio_api *a,
                                         oq_master_audio_process_fn process, void *user) {
    if (!process) return OQ_MASTER_AUDIO_INVALID;
    return oq_master_audio_supported(a) ? a->remove(process, user) : OQ_MASTER_AUDIO_UNAVAILABLE;
}
#endif

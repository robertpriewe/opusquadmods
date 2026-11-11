#ifndef OQMOD_STEMS_H
#define OQMOD_STEMS_H
#include <stdint.h>
/* Early native audio trampolines, NOT a final-master callback. These are the
 * existing EP136 stem paths. Native hooks stay in the foundation, algorithms
 * and decoders stay in the resident USB module. Bind during init only, then
 * publish once, after ALL engine state and originals have been initialized.
 * No late 16-byte code patching, live unloading or replacement is supported.
 * The engine must keep OFF passthrough tracking/destructor callbacks safe. */
struct oqmod_stems_audio {
    uint32_t size;
    int (*ready)(void);
    void *(*bind)(uintptr_t native_function,void *callback,void **original);
    int (*publish)(void);
};
struct oqmod_stems_context {uint32_t size,fourlane,layout,order,pitch;};
#endif

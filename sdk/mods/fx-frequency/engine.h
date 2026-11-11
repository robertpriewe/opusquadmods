/* Complete USB-owned FX Frequency packet engine. Core supplies only generic
 * checked hook installation, monotonic time and transport-readiness capability.
 * The original builder and downstream CRC are never replaced by this add-on. */
#ifndef OQ_FREQUENCY_ENGINE_H
#define OQ_FREQUENCY_ENGINE_H
#include <stdint.h>
#include <stddef.h>
#include "fx_xy_policy.h"
#ifndef OQ_FX_WORD
#define OQ_FX_WORD(address) (*(const uint32_t *)(uintptr_t)(address))
#endif
struct oq_fx_engine {
    const struct oqmod_api *api;
    unsigned hooks,enabled,selection,rx,tx,changed,bad_object;
    uintptr_t feedback_object;
    uint64_t feedback;
    void (*receive_original)(void *,unsigned);
    void (*serialize_original)(void *,unsigned char *);
};
static struct oq_fx_engine fx_engine;
static void fx_receive(void *object,unsigned on){
    fx_engine.receive_original(object,on);
    if(object&&*(uintptr_t *)object==0x2995bb8ul&&on<=1){
        uint64_t stamp=fx_engine.api->now_ms();
        __atomic_store_n(&fx_engine.feedback_object,(uintptr_t)object,__ATOMIC_RELEASE);
        __atomic_store_n(&fx_engine.feedback,(stamp<<2)|2|on,__ATOMIC_RELEASE);
        __atomic_fetch_add(&fx_engine.rx,1,__ATOMIC_RELAXED);
    }
}
static void fx_serialize(void *builder,unsigned char *sub){
    fx_engine.serialize_original(builder,sub);
    if(!builder||!sub||*(uintptr_t *)builder!=0x299db28ul)return;
    uintptr_t mixer=*(uintptr_t *)((char *)builder+0x20);
    if(!mixer||*(uintptr_t *)(mixer+0x520)!=0x2995bb8ul){
        __atomic_fetch_add(&fx_engine.bad_object,1,__ATOMIC_RELAXED);return;
    }
    uint64_t feedback=__atomic_load_n(&fx_engine.feedback,__ATOMIC_ACQUIRE);
    struct fp_state state={
        .enabled=fx_engine.hooks==3&&__atomic_load_n(&fx_engine.enabled,__ATOMIC_ACQUIRE),
        .selection=__atomic_load_n(&fx_engine.selection,__ATOMIC_ACQUIRE),
        .known=(feedback&2)&&__atomic_load_n(&fx_engine.feedback_object,__ATOMIC_ACQUIRE)==mixer+0x520,
        .on=feedback&1,.feedback_ms=feedback>>2
    };
    unsigned result=fp_apply(&state,state.enabled?fx_engine.api->now_ms():0,sub,9);
    __atomic_fetch_add(&fx_engine.tx,1,__ATOMIC_RELAXED);
    if(result==FP_CHANGED)__atomic_fetch_add(&fx_engine.changed,1,__ATOMIC_RELAXED);
}
static unsigned fx_install(const struct oqmod_api *a){
    fx_engine.api=a;
    if(!a->ep136_ok||!a->hook||!a->now_ms||!a->ep136_ok()||
       OQ_FX_WORD(0x1ce8f70)!=0xa9bc7bfdu||OQ_FX_WORD(0x1d0e8c8)!=0xf9401000u||
       OQ_FX_WORD(0x1d0f33c)!=0x97fffd63u)return 0;
    /* TIME already owns the shared builder's vtable slot at USB load time.
     * The core capability checked its original identity before that happened.
     * These independent helper hooks do not compete for that vtable slot. */
    if(a->hook(0x1ce8f70,0xa9bc7bfd,(void *)fx_receive,(void **)&fx_engine.receive_original)==1)fx_engine.hooks=1;
    if(fx_engine.hooks==1&&a->hook(0x1d0e8c8,0xf9401000,(void *)fx_serialize,(void **)&fx_engine.serialize_original)==1)
        fx_engine.hooks=3;
    return fx_engine.hooks;
}
static void fx_engine_set(int enabled,unsigned selection){
    __atomic_store_n(&fx_engine.selection,selection,__ATOMIC_RELEASE);
    if(!enabled||!__atomic_load_n(&fx_engine.enabled,__ATOMIC_ACQUIRE)){
        /* New activation requires fresh feedback, including USB resume. */
        __atomic_store_n(&fx_engine.feedback,0,__ATOMIC_RELEASE);
        __atomic_store_n(&fx_engine.feedback_object,0,__ATOMIC_RELEASE);
    }
    __atomic_store_n(&fx_engine.enabled,!!enabled,__ATOMIC_RELEASE);
}
#endif

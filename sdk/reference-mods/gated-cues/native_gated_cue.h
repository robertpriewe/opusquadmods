/* Paused-only gated hot cues with overlapping cue-key support. Additional cues
 * can retarget an audition. The first cue-key release ends playback, unless
 * PLAY was pressed while a cue was held. */
struct gc_op {unsigned pad,mode;};
struct gc_hold {uintptr_t handler,facade,core;unsigned pad,latched;};
#include <pthread.h>
static pthread_mutex_t gc_lock=PTHREAD_MUTEX_INITIALIZER;
static struct gc_hold gc_held[4][8]; /* native HotCue task thread owns these */
static unsigned gc_last_pad[4],gc_deck_latched[4],gc_ended[4];
static unsigned gc_enabled,gc_hooks,gc_started,gc_finished,gc_passed,gc_rejected,gc_held_mask;
static unsigned gc_play_hook,gc_latched,gc_returned;
static void (*gc_play_original)(void *);
static void (*gc_press_original)(void *,const struct gc_op *);
static void (*gc_release_original)(void *,const struct gc_op *);
#ifndef GC_TEST_HELPERS
static int gc_resolve(void *self,uintptr_t *facade,uintptr_t *core) {
    if(!self||*(uintptr_t *)self!=0x297da78ul)return -1;
    uint64_t alias=*(uint64_t *)((char *)self+0x38);
    if(alias<0xcd212148ce520500ull||alias>0xcd212148ce520503ull)return -1;
    if(!((unsigned (*)(void *,unsigned))0x16142a0)((char *)self+0x30,1))return -1;
    uintptr_t binding=*(uintptr_t *)((char *)self+0x30);
    if(!binding||*(uintptr_t *)binding!=0x22f07d0ul)return -1;
    uintptr_t f=*(uintptr_t *)(binding+0x118);
    if(!f||*(uintptr_t *)f!=0x22e9fc8ul)return -1;
    if(!((unsigned (*)(void *,unsigned))0x7b2448)((void *)(f+0x28),1))return -1;
    uintptr_t c=*(uintptr_t *)(f+0x28);
    if(!c||*(uintptr_t *)c!=0x24af1c8ul)return -1;
    uintptr_t flag=*(uintptr_t *)(c+0x28);
    if(!flag||!*(unsigned char *)(flag+8))return -1;
    uintptr_t play=c-0xd0;
    if(*(uintptr_t *)play!=0x24b4c18ul||((uintptr_t *)0x24b4c18)[4]!=0x1117c48ul||
       *(uintptr_t *)(play+0x60)!=*(uintptr_t *)(c+0x68))return -1;
    uintptr_t play_flag=*(uintptr_t *)(play+0x20);
    if(!play_flag||!*(unsigned char *)(play_flag+8))return -1;
    *facade=f;*core=c;return (int)(alias-0xcd212148ce520500ull);
}
static int gc_paused(uintptr_t core) {
    uintptr_t state=*(uintptr_t *)(core+0x58);if(!state)return 0;
    return __atomic_load_n((unsigned char *)(state+8),__ATOMIC_ACQUIRE)==0;
}
static int gc_cue_exists(void *self,unsigned kind) {
    void *cue=((void *(*)(void *,unsigned))0x1222460)((char *)self+0x70,kind);
    return cue&&((unsigned (*)(void *))0x117ad88)(cue);
}
static void gc_finish(uintptr_t core) {
    ((void (*)(void *,unsigned,unsigned))0x1117c48)((void *)(core-0xd0),1,0);
}
static void gc_return(uintptr_t core,unsigned pad) {
    if(pad<8&&((uintptr_t *)0x24af1c8)[8]==0x10f9000ul)
        ((void (*)(void *,unsigned))0x10f9000)((void *)core,pad+1);
}
static int gc_play_matches(void *task,unsigned d) {
    if(!task||*(uintptr_t *)task!=0x2982bf8ul)return 0;
    uintptr_t h=*(uintptr_t *)((char *)task+0x18);
    if(!h||*(uintptr_t *)h!=0x2982b88ul)return 0;
    if(!((unsigned (*)(void *,unsigned))0x16142a0)((void *)(h+0x30),1))return 0;
    uintptr_t binding=*(uintptr_t *)(h+0x30);
    if(!binding||*(uintptr_t *)binding!=0x22f07d0ul)return 0;
    for(unsigned pad=0;pad<8;pad++) {
        struct gc_hold *held=&gc_held[d][pad];if(!held->handler)continue;
        uintptr_t f=0,c=0;
        if(gc_resolve((void *)held->handler,&f,&c)!=(int)d||f!=held->facade||c!=held->core)continue;
        if(binding==*(uintptr_t *)(held->handler+0x30))return 1;
    }
    return 0;
}
#endif
static void gc_refresh_mask(unsigned d) {
    unsigned mask=0;
    for(unsigned pad=0;pad<8;pad++)if(gc_held[d][pad].handler)mask|=1u<<pad;
    if(mask)__atomic_fetch_or(&gc_held_mask,1u<<d,__ATOMIC_RELEASE);
    else __atomic_fetch_and(&gc_held_mask,~(1u<<d),__ATOMIC_RELEASE);
}
static void gc_press_locked(void *self,const struct gc_op *op) {
    if(!__atomic_load_n(&gc_enabled,__ATOMIC_ACQUIRE)||!op||op->mode||op->pad>=8) {
        __atomic_fetch_add(&gc_passed,1,__ATOMIC_RELAXED);gc_press_original(self,op);return;
    }
    uintptr_t facade=0,core=0;int deck=gc_resolve(self,&facade,&core);
    if(deck<0||deck>3) {__atomic_fetch_add(&gc_rejected,1,__ATOMIC_RELAXED);gc_press_original(self,op);return;}
    struct gc_hold *hold=&gc_held[deck][op->pad];
    if(hold->handler) {
        if(hold->handler==(uintptr_t)self&&hold->facade==facade&&hold->core==core)return;
        __atomic_fetch_add(&gc_rejected,1,__ATOMIC_RELAXED);gc_press_original(self,op);return;
    }
    unsigned active=__atomic_load_n(&gc_held_mask,__ATOMIC_ACQUIRE)&(1u<<deck);
    if((!active&&!gc_paused(core))||!gc_cue_exists(self,op->pad+1)) {
        __atomic_fetch_add(&gc_passed,1,__ATOMIC_RELAXED);gc_press_original(self,op);return;
    }
    if(active) {
        int same=0;
        for(unsigned pad=0;pad<8;pad++)if(gc_held[deck][pad].handler&&
            gc_held[deck][pad].facade==facade&&gc_held[deck][pad].core==core)same=1;
        if(!same) {__atomic_fetch_add(&gc_rejected,1,__ATOMIC_RELAXED);gc_press_original(self,op);return;}
    } else gc_deck_latched[deck]=0;
    /* If an overlapping key remains physically down after the previous
     * audition ended, a fresh cue press starts a new audition epoch. */
    if(active&&gc_ended[deck]) {gc_ended[deck]=0;gc_deck_latched[deck]=0;}
    *hold=(struct gc_hold){(uintptr_t)self,facade,core,op->pad,0};
    gc_last_pad[deck]=op->pad;gc_refresh_mask((unsigned)deck);
    gc_press_original(self,op);__atomic_fetch_add(&gc_started,1,__ATOMIC_RELAXED);
}
static void gc_release_locked(void *self,const struct gc_op *op) {
    /* Finish owned presses even if the menu toggle was switched off mid-hold. */
    if(op&&op->pad<8)for(unsigned d=0;d<4;d++) {
        struct gc_hold held=gc_held[d][op->pad];
        if(held.handler!=(uintptr_t)self||held.pad!=op->pad)continue;
        uintptr_t facade=0,core=0;int deck=gc_resolve(self,&facade,&core);
        memset(&gc_held[d][op->pad],0,sizeof gc_held[d][op->pad]);gc_refresh_mask(d);
        if(deck==(int)d&&facade==held.facade&&core==held.core) {
            struct gc_op release=*op;release.mode=0;
            gc_release_original(self,&release);
            if(!gc_deck_latched[d]&&!gc_ended[d]) {
                gc_finish(core);gc_return(core,gc_last_pad[d]);gc_ended[d]=1;
                __atomic_fetch_add(&gc_returned,1,__ATOMIC_RELAXED);
            }
            __atomic_fetch_add(&gc_finished,1,__ATOMIC_RELAXED);
            if(!(__atomic_load_n(&gc_held_mask,__ATOMIC_ACQUIRE)&(1u<<d))) {
                gc_deck_latched[d]=0;gc_ended[d]=0;
            }
            return;
        }
        __atomic_fetch_add(&gc_rejected,1,__ATOMIC_RELAXED);break;
    }
    gc_release_original(self,op);
}
static void gc_press(void *self,const struct gc_op *op) {
    pthread_mutex_lock(&gc_lock);gc_press_locked(self,op);pthread_mutex_unlock(&gc_lock);
}
static void gc_release(void *self,const struct gc_op *op) {
    pthread_mutex_lock(&gc_lock);gc_release_locked(self,op);pthread_mutex_unlock(&gc_lock);
}
static void gc_play_task(void *task) {
    int claimed=0;pthread_mutex_lock(&gc_lock);
    for(unsigned d=0;d<4;d++)if((gc_held_mask&(1u<<d))&&!gc_deck_latched[d]&&gc_play_matches(task,d)) {
        gc_deck_latched[d]=1;
        for(unsigned pad=0;pad<8;pad++)if(gc_held[d][pad].handler)gc_held[d][pad].latched=1;
        __atomic_fetch_add(&gc_latched,1,__ATOMIC_RELAXED);claimed=1;break;
    }
    pthread_mutex_unlock(&gc_lock);
    if(!claimed)gc_play_original(task);
}
static unsigned gc_install(void) {
    const uintptr_t *vt=(const uintptr_t *)0x297da78;
    if(vt[3]!=0x1c77940ul||vt[4]!=0x1c77b30ul||((uintptr_t *)0x22e9fc8)[12]!=0x7ab008ul||
       ((uintptr_t *)0x24b4c18)[4]!=0x1117c48ul||((uintptr_t *)0x24af1c8)[8]!=0x10f9000ul||
       ((uintptr_t *)0x24af578)[2]!=0x10f7ce0ul||((uintptr_t *)0x2982bf8)[2]!=0x1c9b268ul)return 0;
    if(patch_slot(0x297da78+4*8,(void *)gc_release,(void **)&gc_release_original)==1)gc_hooks|=1;
    if(gc_hooks&&patch_slot(0x297da78+3*8,(void *)gc_press,(void **)&gc_press_original)==1)gc_hooks|=2;
    if(gc_hooks==3)gc_play_hook=patch_slot(0x2982bf8+2*8,(void *)gc_play_task,(void **)&gc_play_original)==1;
    return gc_hooks;
}
static void gc_pump(int enabled) {
    __atomic_store_n(&gc_enabled,enabled&&gc_hooks==3&&gc_play_hook,__ATOMIC_RELEASE);
}
static void gc_report(FILE *out) {
    fprintf(out,"gated_hot_cues hooks=%u enabled=%u mode=PAUSED_ONLY_MULTI_CUE_STOP_ON_ANY_RELEASE_PLAY_LATCH held_mask=%u started=%u finished=%u stock_pass=%u refused=%u audio_hardware_tested=0\n",gc_hooks,
        __atomic_load_n(&gc_enabled,__ATOMIC_ACQUIRE),__atomic_load_n(&gc_held_mask,__ATOMIC_ACQUIRE),
        __atomic_load_n(&gc_started,__ATOMIC_ACQUIRE),__atomic_load_n(&gc_finished,__ATOMIC_ACQUIRE),
        __atomic_load_n(&gc_passed,__ATOMIC_ACQUIRE),__atomic_load_n(&gc_rejected,__ATOMIC_ACQUIRE));
    fprintf(out,"gated_hot_cue_latch hook=%u committed=%u scope=SAME_NATIVE_BINDING release_pause_cancelled=1\n",gc_play_hook,__atomic_load_n(&gc_latched,__ATOMIC_ACQUIRE));
    fprintf(out,"gated_hot_cue_return submitted=%u native_backCue=10f9000 cue_kind=most_recently_pressed_owned_pad\n",__atomic_load_n(&gc_returned,__ATOMIC_ACQUIRE));
}

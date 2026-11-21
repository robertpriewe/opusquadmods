/* FX Frequency 2.11 packet engine: chains the native Beat FX sub-frame serializer 0x1d0e8c8
 * (builder vt 0x299db28, BeatFx object at builder+0x20 with vt 0x2995bb8 at +0x520) and applies
 * fx_touch_policy.h after the original has filled the 9 bytes. The original builder, its CRC and
 * the host's TIME gate are untouched; ALL / disable / stale mixer data = stock packet. */
#ifndef OQ_FREQUENCY_ENGINE_H
#define OQ_FREQUENCY_ENGINE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "fx_touch_policy.h"
#ifndef OQ_FX_WORD
#define OQ_FX_WORD(address) (*(const uint32_t *)(uintptr_t)(address))
#endif
struct oq_fx_engine {
    const struct oqmod_api *api;
    unsigned hooks,enabled,selection,strength,tx,bad_object,no_mixer;
    unsigned results[FT_RESULTS];
    /* snapshot for the status/log (written by the transport thread) */
    unsigned last_result,forcing,step,rx_on,rx_beat,native,sent,starts,press_stops,presses,beat_presses;
    uint64_t last_mixer_ms,prev_frame_ms,gap_sum;
    unsigned on_was,beat_was,max_gap,gaps;
    unsigned char prev_raw[128];unsigned have_raw;
    unsigned short changes[128];        /* new frames in which raw[i] changed */
    struct ft_state state;
    void (*serialize_original)(void *,unsigned char *);
};
static struct oq_fx_engine fx_engine;
/* last 24 events for the status block (transport thread writes, tick reads; display only):
 * F forcing at step, P beat release, R released by BEAT FX ON, S stopped (other), B BEAT button */
struct fx_ev {uint64_t ms;char kind;unsigned a,b;};
static struct fx_ev fx_events[24];static unsigned fx_event_n;
static void fx_event(uint64_t ms,char kind,unsigned a,unsigned b){
    unsigned i=__atomic_fetch_add(&fx_event_n,1,__ATOMIC_ACQ_REL)%24;
    fx_events[i].ms=ms;fx_events[i].kind=kind;fx_events[i].a=a;fx_events[i].b=b;
}
/* raw frame changes for research: offsets that changed at most FX_QUIET times get logged here */
#define FX_QUIET 60u
struct fx_diff {uint64_t ms;unsigned char off,old,val,forcing;};
static struct fx_diff fx_diffs[28];static unsigned fx_diff_n;
static void fx_raw_diff(uint64_t now,const unsigned char *raw){
    if(fx_engine.have_raw){
        for(unsigned i=0;i<128;i++)if(raw[i]!=fx_engine.prev_raw[i]){
            if(fx_engine.changes[i]<0xffff)fx_engine.changes[i]++;
            if(fx_engine.changes[i]<=FX_QUIET){
                unsigned k=__atomic_fetch_add(&fx_diff_n,1,__ATOMIC_ACQ_REL)%28;
                fx_diffs[k].ms=now;fx_diffs[k].off=(unsigned char)i;fx_diffs[k].old=fx_engine.prev_raw[i];
                fx_diffs[k].val=raw[i];fx_diffs[k].forcing=(unsigned char)fx_engine.state.forcing;
            }
        }
    }
    memcpy(fx_engine.prev_raw,raw,128);fx_engine.have_raw=1;
}
/* switch bytes 0x0a 0x0b 0x0c 0x0e as one word; BEAT FX ON (0x0e bit 0x40) is handled apart */
/* BEAT < = switch byte 0x0e bit 0x20, BEAT > = bit 0x10 (hardware 2.10) */
static unsigned fx_switches(const unsigned char *raw){return raw[0x0e]&0x30u;}
static void fx_serialize(void *builder,unsigned char *sub){
    fx_engine.serialize_original(builder,sub);
    if(!builder||!sub||*(uintptr_t *)builder!=0x299db28ul)return;
    uintptr_t mixer=*(uintptr_t *)((char *)builder+0x20);
    if(!mixer||*(uintptr_t *)(mixer+0x520)!=0x2995bb8ul){__atomic_fetch_add(&fx_engine.bad_object,1,__ATOMIC_RELAXED);return;}
    const struct oqmod_api *a=fx_engine.api;
    struct ft_input in;memset(&in,0,sizeof in);
    in.enabled=fx_engine.hooks==1&&__atomic_load_n(&fx_engine.enabled,__ATOMIC_ACQUIRE);
    in.selection=__atomic_load_n(&fx_engine.selection,__ATOMIC_ACQUIRE);
    in.strength=__atomic_load_n(&fx_engine.strength,__ATOMIC_ACQUIRE);
    uint64_t now=a->now_ms();
    in.now=now;
    if(in.enabled&&(in.selection==FT_LOW||in.selection==FT_HIGH)){
        struct oqmod_mixer m;memset(&m,0,sizeof m);m.size=sizeof m;
        if(a->mixer&&a->mixer(&m)&&m.valid){
            in.alive=1;   /* 2.10: frames come only on change; a valid link is alive */
            in.rx_on=(m.raw[0x58]&0x20)!=0;
            in.rx_beat=m.raw[0x5a];
            in.frame_ms=m.updated_ms;
            unsigned on_down=(m.buttons&0x40000000u)!=0,beat_bits=fx_switches(m.raw);
            if(m.updated_ms!=fx_engine.last_mixer_ms){       /* a new mixer frame: button edges */
                in.new_frame=1;
                fx_raw_diff(now,m.raw);
                in.on_edge=on_down&&!fx_engine.on_was;
                unsigned new_bits=beat_bits&~fx_engine.beat_was;
                in.beat_dir=(new_bits&0x20u)?-1:(new_bits&0x10u)?1:0;
                if(in.on_edge)__atomic_fetch_add(&fx_engine.presses,1,__ATOMIC_RELAXED);
                if(in.beat_dir)__atomic_fetch_add(&fx_engine.beat_presses,1,__ATOMIC_RELAXED);
                fx_engine.on_was=on_down;fx_engine.beat_was=beat_bits;fx_engine.last_mixer_ms=m.updated_ms;
                if(fx_engine.prev_frame_ms&&m.updated_ms>fx_engine.prev_frame_ms){
                    unsigned gap=(unsigned)(m.updated_ms-fx_engine.prev_frame_ms);
                    if(gap>fx_engine.max_gap)fx_engine.max_gap=gap;
                    fx_engine.gap_sum+=gap;fx_engine.gaps++;
                }
                fx_engine.prev_frame_ms=m.updated_ms;
                if(in.beat_dir)fx_event(now,'B',new_bits,in.rx_beat);
            }
            __atomic_store_n(&fx_engine.rx_on,in.rx_on,__ATOMIC_RELAXED);
            __atomic_store_n(&fx_engine.rx_beat,in.rx_beat,__ATOMIC_RELAXED);
        } else __atomic_fetch_add(&fx_engine.no_mixer,1,__ATOMIC_RELAXED);
    }
    unsigned native=sub[1]|((unsigned)sub[2]<<8)|((unsigned)sub[3]<<16);
    unsigned was_forcing=fx_engine.state.forcing,was_step=fx_engine.state.step;
    unsigned r=ft_apply(&fx_engine.state,&in,sub);
    if(fx_engine.state.forcing!=was_forcing||(fx_engine.state.forcing&&fx_engine.state.step!=was_step))
        fx_event(now,fx_engine.state.forcing?'F':(r==FT_PASS_BEAT_CHANGE?'P':r==FT_RELEASED?'R':'S'),r,fx_engine.state.forcing?fx_engine.state.step:in.rx_beat);
    if(r<FT_RESULTS)__atomic_fetch_add(&fx_engine.results[r],1,__ATOMIC_RELAXED);
    __atomic_store_n(&fx_engine.last_result,r,__ATOMIC_RELAXED);
    __atomic_store_n(&fx_engine.forcing,fx_engine.state.forcing,__ATOMIC_RELAXED);
    __atomic_store_n(&fx_engine.step,fx_engine.state.step,__ATOMIC_RELAXED);
    __atomic_store_n(&fx_engine.starts,fx_engine.state.starts,__ATOMIC_RELAXED);
    __atomic_store_n(&fx_engine.press_stops,fx_engine.state.press_stops,__ATOMIC_RELAXED);
    __atomic_store_n(&fx_engine.native,native,__ATOMIC_RELAXED);
    __atomic_store_n(&fx_engine.sent,sub[1]|((unsigned)sub[2]<<8)|((unsigned)sub[3]<<16),__ATOMIC_RELEASE);
    __atomic_fetch_add(&fx_engine.tx,1,__ATOMIC_RELAXED);
}
static unsigned fx_install(const struct oqmod_api *a){
    fx_engine.api=a;
    if(!a->ep136_ok||!a->hook||!a->now_ms||!a->ep136_ok()||
       a->size<offsetof(struct oqmod_api,mixer)+sizeof(void *)||!a->mixer||
       OQ_FX_WORD(0x1d0e8c8)!=0xf9401000u||OQ_FX_WORD(0x1d0f33c)!=0x97fffd63u||
       OQ_FX_WORD(0x1d087fc)!=0xaa1303e2u||OQ_FX_WORD(0x1d0880c)!=0x38402443u)return 0;
    if(a->hook(0x1d0e8c8,0xf9401000,(void *)fx_serialize,(void **)&fx_engine.serialize_original)==1)fx_engine.hooks=1;
    return fx_engine.hooks;
}
static void fx_engine_set(int enabled,unsigned selection,unsigned strength){
    __atomic_store_n(&fx_engine.selection,selection,__ATOMIC_RELEASE);
    __atomic_store_n(&fx_engine.strength,strength,__ATOMIC_RELEASE);
    __atomic_store_n(&fx_engine.enabled,!!enabled,__ATOMIC_RELEASE);
}
#endif

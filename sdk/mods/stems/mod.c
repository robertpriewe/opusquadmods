#define _GNU_SOURCE
/* GPL-3.0-only azstem engine and project UI in a resident USB add-on. */
#include "oqmod_native_feature.h"
#include "math_compat.h"
#include <pthread.h>
#include <string.h>
static const struct oq_native_option oq_feature_options[]={
};
static const struct oq_native_feature oq_feature={"stems","Stems","Per-deck VOCAL, INST, BASS and DRUMS controls.\nNeeds compatible pre-generated stem files on the USB.\nUSB-owned decoder, audio processing and touch controls.", 140};
#define OQ_NATIVE_CUSTOM_ENTRY
#define OQ_NATIVE_FEATURE_IMPLEMENTATION
#include "oqmod_native_feature.h"
static struct oqmod_stems_audio stems_audio_io;
static int stem_engine_published;
/* Upstream intentionally bounds these display/path copies with snprintf.
 * New cross compilers diagnose their truncation; keep the warning local. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#include "native/azstem/azstem.c"
#pragma GCC diagnostic pop
#include "control.h"
static unsigned stem_open,stem_toggle,stem_avail,stem_bits,stem_cards;
static struct oqmod_stems_context stem_context;
static void stem_panels(void){
    struct oqmod_panel p[5]={{800,8,100,40,OQMOD_PANEL_TOUCH,0,0}};unsigned count=1;
    unsigned open=__atomic_load_n(&stem_open,__ATOMIC_ACQUIRE);
    if(open){
        static const unsigned row[2][4]={{1,2,0,3},{0,1,2,3}};
        int cards=stem_context.fourlane&&stem_context.layout==0&&stem_context.pitch<157;
        __atomic_store_n(&stem_cards,!!cards,__ATOMIC_RELEASE);
        for(unsigned d=0;d<4;d++){
            unsigned r=row[stem_context.order==1][d];
            if(cards){int h=((int)stem_context.pitch-46)/3-4;if(h<14)h=14;p[count++]=(struct oqmod_panel){5,70+(int)stem_context.pitch*(int)r+46,140,h,OQMOD_PANEL_TOUCH,0,0};}
            else{int four=stem_context.fourlane&&stem_context.layout==0;p[count++]=(struct oqmod_panel){156+(int)r*231,four?699:690,231,four?44:52,OQMOD_PANEL_TOUCH,0,0};}
        }
    }
    oq_feature_api->panels_set(p,count);
}
static void stem_tick(void){
    oq_feature_tick();if(!oq_feature_on)return;
    unsigned toggles=__atomic_exchange_n(&stem_toggle,0,__ATOMIC_ACQ_REL);
    static uint64_t checked;uint64_t now=oq_feature_api->now_ms();
    if(stem_engine_published&&(toggles||now-checked>=100)){
        checked=now;oqs_poll(1,toggles);oqs_engine_flag(1);
        unsigned avail=16,bits=0;for(unsigned d=0;d<4;d++){avail|=(unsigned)oqs_avail[d]<<d;bits|=oqs_on[d]<<(4*d);}
        __atomic_store_n(&stem_avail,avail,__ATOMIC_RELEASE);__atomic_store_n(&stem_bits,bits,__ATOMIC_RELEASE);
    }
    stem_context.size=sizeof stem_context;
    if(oq_feature_api->stems_ui(&stem_context,__atomic_load_n(&stem_avail,__ATOMIC_ACQUIRE),__atomic_load_n(&stem_bits,__ATOMIC_ACQUIRE),__atomic_load_n(&stem_open,__ATOMIC_ACQUIRE)))stem_panels();
}
static void stem_enabled(int on){
    oq_feature_enabled(on);
    if(!on){
        atomic_store_explicit(&g_enable,0,memory_order_release);
        __atomic_store_n(&stem_open,0,__ATOMIC_RELEASE);__atomic_store_n(&stem_toggle,0,__ATOMIC_RELEASE);
        if(stem_engine_published){oqs_poll(0,0);oqs_engine_flag(0);}
        oq_feature_api->panels_set(NULL,0);
    }
}
static void stem_draw(struct oqmod_canvas *c,unsigned slot,int w,int h){
    struct oqmod_surface s={0};oq_feature_api->surface(OQMOD_SURFACE_FX_FREQUENCY,&s);
    unsigned open=__atomic_load_n(&stem_open,__ATOMIC_ACQUIRE);
    if(!slot){
        oq_feature_api->fill(c,0,0,w,h,open?s.selected:s.background);oq_feature_api->outline(c,0,0,w,h,2,s.text);
        oq_feature_api->text(c,0,0,w,h,"STEMS",16,s.text);return;
    }
    if(slot>4)return;
    unsigned d=slot-1,avail=__atomic_load_n(&stem_avail,__ATOMIC_ACQUIRE),on=__atomic_load_n(&stem_bits,__ATOMIC_ACQUIRE);
    int cards=w==140,label=cards?0:31,bw=cards?35:50;
    oq_feature_api->fill(c,0,0,w,h,s.background);
    if(label){char deck[4];snprintf(deck,sizeof deck,"%u",d+1);oq_feature_api->text(c,0,0,label,h,deck,20,s.text);}
    if(!(avail>>d&1)){oq_feature_api->text(c,label,0,w-label,h,"NO STEMS",12,s.dim);return;}
    static const uint32_t colors[]={0x00e040,0xf03030,0xe000c0,0x2060ff};
    static const char *const names[]={"VOCAL","INST","BASS","DRUMS"},*const shortnames[]={"V","I","B","D"};
    for(unsigned k=0;k<4;k++){
        int x=label+(int)k*bw,lit=on>>(4*d+k)&1;uint32_t color=colors[k];
        oq_feature_api->fill(c,x+1,2,bw-3,h-4,lit?(color>>1)&0x7f7f7f:s.background);
        oq_feature_api->outline(c,x+1,2,bw-3,h-4,1,lit?color:s.divider);
        oq_feature_api->text(c,x,0,bw,h,cards?shortnames[k]:names[k],cards?14:11,lit?s.text:s.dim);
    }
}
static int stem_touch(unsigned slot,int x,int y){
    (void)y;if(!oq_feature_on)return 0;
    if(!slot){__atomic_fetch_xor(&stem_open,1,__ATOMIC_ACQ_REL);oq_feature_api->redraw();return 1;}
    if(slot>4)return 0;
    unsigned d=slot-1;
    int cards=__atomic_load_n(&stem_cards,__ATOMIC_ACQUIRE);
    int local=x-(cards?0:31),stem=local/(cards?35:50);
    if(local<0||stem<0||stem>3||!(__atomic_load_n(&stem_avail,__ATOMIC_ACQUIRE)>>d&1))return 0;
    __atomic_fetch_or(&stem_toggle,1u<<(d*4+(unsigned)stem),__ATOMIC_ACQ_REL);return 1;
}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->size<sizeof *a||!a->stems_audio||!a->stems_ui||!a->panels_set||!a->panel_renderer||!a->surface||!a->fill||!a->outline||!a->text||!a->now_ms||!a->native_engine)return 1;
    if(oq_feature_init(a,info)||!a->native_get("engine_external",0)||!a->native_engine())return 1;
    stems_audio_io.size=sizeof stems_audio_io;
    if(!a->stems_audio(&stems_audio_io)||!a->panel_renderer(stem_draw,stem_touch))return 1;
    if(stems_audio_io.ready()){
        oqs_engine_flag(0);atomic_store_explicit(&g_enable,0,memory_order_release);
        azstem_init();atomic_store_explicit(&g_enable,0,memory_order_release);stem_engine_published=stems_audio_io.publish();
    }
    if(!stem_engine_published)a->log("stems native audio guard unavailable; engine remains pass-through");
    info->enabled=stem_enabled;info->tick=stem_tick;info->version="2.0";return 0;
}

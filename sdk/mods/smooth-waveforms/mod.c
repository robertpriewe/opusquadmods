/* USB-owned timer and paint-clock policy; shared host hooks only measure and
 * dispatch callbacks, allowing FPS/diagnostics without any smoothing add-on. */
#include "oqmod_native_feature.h"
static const char *const values0[]={"Stock", "Smooth 30", "Experimental 60", "Native Height 30"};
static const struct oq_native_option oq_feature_options[]={
    {"mode","Mode",values0,4,3,0},
};
static const struct oq_native_feature oq_feature={"smooth-waveforms","Smooth Waveforms","Native Height 30 is the recommended four-deck mode.\nOther modes are available for comparison. No audio changes.\nDisabling restores Stock timing.", 60};
#define OQ_NATIVE_CUSTOM_ENTRY
#define OQ_NATIVE_FEATURE_IMPLEMENTATION
#include "oqmod_native_feature.h"
#include "policy.h"
static void paint_begin(struct oqmod_paint_context *c,uint64_t now){
    uintptr_t retained=c->instance;
    c->instance=0;c->start_ns=now;c->before=0;
    if(retained&&*(uintptr_t *)retained==RT_MAIN_VT)__atomic_store_n(&rt_main,retained,__ATOMIC_RELEASE);
    uintptr_t timer=__atomic_load_n(&rt_main,__ATOMIC_ACQUIRE);
    if(__atomic_load_n(&rt_mode,__ATOMIC_ACQUIRE)!=RT_MODE_STOCK&&timer&&*(uintptr_t *)timer==RT_MAIN_VT){
        c->instance=timer;c->before=*(double *)(timer-8+0x80);
    }
}
static void paint_end(struct oqmod_paint_context *c){
    uintptr_t timer=c->instance;
    if(timer&&*(uintptr_t *)timer==RT_MAIN_VT){
        char *base=(char *)timer-8;
        (void)rt_rebase_paint_clock(1,(double)c->start_ns/1e6,c->before,
            *(double *)(base+0x80),*(double *)(base+0x70),(double *)(base+0x80));
    }
}
static void smooth_enabled(int on){
    if(!on)__atomic_store_n(&rt_mode,RT_MODE_STOCK,__ATOMIC_RELEASE);
    oq_feature_enabled(on);
}
static void smooth_tick(void){
    oq_feature_tick();int variant=oq_feature_api->setting_get("mode",3);
    int mode=variant==2?RT_MODE_FAST_60:variant>=1&&variant<=3?RT_MODE_SMOOTH_30:RT_MODE_STOCK;
    __atomic_store_n(&rt_mode,oq_feature_on?mode:RT_MODE_STOCK,__ATOMIC_RELEASE);
}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->size<sizeof *a||!a->render_provider||!a->native_engine||!a->native_get||!a->setting_get)return 1;
    if(oq_feature_init(a,info)||!a->native_get("engine_external",0)||!a->native_engine())return 1;
    const struct oqmod_render_provider provider={sizeof provider,rt_period,paint_begin,paint_end};
    if(!a->render_provider(&provider))return 1;
    info->tick=smooth_tick;info->enabled=smooth_enabled;info->version="2.0";return 0;
}

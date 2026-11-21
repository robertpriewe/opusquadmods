/* FX Frequency 2.11: LOW / HIGH emulate a finger held near the bottom (LOW) or top (HIGH) of the
 * Beat FX XY pad, at the X step that keeps the beat the mixer reports. See fx_touch_policy.h. */
#include "oqmod_native_feature.h"
#include <string.h>
#include <stddef.h>
#include "frequency.h"
static const char *const values0[]={"LOW", "HIGH", "ALL"};
static const struct oq_native_option oq_feature_options[]={
    {"selection","FX range",values0,3,2,1},
};
static const struct oq_native_feature oq_feature={"fx-frequency","FX Frequency","LOW or HIGH applies the Beat FX like holding the bottom\nor top of the X-PAD, at the selected beat. ALL is normal.\nBEAT FX ON switches it on and off as usual.\nEnable Beat FX Sidebar to show the on-screen controls.", 70};
#define OQ_NATIVE_CUSTOM_ENTRY
#define OQ_NATIVE_FEATURE_IMPLEMENTATION
#include "oqmod_native_feature.h"
#include "engine.h"
static unsigned requested,shown_selection=3;
static uint64_t report_at;
static unsigned report_tx;
static unsigned render_calls,touch_calls,panel_visible;
static unsigned counter(const unsigned *value){return __atomic_load_n(value,__ATOMIC_ACQUIRE);}
static void sync_panel(void){
    struct oqmod_surface s;memset(&s,0,sizeof s);
    unsigned show=oq_feature_on&&oq_feature_api->surface(OQMOD_SURFACE_FX_FREQUENCY,&s)&&s.w==180&&s.h==66;
    struct oqmod_panel p={s.x,s.y,s.w,s.h,OQMOD_PANEL_TOUCH,0,0};
    oq_feature_api->panels_set(show?&p:NULL,show?1:0);
    __atomic_store_n(&panel_visible,show,__ATOMIC_RELEASE);
}
static unsigned read_strength(void){int v=oq_feature_api->setting_get("strength",1);return v<0||v>2?1u:(unsigned)v;}
static const char *const result_names[FT_RESULTS]={"disabled","all","stale","user_touch","off","blocked","beat_unknown","released","forced","beat_change"};
static void tick(void){
    unsigned request=__atomic_exchange_n(&requested,0,__ATOMIC_ACQ_REL);
    if(request)oq_feature_api->setting_set("selection",(int)request-1);
    oq_feature_tick();
    unsigned selected=(unsigned)oq_feature_api->setting_get("selection",2)+1;
    if(selected<1||selected>3)selected=3;
    unsigned strength=read_strength();
    fx_engine_set(oq_feature_on,selected,strength);
    sync_panel();
    if(selected!=__atomic_load_n(&shown_selection,__ATOMIC_ACQUIRE)){
        __atomic_store_n(&shown_selection,selected,__ATOMIC_RELEASE);oq_feature_api->redraw();
    }
    uint64_t now=oq_feature_api->now_ms();
    /* one line whenever the decision, the step, the mixer's ON / beat or the range changes (max 400) */
    {
        static unsigned last_key=~0u,lines;static uint64_t t0;if(!t0)t0=now;
        unsigned r=counter(&fx_engine.last_result),f=counter(&fx_engine.forcing),st=counter(&fx_engine.step);
        unsigned on=counter(&fx_engine.rx_on),beat=counter(&fx_engine.rx_beat),sent=counter(&fx_engine.sent),native=counter(&fx_engine.native);
        unsigned key=(r&15)|f<<4|(st&7)<<5|on<<8|(beat&255)<<9|selected<<17|strength<<19|((native>>4)&3)<<21;
        if(key!=last_key&&lines<400&&oq_feature_api->log){
            last_key=key;lines++;char line[220];
            snprintf(line,sizeof line,"fx28 t=%llu range=%s strength=%u %s forcing=%u step=%u mixer_on=%u mixer_beat=%u native=%06x sent=%06x presses=%u",
                (unsigned long long)(now-t0),selected==1?"LOW":selected==2?"HIGH":"ALL",strength,r<FT_RESULTS?result_names[r]:"?",f,st,on,beat,native,sent,counter(&fx_engine.presses));
            oq_feature_api->log(line);
        }
    }
    unsigned tx=counter(&fx_engine.tx);
    if(now-report_at>=1000&&tx!=report_tx){
        char status[1024];size_t cap=sizeof status;
        unsigned gaps=fx_engine.gaps;
        int used=snprintf(status,cap,"FX Frequency 2.11: hook %u TX %u range %u str %u on %u\n"
            "hold=%u step=%u mixer_on=%u beat=%u starts=%u stops=%u moves=%u presses=%u btn=%u gap avg/max=%u/%u\n"
            "forced=%u off=%u blocked=%u beat_change=%u\nchg:",
            fx_engine.hooks,tx,selected,strength,counter(&fx_engine.enabled),
            counter(&fx_engine.forcing),counter(&fx_engine.step),counter(&fx_engine.rx_on),counter(&fx_engine.rx_beat),
            counter(&fx_engine.starts),counter(&fx_engine.press_stops),fx_engine.state.moves,
            counter(&fx_engine.presses),counter(&fx_engine.beat_presses),
            gaps?(unsigned)(fx_engine.gap_sum/gaps):0u,fx_engine.max_gap,
            counter(&fx_engine.results[FT_FORCED]),counter(&fx_engine.results[FT_PASS_OFF]),counter(&fx_engine.results[FT_PASS_BLOCKED]),counter(&fx_engine.results[FT_PASS_BEAT_CHANGE]));
        /* which raw mixer bytes change, and how often (up to 24 offsets) */
        for(unsigned i=0,k=0;i<128&&k<24&&used>0&&(size_t)used<cap-16;i++)if(fx_engine.changes[i]){
            used+=snprintf(status+used,cap-(size_t)used," %02x:%u",i,fx_engine.changes[i]);k++;}
        /* last quiet byte changes: ms(mod 100 s, in 10 ms) off old>new, * = holding */
        if(used>0&&(size_t)used<cap-8)used+=snprintf(status+used,cap-(size_t)used,"\nd:");
        {unsigned n=__atomic_load_n(&fx_diff_n,__ATOMIC_ACQUIRE),first=n>28?n-28:0;
         for(unsigned i=first;i<n&&used>0&&(size_t)used<cap-24;i++){const struct fx_diff *d=&fx_diffs[i%28];
            used+=snprintf(status+used,cap-(size_t)used," %u/%02x:%02x>%02x%s",(unsigned)(d->ms/10%10000),d->off,d->old,d->val,d->forcing?"*":"");}}
        if(used>0&&(size_t)used<cap-8)used+=snprintf(status+used,cap-(size_t)used,"\nev:");
        {unsigned n=__atomic_load_n(&fx_event_n,__ATOMIC_ACQUIRE),first=n>24?n-24:0;
         for(unsigned i=first;i<n&&used>0&&(size_t)used<cap-24;i++){const struct fx_ev *e=&fx_events[i%24];
            used+=snprintf(status+used,cap-(size_t)used," %u%c%x/%u",(unsigned)(e->ms/10%10000),e->kind?e->kind:'?',e->a,e->b);}}
        oq_feature_api->set_status(status);report_tx=tx;report_at=now;
    }
}
static void enabled(int on){
    fx_engine_set(on,(unsigned)oq_feature_api->setting_get("selection",2)+1,read_strength());
    oq_feature_enabled(on);
    sync_panel();
    if(fx_engine.hooks!=1)oq_feature_api->set_status("Native hook guard refused; frequency packets unchanged.");
    oq_feature_api->redraw();
}
static void draw(struct oqmod_canvas *c,int w,int h){
    struct oqmod_surface s;if(!oq_feature_api->surface(OQMOD_SURFACE_FX_FREQUENCY,&s))return;
    __atomic_fetch_add(&render_calls,1,__ATOMIC_RELAXED);
    unsigned current=__atomic_load_n(&shown_selection,__ATOMIC_ACQUIRE);
    oq_feature_api->fill(c,0,0,w,h,s.background);oq_feature_api->fill(c,0,0,w,24,s.band);
    oq_feature_api->text(c,0,0,w,24,fx_frequency_caption(current),13,current==3?s.dim:s.text);
    static const char *const labels[]={"LOW","ALL","HIGH"};static const unsigned ranges[]={1,3,2};
    for(int i=0;i<3;i++){
        int x=i*61,on=current==ranges[i];
        oq_feature_api->fill(c,x,24,58,42,on?s.selected:s.background);
        oq_feature_api->outline(c,x,24,58,42,1,on?s.selected:s.divider);
        oq_feature_api->text(c,x,24,58,42,labels[i],14,on?s.text:s.dim);
    }
}
static int touch(int x,int y){
    if(!oq_feature_on||x<0||x>=180||y<24||y>=66||x%61>=58||x/61>=3)return 0;
    __atomic_fetch_add(&touch_calls,1,__ATOMIC_RELAXED);
    static const unsigned ranges[]={1,3,2};
    unsigned current=__atomic_load_n(&shown_selection,__ATOMIC_ACQUIRE);
    unsigned next=fx_frequency_select(current,ranges[x/61]);
    __atomic_store_n(&requested,next,__ATOMIC_RELEASE);return 1;
}
static void panel_draw(struct oqmod_canvas *c,unsigned slot,int w,int h){if(!slot)draw(c,w,h);}
static int panel_touch(unsigned slot,int x,int y){return !slot?touch(x,y):0;}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->size<sizeof *a||!a->surface||!a->use_surface||!a->set_status||
       !a->fill||!a->outline||!a->text||!a->redraw||!a->native_get||!a->native_engine||!a->now_ms||!a->panel_renderer||!a->panels_set)return 1;
    if(oq_feature_init(a,info))return 1;
    static const char *const strength_labels[]={"Light","Medium","Strong"};
    a->choice_buttons("strength","LOW / HIGH strength (how close to the pad edge)",strength_labels,3,1);
    if(!a->native_get("engine_external",0)||!a->native_engine()||!a->panel_renderer(panel_draw,panel_touch))return 1;
    struct oqmod_surface slot;memset(&slot,0,sizeof slot);a->surface(OQMOD_SURFACE_FX_FREQUENCY,&slot);
    if(slot.w!=180||slot.h!=66)return 1;
    (void)fx_install(a);
    info->x=info->y=info->w=info->h=0;
    info->enabled=enabled;info->tick=tick;info->draw=NULL;info->touch=NULL;info->version="3.0";
    return 0;
}

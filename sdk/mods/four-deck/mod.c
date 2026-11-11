/* USB-owned lane/card/mixer/overview policy. Native update/paint hooks stay shared. */
#include "oqmod_native_feature.h"
#include <string.h>
static const char *const values0[]={"4 DECK", "2/4 DECK", "2 DECK"};
static const char *const values1[]={"3 / 1 / 2 / 4", "1 / 2 / 3 / 4"};
static const char *const values2[]={"Hidden", "Visible"};
static const char *const timing_values[]={"Stock", "Smooth 30", "Experimental 60", "Native Height 30 (recommended)"};
static const struct oq_native_option oq_feature_options[]={
    {"layout","Layout",values0,3,0,0},
    {"order","Lane order",values1,2,0,0},
    {"overviews","Track overviews",values2,2,0,0},
    {"mode","Waveform smoothing",timing_values,4,3,0},
};
static const struct oq_native_feature oq_feature={"waveform-layout","4-Deck Waveform","2 DECK, 2/4 DECK and 4 DECK layouts with native waveforms.\nNative Height 30 is recommended for four decks.", 10};
#define OQ_NATIVE_CUSTOM_ENTRY
#define OQ_NATIVE_FEATURE_IMPLEMENTATION
#include "oqmod_native_feature.h"
#include "timing_policy.h"
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

static void combined_enabled(int on){
    if(!on)__atomic_store_n(&rt_mode,RT_MODE_STOCK,__ATOMIC_RELEASE);
    oq_feature_enabled(on);
    if(!on)oq_feature_api->native_set("mode",0);
}
static void combined_tick(void){
    oq_feature_tick();int variant=oq_feature_api->setting_get("mode",3);
    int mode=variant==2?RT_MODE_FAST_60:variant>=1&&variant<=3?RT_MODE_SMOOTH_30:RT_MODE_STOCK;
    __atomic_store_n(&rt_mode,oq_feature_on?mode:RT_MODE_STOCK,__ATOMIC_RELEASE);
}
static struct oqmod_layout_inputs layout_cfg;
static struct oqmod_components layout_components;
enum {LAYOUT_4DECK,LAYOUT_24DECK,LAYOUT_2DECK,LAYOUT_COUNT};
#define olog(...) ((void)0)
#define BEATFX_CLASS "N3gui2fx12BeatFxWidgetE"
#include "constants.h"
static int ptr_ok(const void *p){return p&&!((uintptr_t)p&7)&&(uintptr_t)p>0x10000;}
static const char *rtti_name(const void *p){return layout_components.class_name(p);}
static int is_class(const void *p,const char *name){const char *n=rtti_name(p);return n&&!strcmp(n,name);}
static char *as_component(void *p){return (char *)p+*(const long *)(*(uintptr_t *)p-24);}
static int comp_visible_now(const char *p){return (*(const unsigned *)(p+COMP_FLAGS)>>1)&1;}
static void comp_visible(char *p,int on){if(comp_visible_now(p)!=on)layout_components.visible(p,on);}
static void comp_bounds(char *p,int x,int y,int w,int h){const int *b=(const int *)(p+COMP_BOUNDS);if(b[0]!=x||b[1]!=y||b[2]!=w||b[3]!=h)layout_components.bounds(p,x,y,w,h);}
static void comp_size(char *p,int w,int h){const int *b=(const int *)(p+COMP_BOUNDS);if(b[2]!=w||b[3]!=h)layout_components.resize(p,w,h);}
struct wfv_parts {char *lane[4],*prev,*dif,*card[4];void *card_obj[4];};
static int wfv_parts(void *view,struct wfv_parts *out){
    struct oqmod_waveform_parts p;if(!oq_feature_api->waveform_parts(view,&p))return 0;
    _Static_assert(sizeof p==sizeof *out,"waveform parts ABI");memcpy(out,&p,sizeof p);return 1;
}
static struct oqmod_native_view_state g_fx_sidebar;
static int fx_sidebar_converge(char *,char *);
static void fx_sidebar_show(void);
#include "geometry.h"
static void fx_sidebar_restore(void){oq_feature_api->waveform_sidebar(0,NULL,&g_fx_sidebar);}
static int fx_sidebar_converge(char *owner,char *mixer){
    struct oqmod_native_view_context c={.owner=owner,.parent=mixer};
    memcpy(c.parent_box,g_mixer_hidden==mixer?g_mixer_box:(const int *)(mixer+COMP_BOUNDS),sizeof c.parent_box);
    return oq_feature_api->waveform_sidebar(1,&c,&g_fx_sidebar);
}
static void fx_sidebar_show(void){oq_feature_api->waveform_sidebar(2,NULL,&g_fx_sidebar);}
#include "overlay.h"
#include "lanes.h"
#include "policy.h"
static int layout_update(void *view,const struct oqmod_layout_inputs *input){layout_cfg=*input;return layout_converge(view);}
static int layout_cards(void *dif){
    if(!want_four()||dif!=g_dif_changed)return 0;
    for(unsigned i=0;i<4;i++){void *c=*(void **)((char *)dif+DIF_CARD0+8*i);if(!is_class(c,"N3gui8waveform14DeckInfoWidgetE"))continue;comp_visible(as_component(c),1);layout_components.refresh_card(c);}
    return 1;
}
static void layout_suspend(void){
    g_fast_view=NULL;if(g_dif_changed)dif_restore();if(g_mixer_hidden||g_fx_sidebar.root)mixer_restore();
}
static int layout_paint_begin(void *comp,void *graphics){
    int h=((const int *)((char *)comp+COMP_BOUNDS))[3];void ***ctx=*(void ****)graphics;
    if(h<=0||h>=DWW_STOCK_H||!ptr_ok(ctx))return 0;
    float t[6]={1,0,0,0,(float)h/DWW_STOCK_H,0};
    ((void (*)(void *))(*ctx)[14])(ctx);((void (*)(void *,const float *))(*ctx)[4])(ctx,t);return 1;
}
static void layout_paint_end(void *comp,void *graphics){(void)comp;void ***ctx=*(void ****)graphics;((void (*)(void *))(*ctx)[15])(ctx);}
static int layout_pitch(const struct oqmod_layout_inputs *input){layout_cfg=*input;return lane4_pitch();}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->size<sizeof *a||!a->layout_provider||!a->components||!a->waveform_parts||!a->waveform_sidebar||!a->native_get||!a->native_engine)return 1;
    /* This new per-layout preference has no inherited stock-timing fallback.
     * Existing explicit choices win; missing mode starts at Native Height 30. */
    if(!a->setting_get||!a->setting_set)return 1;
    a->setting_set("mode",a->setting_get("mode",3));
    if(oq_feature_init(a,info)||!a->native_get("engine_external",0)||!a->native_engine())return 1;
    layout_components.size=sizeof layout_components;
    if(!a->components(&layout_components)||!layout_components.resize||!layout_components.repaint||!layout_components.refresh_card||!layout_components.stock_cards)return 1;
    struct oqmod_layout_provider p={sizeof p,layout_update,layout_cards,layout_suspend,layout_paint_begin,layout_paint_end,layout_pitch};
    if(!a->layout_provider(&p))return 1;
    const struct oqmod_render_provider timing={sizeof timing,rt_period,paint_begin,paint_end};
    if(!a->render_provider||!a->render_provider(&timing))return 1;
    info->enabled=combined_enabled;info->tick=combined_tick;
    info->version="2.2";return 0;
}

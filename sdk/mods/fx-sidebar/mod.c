/* USB-owned native Beat FX sidebar geometry, restoration and frame decoration. */
#include "oqmod_native_feature.h"
#include <string.h>
static const struct oq_native_option oq_feature_options[]={
};
static const struct oq_native_feature oq_feature={"fx-sidebar","Beat FX Sidebar","Native effect name, BPM, beat and time at the right.\nRequired for FX Frequency and Gate Cues quick controls.\nYields to native popup screens in every waveform layout.", 80};
#define OQ_NATIVE_CUSTOM_ENTRY
#define OQ_NATIVE_FEATURE_IMPLEMENTATION
#include "oqmod_native_feature.h"
#define COMP_PARENT 0x18
#define COMP_BOUNDS 0x20
#define COMP_FLAGS 0xc0
#define BEATFX_CLASS "N3gui2fx12BeatFxWidgetE"
static struct oqmod_components sb_components;
static int sb_parent_box[4];
static int ptr_ok(const void *p){return p&&!((uintptr_t)p&7)&&(uintptr_t)p>0x10000;}
static int is_class(const void *p,const char *name){const char *n=sb_components.class_name(p);return n&&!strcmp(n,name);}
static const char *rtti_name(const void *p){return sb_components.class_name(p);}
static int comp_visible_now(const char *c){return (*(const unsigned *)(c+COMP_FLAGS)>>1)&1;}
static void comp_visible(char *p,int on){if(comp_visible_now(p)!=on)sb_components.visible(p,on);}
static void comp_bounds(char *p,int x,int y,int w,int h){const int *b=(const int *)(p+COMP_BOUNDS);if(b[0]!=x||b[1]!=y||b[2]!=w||b[3]!=h)sb_components.bounds(p,x,y,w,h);}
static char *child_of_class(char *c,const char *name,size_t prefix){
    char **kids=*(char ***)(c+0x40);int n=*(int *)(c+0x50);
    if(!ptr_ok(kids)||n<0||n>64)return NULL;
    for(int i=0;i<n;i++){const char *k=rtti_name(kids[i]);if(k&&(prefix?!strncmp(k,name,prefix):!strcmp(k,name)))return kids[i];}
    return NULL;
}
#include "engine.h"
static void sidebar_state(struct oqmod_native_view_state *out){
    *out=(struct oqmod_native_view_state){.root=g_fx_sidebar.root,.owner=g_fx_sidebar.owner,.parent=g_fx_sidebar.parent,
        .time=g_fx_sidebar.time,.percent=g_fx_sidebar.percent,.chooser=g_fx_sidebar.chooser};
    memcpy(out->chooser_screen,g_fx_sidebar.chooser_screen,sizeof out->chooser_screen);
}
static void sidebar_panels(int show,int time){
    if(!show){oq_feature_api->panels_set(NULL,0);return;}
    int h=time?202:152;
    struct oqmod_panel panels[6]={
        {1090,88,180,28,0,0,0},{1090,116,1,h-28,0,0,0},{1269,116,1,h-28,0,0,0},
        {1090,88+h-1,180,1,0,0,0},{1091,239,178,1,0,0,0},{1096,248,44,24,0,0,0}};
    oq_feature_api->panels_set(panels,time?6:4);
}
static int sidebar_converge(const struct oqmod_native_view_context *ctx,struct oqmod_native_view_state *out){
    if(!oq_feature_on||!is_class(ctx->owner,"N3gui8waveform12WaveformViewE")||!is_class(ctx->parent,"N3gui5mixer15MixerAreaWidgetE"))return 0;
    memcpy(sb_parent_box,ctx->parent_box,sizeof sb_parent_box);
    int ok=fx_sidebar_converge(ctx->owner,ctx->parent);sidebar_state(out);return ok;
}
static int sidebar_visible(int show,struct oqmod_native_view_state *out){
    if(!is_class(g_fx_sidebar.root,BEATFX_CLASS)||!is_class(g_fx_sidebar.time,"N3gui2fx14TimeInfoWidgetE")){
        memset(out,0,sizeof *out);sidebar_panels(0,0);return 0;
    }
    int time=comp_visible_now(g_fx_sidebar.time);comp_visible(g_fx_sidebar.root,show);sidebar_state(out);sidebar_panels(show,time);
    return show?1|(time?2:0):0;
}
static void sidebar_restore(void){fx_sidebar_restore();sidebar_panels(0,0);}
static void sidebar_draw(struct oqmod_canvas *c,unsigned slot,int w,int h){
    if(slot==0){oq_feature_api->fill(c,0,0,w,h,0x343434);oq_feature_api->outline(c,0,0,w,1,1,0x404040);
        oq_feature_api->fill(c,0,0,1,h,0x404040);oq_feature_api->fill(c,w-1,0,1,h,0x404040);
        oq_feature_api->text(c,0,0,w,h,"BEAT FX",14,0x808080);
    }else if(slot==5){oq_feature_api->fill(c,0,0,w,h,0);oq_feature_api->text(c,0,0,w,h,"TIME",11,0x808080);}
    else oq_feature_api->fill(c,0,0,w,h,0x404040);
}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->size<sizeof *a||!a->components||!a->native_view_provider||!a->native_engine||!a->native_get||!a->panel_renderer||!a->panels_set||!a->fill||!a->text||!a->outline)return 1;
    if(oq_feature_init(a,info)||!a->native_get("engine_external",0)||!a->native_engine())return 1;
    sb_components.size=sizeof sb_components;if(!a->components(&sb_components)||!sb_components.class_name||!sb_components.bounds||!sb_components.visible||!sb_components.reparent||!a->panel_renderer(sidebar_draw,NULL))return 1;
    struct oqmod_native_view_provider provider={sizeof provider,sidebar_converge,sidebar_restore,sidebar_visible};
    if(!a->native_view_provider(OQMOD_NATIVE_VIEW_FX_SIDEBAR,&provider))return 1;
    info->version="2.0";return 0;
}

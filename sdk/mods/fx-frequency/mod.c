/* USB implementation: packet hooks/policy, range buttons, menu and preferences.
 * Requires the extraction host, not R112's built-in frequency engine. */
#include "oqmod_native_feature.h"
#include <string.h>
#include "frequency.h"
static const char *const values0[]={"LOW", "HIGH", "ALL"};
static const struct oq_native_option oq_feature_options[]={
    {"selection","FX range",values0,3,2,1},
};
static const struct oq_native_feature oq_feature={"fx-frequency","FX Frequency","LOW, ALL or HIGH chooses the filter-assisted FX range.\nEnable Beat FX Sidebar to show the on-screen controls.\nThe physical Beat FX ON button still engages the effect.", 70};
#define OQ_NATIVE_CUSTOM_ENTRY
#define OQ_NATIVE_FEATURE_IMPLEMENTATION
#include "oqmod_native_feature.h"
#include "engine.h"
static unsigned requested,shown_selection=3;
static unsigned report_rx,report_tx,report_changes;
static uint64_t report_at;
static void tick(void){
    unsigned request=__atomic_exchange_n(&requested,0,__ATOMIC_ACQ_REL);
    if(request)oq_feature_api->setting_set("selection",(int)request-1);
    oq_feature_tick();
    unsigned selected=(unsigned)oq_feature_api->setting_get("selection",2)+1;
    if(selected<1||selected>3)selected=3;
    fx_engine_set(oq_feature_on,selected);
    if(selected!=__atomic_load_n(&shown_selection,__ATOMIC_ACQUIRE)){
        __atomic_store_n(&shown_selection,selected,__ATOMIC_RELEASE);oq_feature_api->redraw();
    }
    unsigned rx=__atomic_load_n(&fx_engine.rx,__ATOMIC_ACQUIRE),tx=__atomic_load_n(&fx_engine.tx,__ATOMIC_ACQUIRE);
    unsigned changed=__atomic_load_n(&fx_engine.changed,__ATOMIC_ACQUIRE);
    uint64_t now=oq_feature_api->now_ms();
    if(now-report_at>=1000&&(rx!=report_rx||tx!=report_tx||changed!=report_changes)){
        char status[192];snprintf(status,sizeof status,"USB engine: hooks %u/3; RX %u; TX %u; changed %u\nY-only filter proxy; original builder/CRC unchanged.",fx_engine.hooks,rx,tx,changed);
        oq_feature_api->set_status(status);report_rx=rx;report_tx=tx;report_changes=changed;report_at=now;
    }
}
static void enabled(int on){
    fx_engine_set(on,(unsigned)oq_feature_api->setting_get("selection",2)+1);
    oq_feature_enabled(on);
    if(fx_engine.hooks!=3)oq_feature_api->set_status("Native hook guard refused; frequency packets unchanged.");
    oq_feature_api->redraw();
}
static void draw(struct oqmod_canvas *c,int w,int h){
    struct oqmod_surface s;if(!oq_feature_api->surface(OQMOD_SURFACE_FX_FREQUENCY,&s))return;
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
    static const unsigned ranges[]={1,3,2};
    unsigned current=__atomic_load_n(&shown_selection,__ATOMIC_ACQUIRE);
    unsigned next=fx_frequency_select(current,ranges[x/61]);
    __atomic_store_n(&requested,next,__ATOMIC_RELEASE);return 1;
}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->size<sizeof *a||!a->surface||!a->use_surface||!a->set_status||
       !a->fill||!a->outline||!a->text||!a->redraw||!a->native_get||!a->native_engine||!a->now_ms)return 1;
    if(oq_feature_init(a,info))return 1;
    if(!a->native_get("engine_external",0)||!a->native_engine()||!a->use_surface(OQMOD_SURFACE_FX_FREQUENCY))return 1;
    struct oqmod_surface slot;memset(&slot,0,sizeof slot);a->surface(OQMOD_SURFACE_FX_FREQUENCY,&slot);
    if(slot.w!=180||slot.h!=66)return 1;
    (void)fx_install(a);
    info->x=slot.x;info->y=slot.y;info->w=slot.w;info->h=slot.h;
    info->enabled=enabled;info->tick=tick;info->draw=draw;info->touch=touch;info->version="2.0";
    return 0;
}

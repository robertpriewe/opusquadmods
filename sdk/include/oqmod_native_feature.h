#ifndef OQMOD_NATIVE_FEATURE_H
#define OQMOD_NATIVE_FEATURE_H
/* Optional reusable client for a named native service. Define oq_feature and
 * oq_feature_options before including this header; see the native-feature
 * example. Policies and option values belong to the add-on, not to menu IDs. */
#include <stddef.h>
#include <stdio.h>
#include "oqmod.h"
struct oq_native_option {
    const char *property,*label;
    const char *const *values;unsigned count;
    int fallback,base;
};
struct oq_native_feature {
    const char *service,*name,*description;int priority;
};
#endif
#if defined(OQ_NATIVE_FEATURE_IMPLEMENTATION) && !defined(OQ_NATIVE_FEATURE_IMPLEMENTED)
#define OQ_NATIVE_FEATURE_IMPLEMENTED
static const struct oqmod_api *oq_feature_api;
static int oq_feature_on,oq_feature_last[4],oq_feature_force,oq_feature_epoch;
static unsigned oq_feature_pending;
static int oq_feature_revision[4];
static unsigned oq_feature_count(void){return sizeof oq_feature_options/sizeof *oq_feature_options;}
static void oq_feature_apply(void){
    int epoch=oq_feature_api->native_get("generation",0);
    if(epoch!=oq_feature_epoch){oq_feature_epoch=epoch;oq_feature_force=1;oq_feature_pending=0;}
    for(unsigned i=0;i<oq_feature_count();i++){
        const struct oq_native_option *o=&oq_feature_options[i];
        char acknowledgement[80];snprintf(acknowledgement,sizeof acknowledgement,"applied.%s",o->property);
        int v=oq_feature_api->setting_get(o->property,o->fallback);
        if(v<0||(unsigned)v>=o->count)v=o->fallback;
        if(oq_feature_force||v!=oq_feature_last[i]){
            int revision=oq_feature_api->native_get(acknowledgement,0);
            if(oq_feature_api->native_set(o->property,v+o->base)){
                oq_feature_last[i]=v;oq_feature_revision[i]=revision;oq_feature_pending|=1u<<i;
            }else{oq_feature_last[i]=-1;oq_feature_pending&=~(1u<<i);}
        }else if(oq_feature_on){
            /* set() only queues a request. Until the GUI has consumed it, an
             * old read-back is NOT a new user edit of the saved preference. */
            if(oq_feature_pending>>i&1u){
                if(oq_feature_api->native_get(acknowledgement,0)==oq_feature_revision[i])continue;
                oq_feature_pending&=~(1u<<i);
            }
            /* Main-screen tabs and FX range buttons remain native controls.
             * Mirror their changes back into this add-on's preferences. */
            int actual=oq_feature_api->native_get(o->property,v+o->base)-o->base;
            if(actual>=0&&(unsigned)actual<o->count&&actual!=v){
                oq_feature_api->setting_set(o->property,actual);oq_feature_last[i]=actual;
            }
        }
    }
    oq_feature_force=0;
}
static void oq_feature_tick(void){
    if(oq_feature_on){oq_feature_apply();oq_feature_api->native_set("enabled",1);}
}
static void oq_feature_enabled(int on){
    oq_feature_on=!!on;
    if(on){oq_feature_force=1;oq_feature_apply();}
    oq_feature_api->native_set("enabled",!!on);
    oq_feature_api->log(on?"native service enabled":"native service disabled");
}
static int oq_feature_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->version!=OQMOD_API_VERSION||a->size<sizeof *a||!a->native_bind||!a->native_get||!a->native_set||
       !a->menu_order||!a->default_enabled||!a->choice_buttons||!a->setting_get||!a->setting_set||!a->log||
       oq_feature_count()>4||!a->native_bind(oq_feature.service)||a->native_get("protocol",0)<2)return 1;
    oq_feature_api=a;a->menu_order(oq_feature.priority);
    a->default_enabled(a->native_get("initial.enabled",0));
    for(unsigned i=0;i<oq_feature_count();i++){
        const struct oq_native_option *o=&oq_feature_options[i];
        char initial[80];snprintf(initial,sizeof initial,"initial.%s",o->property);
        int fallback=a->native_get(initial,o->fallback+o->base)-o->base;
        if(fallback<0||(unsigned)fallback>=o->count)fallback=o->fallback;
        int value=a->choice_buttons(o->property,o->label,o->values,o->count,fallback);
        a->setting_set(o->property,value);
        oq_feature_last[i]=-1;
    }
    info->api_version=OQMOD_API_VERSION;info->name=oq_feature.name;
    info->description=oq_feature.description;info->version="1.1";
    info->enabled=oq_feature_enabled;info->tick=oq_feature_tick;
    return 0;
}
#ifndef OQ_NATIVE_CUSTOM_ENTRY
__attribute__((visibility("default")))
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){return oq_feature_init(a,info);}
#endif
#endif

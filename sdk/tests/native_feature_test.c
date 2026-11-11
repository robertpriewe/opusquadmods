#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "oqmod_native_feature.h"
static const char *const choices[]={"One","Two","Three"};
static const struct oq_native_option oq_feature_options[]={{"sensitivity","Sensitivity",choices,3,2,1}};
static const struct oq_native_feature oq_feature={"fx-target","Target","SDK test",90};
#define OQ_NATIVE_FEATURE_IMPLEMENTATION
#include "oqmod_native_feature.h"
static int preference=-1,current=3,active,priority,default_on,writes;
static int deferred,pending=-1,revision,generation=1,reject,protocol=2;
static int bind(const char *s){return !strcmp(s,"fx-target");}
static int get(const char *p,int fallback){
    if(!strcmp(p,"protocol"))return protocol;
    if(!strcmp(p,"generation"))return generation;
    if(!strcmp(p,"applied.sensitivity"))return revision;
    if(!strcmp(p,"initial.enabled"))return 1;
    if(!strcmp(p,"initial.sensitivity"))return 3;
    return !strcmp(p,"sensitivity")?current:fallback;
}
static int set(const char *p,int v){
    if(reject)return 0;
    writes++;if(!strcmp(p,"enabled"))active=v;
    else {assert(!strcmp(p,"sensitivity")&&v>=1&&v<=3);if(deferred)pending=v;else {current=v;revision++;}}
    return 1;
}
static int setting_get(const char *k,int fallback){assert(!strcmp(k,"sensitivity"));return preference<0?fallback:preference;}
static void setting_set(const char *k,int v){assert(!strcmp(k,"sensitivity"));preference=v;}
static int option(const char *k,const char *label,const char *const *names,unsigned count,int fallback){
    assert(!strcmp(k,"sensitivity")&&label&&names&&count==3);return preference<0?fallback:preference;
}
static void order(int p){priority=p;}
static void def(int n){default_on=n;}
static void log_line(const char *s){assert(s);}
int main(void){
    struct oqmod_api a={.version=OQMOD_API_VERSION,.size=sizeof a,.native_bind=bind,.native_get=get,.native_set=set,
        .menu_order=order,.default_enabled=def,.choice_buttons=option,.setting_get=setting_get,.setting_set=setting_set,.log=log_line};
    struct oqmod_info info={0};
    unsigned full=a.size;a.size=offsetof(struct oqmod_api,native_bind);
    assert(oqmod_init(&a,&info)!=0&&!writes);a.size=full;
    protocol=1;assert(oqmod_init(&a,&info)!=0&&!writes);protocol=2;
    assert(!oqmod_init(&a,&info)&&priority==90&&default_on==1&&preference==2);
    info.enabled(1);assert(active&&current==3);
    preference=0;info.tick();assert(current==1);
    current=2;info.tick();assert(preference==1); /* native main-screen changes sync */
    info.enabled(0);assert(!active);int n=writes;info.tick();assert(n==writes);
    info.enabled(1);assert(active&&current==2);
    info.enabled(0);preference=0;current=3;deferred=1;
    info.enabled(1);assert(pending==1&&preference==0);
    info.tick();assert(preference==0); /* queued request is not a native user edit */
    current=pending;pending=-1;revision++;info.tick();assert(preference==0);
    /* A real native edit after the queued apply wins, even if it happens before
     * the client first observes the requested value on screen. */
    preference=1;info.tick();assert(pending==2);current=3;revision++;
    info.tick();assert(preference==2);
    /* Re-reading global settings must re-assert the add-on's preference, not
     * import a reset native value back over it. */
    generation++;current=1;info.tick();assert(pending==3&&preference==2);
    reject=1;preference=0;info.tick();assert(preference==0);
    reject=0;info.tick();assert(pending==1&&preference==0);
    puts("PASS: SDK native client: old-host refusal, defaults/value offsets, deferred-apply acknowledgement, native edit sync, epoch reset, failed-request retry and enable/disable restoration");
}

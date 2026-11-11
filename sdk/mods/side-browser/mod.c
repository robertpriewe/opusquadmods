/* USB-owned browser state, bookmarks, gestures, pages and confirmation policy. */
#include "oqmod_native_feature.h"
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include <string.h>
#include "browser_input.h"
#include "browser_wire.h"
#include "deck_view_wire.h"
static const char *const values0[]={"Deck Select", "Track Search", "Beat Loop wheels"};
static const struct oq_native_option oq_feature_options[]={
    {"navigation","Controls",values0,3,2,0},
};
static const struct oq_native_feature oq_feature={"side-browser","Side Screen Browser","Hold a Deck Select button 1 second to open or close.\nUSB > Playlists > Tracks. Beat Loop: turn to scroll, press\nto open or confirm, hold to go back. LOAD LOCK required.", 30};
#define OQ_NATIVE_CUSTOM_ENTRY
#define OQ_NATIVE_FEATURE_IMPLEMENTATION
#include "oqmod_native_feature.h"
static struct oqmod_browser_services browser_io;
enum {BR_USBS=OQMOD_LIBRARY_SOURCES};
static uint64_t browser_now(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return (uint64_t)ts.tv_sec*1000000000ull+ts.tv_nsec;}
static const char *browser_reason(int n){
    static const char *const r[]={"READY","NATIVE LOADER NOT READY","USB CHANGED - SELECT TRACK AGAIN","ENABLE STOCK LOAD LOCK FIRST","DECK IS PLAYING - PAUSE FIRST","DECK STATE UNKNOWN - CANCEL"};
    return n>=0&&n<6?r[n]:r[5];
}
#include "engine.h"
static void *browser_worker(void *unused){
    (void)unused;uint64_t checked=0;
    for(;;){
        struct timespec wait={0,33000000};nanosleep(&wait,0);br_poll();
        uint64_t now=browser_now();
        if(now-checked>=1000000000ull){
            checked=now;br_check_media();struct oqmod_library_source found[BR_USBS];
            unsigned n=browser_io.sources(found,BR_USBS);
            pthread_mutex_lock(&br_mutex);memcpy(br_usbs,found,n*sizeof *found);br_usb_count=n;pthread_mutex_unlock(&br_mutex);
        }
        for(unsigned side=0;side<2;side++){
            struct br_job job={0};
            pthread_mutex_lock(&br_mutex);struct br_side *s=&br_sides[side];
            if(!br_enabled||!s->armed||s->done==s->revision){pthread_mutex_unlock(&br_mutex);continue;}
            job.side=side;job.deck=s->deck;job.level=s->level;job.first=s->cursor/5*5;job.revision=s->revision;job.playlist=s->playlist;
            job.restore=s->restore;job.track=s->restore_track;if(job.level)job.usb=s->selected;
            pthread_mutex_unlock(&br_mutex);
            struct oqmod_catalog_page page={0};struct oqmod_library_source found[BR_USBS];
            unsigned count=0;const char *error=NULL;int status=0;
            if(!job.level){
                count=browser_io.sources(found,BR_USBS);page.total=count;
                if(job.first>=count)job.first=0;
                page.first=job.first;
                for(unsigned i=job.first;i<count&&page.count<5;i++){
                    struct oqmod_catalog_item *r=&page.row[page.count++];r->id=br_usb_id(&found[i]);snprintf(r->title,sizeof r->title,"%s",found[i].label);
                }
                if(!count)error="NO ONELIBRARY USB FOUND";
            }else{
                struct oqmod_catalog_query q={.source=job.usb,.playlist=job.playlist,.track=job.track,.level=job.level,.first=job.first,.restore=job.restore};
                int result=browser_io.query(&q,&page);status=q.status;
                if(result==1){job.cursor=q.cursor;job.parent=q.parent;}
                else{
                    static const char *const errors[]={"LIBRARY QUERY FAILED","USB CHANGED - GO BACK TO USB SELECT","LIBRARY READER KEY UNAVAILABLE","LIBRARY BUSY (WAL) - RECONNECT USB","LIBRARY OPEN FAILED","SAVED PLAYLIST CHANGED - GO BACK","LIBRARY QUERY FAILED"};
                    error=errors[result<0&&result>=-6?-result:0];
                }
            }
            br_apply(&job,&page,error,found,count,status);
        }
    }
    return NULL;
}
static unsigned browser_started;
static void browser_start_worker(void){
    if(browser_started)return;
    pthread_t worker;if(pthread_create(&worker,NULL,browser_worker,NULL))return;
    pthread_detach(worker);browser_started=1;__atomic_store_n(&br_ready,1,__ATOMIC_RELEASE);
}
static void browser_enabled(int on){oq_feature_enabled(on);br_set_enabled(on);if(on)browser_start_worker();}
static void browser_tick(void){oq_feature_tick();if(oq_feature_on)browser_start_worker();}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->size<sizeof *a||!a->browser_services||!a->browser_provider||!a->native_engine)return 1;
    if(oq_feature_init(a,info)||!a->native_get("engine_external",0)||!a->native_engine())return 1;
    browser_io.size=sizeof browser_io;
    if(!a->browser_services(&browser_io)||!browser_io.sources||!browser_io.query||!browser_io.page||!browser_io.close||!browser_io.rich||!browser_io.state||!browser_io.ready||!browser_io.preflight||!browser_io.dispatch)return 1;
    struct oqmod_browser_provider p={sizeof p,br_selector,br_navigation,br_wheel_input,br_poll,br_load_pump,br_set_enabled,br_set_navigation,br_import_native};
    if(!a->browser_provider(&p))return 1;
    info->enabled=browser_enabled;info->tick=browser_tick;info->version="2.0";return 0;
}

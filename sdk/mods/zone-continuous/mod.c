/* USB-owned continuation/autoplay policy. Early loader/routing observers are
 * SDK infrastructure and never change a flag, load a track or post PLAY. */
#include "oqmod_native_feature.h"
#include <pthread.h>
#include <string.h>
static const struct oq_native_option oq_feature_options[]={};
static const struct oq_native_feature oq_feature={"zone-continuous","Zone Continuous Play","The deck assigned to ZONE on channel 3 or 4 continues\nto its playlist's next track. Other decks are unchanged.",130};
#define OQ_NATIVE_CUSTOM_ENTRY
#define OQ_NATIVE_FEATURE_IMPLEMENTATION
#include "oqmod_native_feature.h"
static int (*zone_original)(void *);
static pthread_mutex_t zone_lock=PTHREAD_MUTEX_INITIALIZER;
static int zone_enabled,zone_deck=-1;
static unsigned zone_hooks,zone_epoch,zone_forced,zone_plays,zone_refused;
static struct {int valid;uint64_t at;uint32_t length;unsigned epoch;} zone_pending[4];
static void zone_state(int on,int deck){
    pthread_mutex_lock(&zone_lock);
    if(zone_enabled!=on||zone_deck!=deck){zone_epoch++;memset(zone_pending,0,sizeof zone_pending);}
    zone_enabled=on;zone_deck=deck;
    pthread_mutex_unlock(&zone_lock);
}
static int zone_try(void *self){
    unsigned deck=oq_feature_api->object_deck(1,self);
    pthread_mutex_lock(&zone_lock);
    int active=zone_enabled&&deck>=1&&deck<=4&&(int)deck==zone_deck;
    unsigned epoch=zone_epoch;
    pthread_mutex_unlock(&zone_lock);
    if(!active)return zone_original(self);
    unsigned char *flag=(unsigned char *)self+40,saved=*flag;
    struct oqmod_deck before={0};oq_feature_api->deck(deck,&before);
    *flag=1;
    int result=zone_original(self);
    *flag=saved;
    pthread_mutex_lock(&zone_lock);
    if(!saved)zone_forced++;
    if((result&255)&&zone_enabled&&epoch==zone_epoch&&(int)deck==zone_deck){
        zone_pending[deck-1].valid=1;zone_pending[deck-1].epoch=epoch;
        zone_pending[deck-1].at=oq_feature_api->now_ms();zone_pending[deck-1].length=before.length_ms;
    }
    pthread_mutex_unlock(&zone_lock);return result;
}
static void tick(void){
    oq_feature_tick();
    int on=oq_feature_api->native_get("zone_on",-1),assignment=oq_feature_api->native_get("zone_assign",-1);
    int deck=on==1&&(assignment==0||assignment==1)?assignment+3:-1;
    zone_state(oq_feature_on&&zone_hooks==1,deck);
    uint64_t now=oq_feature_api->now_ms();
    for(unsigned d=0;d<4;d++){
        struct oqmod_deck state={0};oq_feature_api->deck(d+1,&state);
        pthread_mutex_lock(&zone_lock);
        int play=zone_pending[d].valid&&zone_enabled&&zone_deck==(int)d+1&&zone_pending[d].epoch==zone_epoch&&
            now>=zone_pending[d].at&&((state.length_ms&&state.length_ms!=zone_pending[d].length&&now-zone_pending[d].at>=500)||now-zone_pending[d].at>=3000);
        if(play)zone_pending[d].valid=0;
        pthread_mutex_unlock(&zone_lock);
        if(play){int sent=oq_feature_api->deck_play(d+1);if(sent)zone_plays++;else zone_refused++;}
    }
    static uint64_t report_at;
    if(now>=report_at){
        char text[160];pthread_mutex_lock(&zone_lock);
        snprintf(text,sizeof text,"USB engine: hook %u; zone deck %d\nForced %u; PLAY %u; refused %u",zone_hooks,zone_deck,zone_forced,zone_plays,zone_refused);
        pthread_mutex_unlock(&zone_lock);oq_feature_api->set_status(text);report_at=now+1000;
    }
}
static void enabled(int on){zone_state(0,-1);oq_feature_enabled(on);}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->size<sizeof *a||!a->object_deck||!a->deck||!a->deck_play||!a->now_ms||!a->native_get||
       !a->native_engine||!a->hook||!a->ep136_ok||!a->set_status)return 1;
    if(oq_feature_init(a,info)||!a->native_get("engine_external",0)||!a->native_engine())return 1;
    zone_hooks=a->ep136_ok()&&a->hook(0x7b5a70,0xa9bc7bfd,(void *)zone_try,(void **)&zone_original)==1;
    info->enabled=enabled;info->tick=tick;info->version="2.0";return 0;
}

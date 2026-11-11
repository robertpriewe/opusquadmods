/* USB-owned forward Slip swap, state convergence, cleanup and LED indication. */
#include "oqmod_native_feature.h"
#include <string.h>
static const struct oq_native_option oq_feature_options[]={
};
static const struct oq_native_feature oq_feature={"slip-mode","Slip Mode","Swap each physical SLIP REVERSE button for forward Slip.\nThe button toggles Slip per deck; LEDs show its state.\nDisable to restore the stock SLIP REVERSE function.", 100};
#define OQ_NATIVE_CUSTOM_ENTRY
#define OQ_NATIVE_FEATURE_IMPLEMENTATION
#include "oqmod_native_feature.h"
static void (*slip_original)(void *,unsigned,const void *);
static unsigned slip_hook,slip_enabled,slip_requests[4],slip_last_selected[2];
static uintptr_t slip_held[4];
static unsigned slip_owned,slip_pending,slip_expected,slip_errors,slip_led_sides;
static uint64_t slip_deadline[4],slip_last_poll;
static void slip_edge(void *self,unsigned trigger,const void *priority){
    unsigned deck=oq_feature_api->object_deck(2,self);
    if(deck>=1&&deck<=4&&trigger<=1){
        unsigned d=deck-1;
        if(!trigger)__atomic_store_n(&slip_last_selected[d&1],deck,__ATOMIC_RELEASE);
        if(trigger==1&&slip_held[d]==(uintptr_t)self){slip_held[d]=0;return;}
        if(trigger==0&&__atomic_load_n(&slip_enabled,__ATOMIC_ACQUIRE)){
            if(!slip_held[d]){slip_held[d]=(uintptr_t)self;__atomic_fetch_add(&slip_requests[d],1,__ATOMIC_RELEASE);}
            return;
        }
    }
    slip_original(self,trigger,priority);
}
static void slip_release_leds(unsigned side){
    const unsigned leds[]={OQMOD_LED_JOG_CENTER,OQMOD_LED_INNER_CIRCLE,OQMOD_LED_SLIP_REVERSE};
    for(unsigned i=0;i<3;i++)oq_feature_api->led_set(side,leds[i],0,0);
    slip_led_sides&=~(1u<<side);
}
static void slip_tick(void){
    if(oq_feature_on)oq_feature_tick();
    int enabled=oq_feature_on&&slip_hook;
    __atomic_store_n(&slip_enabled,enabled,__ATOMIC_RELEASE);
    unsigned requests=0;for(unsigned d=0;d<4;d++)if(__atomic_exchange_n(&slip_requests[d],0,__ATOMIC_ACQ_REL)&1u)requests|=1u<<d;
    if(!enabled&&!slip_owned&&!slip_pending&&!requests)return;
    uint64_t now=oq_feature_api->now_ms();if(!requests&&now-slip_last_poll<100)return;slip_last_poll=now;
    struct oqmod_slip_snapshot state={.size=sizeof state};
    if(!oq_feature_api->deck_slip_read(&state))return;
    unsigned ready=state.ready&15,on=state.on&state.input_on&15,synced=ready&~(state.on^state.input_on);
    for(unsigned d=0;d<4;d++){
        unsigned bit=1u<<d;
        if(slip_pending&bit){
            if((ready&synced&bit)&&((on&bit)==(slip_expected&bit))){slip_pending&=~bit;slip_errors&=~bit;if(!(slip_expected&bit))slip_owned&=~bit;}
            else if(now>=slip_deadline[d]){slip_pending&=~bit;slip_errors|=bit;}
        }
        int close=!enabled&&(slip_owned&bit)&&!(slip_pending&bit);
        if(!close&&(!enabled||!(requests&bit)))continue;
        if(!(ready&bit)||(slip_pending&bit))continue;
        unsigned desired=close?0:!(on&bit);
        if(!oq_feature_api->deck_slip_set(d+1,desired)){slip_errors|=bit;continue;}
        slip_pending|=bit;slip_deadline[d]=now+2000;
        if(desired){slip_expected|=bit;slip_owned|=bit;}else slip_expected&=~bit;
    }
    for(unsigned side=0;side<2;side++){
        unsigned selected=state.selected[side];
        if(selected<1||selected>4||((selected-1)&1)!=side)selected=__atomic_load_n(&slip_last_selected[side],__ATOMIC_ACQUIRE);
        if(enabled&&selected>=1&&selected<=4&&((selected-1)&1)==side&&(ready&on&(1u<<(selected-1)))){
            unsigned phase=(unsigned)((now/250)&1);slip_led_sides|=1u<<side;
            oq_feature_api->led_set(side,OQMOD_LED_JOG_CENTER,phase?0xffffff:0,150);
            oq_feature_api->led_set(side,OQMOD_LED_INNER_CIRCLE,phase?0xff0000:0,150);
            oq_feature_api->led_set(side,OQMOD_LED_SLIP_REVERSE,phase?0xffffff:0,150);
        }else if(slip_led_sides>>side&1u)slip_release_leds(side);
    }
    static unsigned reported=~0u;unsigned report=on|(slip_pending<<4)|(slip_errors<<8)|(slip_owned<<12);
    if(reported!=report){
        char text[128];snprintf(text,sizeof text,"USB Slip: native ON %x; pending %x\nOwned %x; errors %x",on,slip_pending,slip_owned,slip_errors);
        reported=report;oq_feature_api->set_status(text);
    }
}
static void slip_on(int on){
    __atomic_store_n(&slip_enabled,0,__ATOMIC_RELEASE);
    for(unsigned d=0;d<4;d++)__atomic_store_n(&slip_requests[d],0,__ATOMIC_RELEASE);
    for(unsigned side=0;side<2;side++)if(slip_led_sides>>side&1u)slip_release_leds(side);
    oq_feature_enabled(on);slip_last_poll=0;
    __atomic_store_n(&slip_enabled,on&&slip_hook,__ATOMIC_RELEASE);
}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->size<sizeof *a||!a->object_deck||!a->deck_slip_read||!a->deck_slip_set||!a->background_tick||!a->now_ms||!a->led_set||!a->set_status||!a->ep136_ok||!a->hook||!a->native_get||!a->native_engine)return 1;
    if(oq_feature_init(a,info)||!a->native_get("engine_external",0)||!a->native_engine()||!a->background_tick(1))return 1;
    slip_hook=a->ep136_ok()&&a->hook(0x1c74558,0xa9b47bfd,(void *)slip_edge,(void **)&slip_original)==1;
    info->enabled=slip_on;info->tick=slip_tick;info->version="2.0";return 0;
}

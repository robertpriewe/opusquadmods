/* USB-owned TIME navigation policy. Core retains native decoding/absolute ACK,
 * target observation/action and a fail-open controller TIME suppression lease. */
#include "oqmod_native_feature.h"
static const char *const values0[]={"1", "2", "3", "4", "5"};
static const struct oq_native_option oq_feature_options[]={
    {"sensitivity","Detents per target",values0,5,2,1},
};
static const struct oq_native_feature oq_feature={"fx-target","FX Target Knob","TIME selects MIC / 3 / 1 / 2 / 4 / MASTER.\nHold either deck's SHIFT to adjust effect time instead.\nFX SELECT retains its normal function.", 90};
#define OQ_NATIVE_CUSTOM_ENTRY
#define OQ_NATIVE_FEATURE_IMPLEMENTATION
#include "oqmod_native_feature.h"
#include "policy.h"
static int target_enabled,target_ready,target_counts;
static struct ft_divider target_divider;
static unsigned target_waiting,target_wanted,target_requested_generation,target_seen_generation;
static uint64_t target_deadline;
static unsigned target_changes,target_rejected;
static int target_event(const struct oqmod_encoder_event *event){
    if(!event||event->identity!=0x000aaf5ef2bf6fb5ull||!event->raw||event->push>1||event->shift||
       !__atomic_load_n(&target_enabled,__ATOMIC_ACQUIRE)||!__atomic_load_n(&target_ready,__ATOMIC_ACQUIRE))return 0;
    if(event->raw< -65535||event->raw>65535)return 0;
    int move=ft_movement(event->raw);
    if(move>=-64&&move<=64){
        int old=__atomic_load_n(&target_counts,__ATOMIC_ACQUIRE),next;
        do{next=old+move;if(next>512)next=512;if(next< -512)next= -512;}
        while(!__atomic_compare_exchange_n(&target_counts,&old,next,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE));
    }else __atomic_fetch_add(&target_rejected,1,__ATOMIC_RELAXED);
    return 1;
}
static void target_reset(void){
    __atomic_store_n(&target_counts,0,__ATOMIC_RELEASE);target_divider=(struct ft_divider){0};target_waiting=0;
}
static void target_tick(void){
    oq_feature_tick();
    struct oqmod_fx_target state={.size=sizeof state};
    int ready=oq_feature_api->fx_target_read(&state)&&state.ready;
    __atomic_store_n(&target_ready,ready,__ATOMIC_RELEASE);
    int counts=__atomic_exchange_n(&target_counts,0,__ATOMIC_ACQ_REL);
    if(!oq_feature_on||!ready||state.shift){target_reset();oq_feature_api->time_lease(0);return;}
    oq_feature_api->time_lease(1);
    unsigned sensitivity=(unsigned)oq_feature_api->setting_get("sensitivity",2)+1;
    ft_divide_by(&target_divider,counts,sensitivity);
    uint64_t now=oq_feature_api->now_ms();
    if(target_waiting){
        if(state.value==target_wanted){target_waiting=0;target_seen_generation=state.generation;}
        else if(state.generation!=target_requested_generation){target_seen_generation=state.generation;target_reset();return;}
        else if(now<target_deadline)return;
        else{target_rejected++;target_reset();return;}
    }else if(state.generation!=target_seen_generation){
        target_seen_generation=state.generation;target_divider=(struct ft_divider){0};ft_divide_by(&target_divider,counts,sensitivity);
    }
    if(!target_divider.pending)return;
    int direction=target_divider.pending>0?1:-1;target_divider.pending-=direction;
    unsigned wanted=ft_clock_target(state.value,state.mic,direction);
    if(!oq_feature_api->fx_target_select(wanted)){target_rejected++;target_reset();return;}
    target_waiting=1;target_wanted=wanted;target_requested_generation=state.generation;target_deadline=now+500;target_changes++;
}
static void target_on(int on){
    __atomic_store_n(&target_enabled,0,__ATOMIC_RELEASE);target_reset();oq_feature_api->time_lease(0);
    oq_feature_enabled(on);__atomic_store_n(&target_enabled,!!on,__ATOMIC_RELEASE);
}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->size<sizeof *a||!a->encoder_bind||!a->fx_target_read||!a->fx_target_select||!a->time_lease||!a->now_ms||!a->native_get||!a->native_engine)return 1;
    if(oq_feature_init(a,info)||!a->native_get("engine_external",0)||!a->native_engine()||!a->encoder_bind(0x000aaf5ef2bf6fb5ull,target_event))return 1;
    info->tick=target_tick;info->enabled=target_on;info->version="2.0";return 0;
}

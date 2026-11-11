#include <assert.h>
#include <string.h>
#include "../mods/fx-target/mod.c"
static struct oqmod_fx_target snapshot={.size=sizeof snapshot,.ready=1,.value=4,.mic=0,.generation=1};
static int sensitivity=2,lease;static unsigned requests,wanted;static uint64_t clock_ms=100;
static int (*input)(const struct oqmod_encoder_event *);
static int bind_service(const char *s){return !strcmp(s,"fx-target");}
static int get(const char *p,int fallback){if(!strcmp(p,"protocol"))return 2;if(!strcmp(p,"engine_external"))return 1;return fallback;}
static int set(const char *p,int v){(void)p;(void)v;return 1;}
static int setting(const char *p,int fallback){return !strcmp(p,"sensitivity")?sensitivity:fallback;}
static void setting_set(const char *p,int v){assert(!strcmp(p,"sensitivity"));sensitivity=v;}
static int yes(void){return 1;}
static int bind_encoder(uint64_t id,int (*handler)(const struct oqmod_encoder_event *)){assert(id==0x000aaf5ef2bf6fb5ull);input=handler;return 1;}
static int read_target(struct oqmod_fx_target *out){assert(out->size==sizeof *out);*out=snapshot;return 1;}
static int select_target(unsigned value){requests++;wanted=value;return 1;}
static int own_time(int active){lease=active;return 1;}
static uint64_t now(void){return clock_ms;}
static void number(int n){(void)n;}
static void text(const char *s){(void)s;}
static int choices(const char *k,const char *l,const char *const *v,unsigned n,int f){(void)k;(void)l;(void)v;assert(n==5);return f;}
static void rotate(int delta,unsigned shift){struct oqmod_encoder_event e={0x000aaf5ef2bf6fb5ull,delta,0,shift};assert(input(&e)==(delta&&!shift&&target_enabled&&target_ready));}
int main(void){
    struct oqmod_api a={.version=1,.size=sizeof a,.native_bind=bind_service,.native_get=get,.native_set=set,.native_engine=yes,
        .encoder_bind=bind_encoder,.fx_target_read=read_target,.fx_target_select=select_target,.time_lease=own_time,.now_ms=now,
        .setting_get=setting,.setting_set=setting_set,.menu_order=number,.default_enabled=number,.choice_buttons=choices,.log=text};
    struct oqmod_info info={0};assert(!oqmod_init(&a,&info)&&input);info.enabled(1);info.tick();assert(lease);
    rotate(1,0);info.tick();rotate(1,0);info.tick();assert(!requests);rotate(1,0);info.tick();assert(requests==1&&wanted==5);
    clock_ms+=1;snapshot.value=wanted;snapshot.generation++;info.tick();
    for(unsigned n=0;n<12;n++){
        unsigned old=snapshot.value;rotate(3,0);info.tick();assert(wanted==ft_clock_target(old,0,1));snapshot.value=wanted;snapshot.generation++;info.tick();
    }
    rotate(-3,0);info.tick();assert(wanted==ft_clock_target(snapshot.value,0,-1));snapshot.value=wanted;snapshot.generation++;info.tick();
    unsigned before=requests;snapshot.shift=1;rotate(3,1);info.tick();assert(!lease&&requests==before);
    snapshot.shift=0;info.tick();assert(lease);
    sensitivity=0;rotate(1,0);info.tick();assert(requests==before+1);clock_ms+=501;info.tick();assert(!target_waiting&&target_rejected);
    info.enabled(0);rotate(1,0);assert(!lease);info.enabled(1);info.tick();assert(!target_counts);
    struct oqmod_encoder_event foreign={0x9999,3,0,0};assert(!input(&foreign));foreign.identity=0x000aaf5ef2bf6fb5ull;foreign.raw=999999;assert(!input(&foreign));
    snapshot.ready=0;info.tick();assert(!lease&&!target_ready);
    puts("PASS: USB TIME engine, sensitivity 1/3, circular targets, pending ACK/timeout, SHIFT passthrough, disable cancellation and unknown-state lease refusal");
}

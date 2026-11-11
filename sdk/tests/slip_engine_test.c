#include <assert.h>
#include "../mods/slip-mode/mod.c"
static struct oqmod_slip_snapshot native={.size=sizeof native,.ready=15,.selected={1,2}};
static unsigned objects[4],originals,requests,last_deck,last_on,led_red,led_released;
static uint64_t clock_ms=1000;static void (*edge)(void *,unsigned,const void *);
static unsigned object_deck(unsigned kind,const void *p){if(kind!=2)return 0;for(unsigned d=0;d<4;d++)if(p==objects+d)return d+1;return 0;}
static void original(void *p,unsigned trigger,const void *priority){(void)p;(void)trigger;(void)priority;originals++;}
static int hook(uintptr_t at,uint32_t first,void *handler,void **out){assert(at==0x1c74558&&first==0xa9b47bfd);edge=handler;*out=(void *)original;return 1;}
static int snapshot(struct oqmod_slip_snapshot *out){assert(out->size==sizeof *out);*out=native;return 1;}
static int apply(unsigned deck,unsigned on){assert(deck>=1&&deck<=4&&on<=1);requests++;last_deck=deck;last_on=on;return 1;}
static int led(unsigned side,unsigned id,uint32_t rgb,uint32_t hold){assert(side<2);if(!hold)led_released++;if(id==OQMOD_LED_INNER_CIRCLE&&rgb==0xff0000)led_red++;return 1;}
static uint64_t now(void){return clock_ms;}
static int bind(const char *s){return !strcmp(s,"slip-mode");}
static int get(const char *p,int fallback){if(!strcmp(p,"protocol"))return 2;if(!strcmp(p,"engine_external"))return 1;return fallback;}
static int set(const char *p,int v){assert(!strcmp(p,"enabled")&&(v==0||v==1));return 1;}
static int setting(const char *s,int v){(void)s;return v;}
static void write_setting(const char *s,int v){(void)s;(void)v;assert(0);}
static int yes(void){return 1;}
static int background(int allow){return allow==1;}
static void number(int n){(void)n;}
static void text(const char *s){(void)s;}
static int choices(const char *k,const char *l,const char *const *v,unsigned n,int f){(void)k;(void)l;(void)v;(void)n;return f;}
int main(void){
    struct oqmod_api a={.version=1,.size=sizeof a,.native_bind=bind,.native_get=get,.native_set=set,.native_engine=yes,
        .background_tick=background,.object_deck=object_deck,.deck_slip_read=snapshot,.deck_slip_set=apply,.now_ms=now,.led_set=led,
        .ep136_ok=yes,.hook=hook,.setting_get=setting,.setting_set=write_setting,.menu_order=number,.default_enabled=number,.choice_buttons=choices,.log=text,.set_status=text};
    struct oqmod_info info={0};assert(!oqmod_init(&a,&info)&&edge);info.enabled(1);info.tick();
    edge(objects,0,NULL);edge(objects,0,NULL);info.tick();assert(requests==1&&last_deck==1&&last_on==1&&!originals);
    native.on=1;clock_ms+=100;info.tick();assert(slip_pending==1); /* native/input need agreement */
    native.input_on=1;clock_ms=1250;info.tick();assert(!slip_pending&&slip_owned==1&&led_red);
    /* Disable while held: release remains owned; already-owned state is closed
     * by the background callback even with no settings USB available. */
    info.enabled(0);edge(objects,1,NULL);assert(!originals&&led_released);clock_ms+=100;info.tick();
    assert(requests==2&&last_deck==1&&!last_on);
    native.on=native.input_on=0;clock_ms+=100;info.tick();assert(!slip_owned&&!slip_pending);
    unsigned before=requests;clock_ms+=100;info.tick();assert(requests==before);
    edge(objects,0,NULL);edge(objects,1,NULL);assert(originals==2); /* stock reverse */
    info.enabled(1);native.selected[0]=3;edge(objects+2,0,NULL);edge(objects+2,1,NULL);info.tick();assert(last_deck==3&&last_on==1);
    clock_ms+=2100;native.ready=0;info.tick();assert(slip_errors&4);
    unsigned foreign;edge(&foreign,0,NULL);assert(originals==3);
    puts("PASS: USB Slip swap, held-release ownership, per-deck requests, native/input acknowledgement, red/centre/reverse LEDs and disabled/USB cleanup");
}

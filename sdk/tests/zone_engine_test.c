#include <assert.h>
#include "../mods/zone-continuous/mod.c"
static unsigned char loaders[5][64];static uint32_t lengths[4];
static uint64_t clock_ms=100;static unsigned plays[4],calls;static int routing_on=1,routing_assign=0,hook_ok=1,play_ok=1;
static int (*installed)(void *);
static int native_try(void *p){calls++;return ((unsigned char *)p)[40];}
static unsigned object(unsigned kind,const void *p){if(kind!=1)return 0;for(unsigned d=0;d<4;d++)if(p==loaders[d])return d+1;return 0;}
static int hook(uintptr_t at,uint32_t word,void *rep,void **orig){assert(at==0x7b5a70&&word==0xa9bc7bfd);if(!hook_ok)return 0;installed=rep;*orig=(void *)native_try;return 1;}
static uint64_t now(void){return clock_ms;}
static int deck(unsigned d,struct oqmod_deck *out){assert(d>=1&&d<=4);memset(out,0,sizeof *out);out->length_ms=lengths[d-1];return 1;}
static int play(unsigned d){assert(d>=3&&d<=4);plays[d-1]++;return play_ok;}
static int yes(void){return 1;}
static int bind(const char *s){return !strcmp(s,"zone-continuous");}
static int get(const char *p,int fallback){if(!strcmp(p,"protocol"))return 2;if(!strcmp(p,"engine_external"))return 1;if(!strcmp(p,"zone_on"))return routing_on;if(!strcmp(p,"zone_assign"))return routing_assign;return fallback;}
static int set(const char *p,int v){assert(!strcmp(p,"enabled")&&(v==0||v==1));return 1;}
static int setting(const char *s,int f){(void)s;return f;}
static void write_setting(const char *s,int n){(void)s;(void)n;assert(0);}
static void number(int n){(void)n;}
static void text(const char *s){(void)s;}
static int choices(const char *k,const char *l,const char *const *v,unsigned n,int f){(void)k;(void)l;(void)v;(void)n;return f;}
int main(void){
    struct oqmod_api a={.version=1,.size=sizeof a,.native_bind=bind,.native_get=get,.native_set=set,
        .native_engine=yes,.ep136_ok=yes,.hook=hook,.object_deck=object,.deck=deck,.deck_play=play,.now_ms=now,
        .menu_order=number,.default_enabled=number,.choice_buttons=choices,.setting_get=setting,.setting_set=write_setting,.log=text,.set_status=text};
    struct oqmod_info info={0};assert(!oqmod_init(&a,&info)&&installed);
    info.enabled(1);info.tick();
    for(int on=-1;on<=1;on++)for(int assign=-1;assign<=3;assign++)for(unsigned d=0;d<5;d++)for(unsigned flag=0;flag<2;flag++){
        routing_on=on;routing_assign=assign;info.tick();loaders[d][40]=(unsigned char)flag;
        int active=on==1&&(assign==0||assign==1)&&d==(unsigned)assign+2;
        assert(installed(loaders[d])==(int)(active?1:flag));assert(loaders[d][40]==flag);
    }
    routing_on=1;routing_assign=0;info.enabled(0);info.enabled(1);info.tick();
    loaders[2][40]=0;lengths[2]=1000;assert(installed(loaders[2])==1);
    clock_ms+=499;lengths[2]=2000;info.tick();assert(!plays[2]);clock_ms++;info.tick();assert(plays[2]==1);
    info.tick();assert(plays[2]==1); /* exactly once */
    installed(loaders[2]);clock_ms+=2999;info.tick();assert(plays[2]==1);clock_ms++;info.tick();assert(plays[2]==2);
    installed(loaders[2]);info.enabled(0);info.enabled(1);clock_ms+=4000;info.tick();assert(plays[2]==2);
    installed(loaders[2]);routing_assign=1;clock_ms+=4000;info.tick();assert(plays[2]==2);
    play_ok=0;installed(loaders[3]);clock_ms+=3000;info.tick();assert(plays[3]==1&&zone_refused==1);
    info.tick();assert(plays[3]==1); /* failed native play is not spammed */
    hook_ok=0;info.enabled(0);assert(!oqmod_init(&a,&info));info.enabled(1);info.tick();
    assert(!zone_enabled); /* unavailable hooks cannot enable policy */
    a.object_deck=NULL;assert(oqmod_init(&a,&info));
    puts("PASS: USB ZONE engine routing matrix, flag restoration, 500ms readiness/3s fallback, single PLAY, disable/reassign cancellation and hook refusal");
}

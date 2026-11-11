#include <assert.h>
#include <string.h>
#include "../mods/smooth-waveforms/mod.c"
static struct oqmod_render_provider provider;static int preference=3;
static int get(const char *s,int fallback){if(!strcmp(s,"protocol"))return 2;if(!strcmp(s,"engine_external"))return 1;return fallback;}
static int bind(const char *s){return !strcmp(s,"smooth-waveforms");}
static int yes(void){return 1;}
static int register_provider(const struct oqmod_render_provider *p){provider=*p;return 1;}
static int setting(const char *s,int fallback){return !strcmp(s,"mode")?preference:fallback;}
static void write_setting(const char *s,int v){assert(!strcmp(s,"mode"));preference=v;}
static int set(const char *s,int v){(void)s;(void)v;return 1;}
static void number(int n){(void)n;}
static void text(const char *s){(void)s;}
static int choices(const char *k,const char *l,const char *const *v,unsigned n,int f){(void)k;(void)l;(void)v;assert(n==4);return f;}
int main(void){
    struct oqmod_api api={.version=1,.size=sizeof api,.native_bind=bind,.native_get=get,.native_set=set,.native_engine=yes,
        .render_provider=register_provider,.setting_get=setting,.setting_set=write_setting,.menu_order=number,.default_enabled=number,.choice_buttons=choices,.log=text};
    struct oqmod_info info={0};assert(!oqmod_init(&api,&info));info.enabled(1);info.tick();assert(rt_mode==RT_MODE_SMOOTH_30);
    _Alignas(8) unsigned char storage[200]={0};void *timer=storage+8;
    *(uintptr_t *)timer=RT_MAIN_VT;*(double *)((char *)timer+0x50)=16.88;
    assert(provider.period(timer,33,0,1000000000)==33);
    preference=2;info.tick();assert(provider.period(timer,33,0,1000000000)==16);
    assert(*(double *)((char *)timer+0x50)==8.44);provider.period(timer,8,0,1000000000);assert(*(double *)((char *)timer+0x50)==8.44);
    info.enabled(0);assert(provider.period(timer,33,0,1000000000)==33&&*(double *)((char *)timer+0x50)==16.88);
    info.enabled(1);preference=3;info.tick();
    *(double *)(storage+0x80)=100.0;
    struct oqmod_paint_context context={0};provider.begin(&context,105000000);
    *(double *)(storage+0x80)=*(double *)(storage+0x70)=110.0;provider.end(&context);
    assert(*(double *)(storage+0x80)==105.0);
    /* Native class allowlist, not a blanket 33 -> 16 transform. */
    preference=2;info.tick();
    const uintptr_t classes[]={RT_PLAYER_VT,RT_TASKBOX_VT,RT_REPAINT_VT,0x1234};
    for(unsigned i=0;i<4;i++){
        *(uintptr_t *)timer=classes[i];
        assert(provider.period(timer,33,0,1000000000)==(i<3?16:33));
        assert(provider.period(timer,100,0,1000000000)==100&&provider.period(timer,-1,0,1000000000)==-1);
    }
    *(uintptr_t *)timer=RT_REPAINT_VT;*(int *)((char *)timer+0x10)=1;
    provider.period(timer,33,0,1000000000);assert(provider.period(timer,30,RT_NEXTSTEP_LR,1001000000)==(-2147483647-1));
    info.enabled(0);provider.begin(&context,105000000);assert(!context.instance);
    api.render_provider=NULL;assert(oqmod_init(&api,&info));
    puts("PASS: USB render policy: Native Height 30 default, exact timer allowlist, fast-clock restoration, paint compensation and disable pass-through");
}

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static int bad_signature;
static uint32_t word(uintptr_t address){
    if(bad_signature)return 0;
    if(address==0x1ce8f78)return 0x3941c802u;
    if(address==0x1ce8f88)return 0x3941cc02u;
    if(address==0x1ce90a4)return 0x3901c802u;
    if(address==0x1ce90a8)return 0x3901cc15u;
    return address==0x1ce8f70?0xa9bc7bfdu:address==0x1d0e8c8?0xf9401000u:0x97fffd63u;
}
#define OQ_FX_WORD(address) word(address)
#include "../mods/fx-frequency/mod.c"
static unsigned original_calls,feedback_calls,hook_calls,fail_hook,paint_calls,setting_writes,revision;
static unsigned surface_visible=1,external_cap=1;static int preference=2,native_selection=3;
static uint64_t clock_ms=1000;
static unsigned panels;
static void (*draw_panel)(struct oqmod_canvas *,unsigned,int,int);
static int (*touch_panel)(unsigned,int,int);
static int renderer(void (*d)(struct oqmod_canvas *,unsigned,int,int),int (*t)(unsigned,int,int)){draw_panel=d;touch_panel=t;return 1;}
static int set_panels(const struct oqmod_panel *p,unsigned n){
    assert(n<=1);if(n)assert(p&&p->x==1090&&p->y==306&&p->w==180&&p->h==66&&p->flags==OQMOD_PANEL_TOUCH);panels=n;return 1;
}
static unsigned char native_bytes[9];
static void original_serialize(void *p,unsigned char *out){(void)p;original_calls++;memcpy(out,native_bytes,9);}
static void original_receive(void *p,unsigned on){
    ((unsigned char *)p)[0x72]=1;((unsigned char *)p)[0x73]=(unsigned char)on;feedback_calls++;
}
static int hook(uintptr_t address,uint32_t first,void *replacement,void **original){
    hook_calls++;if(hook_calls==fail_hook)return 0;
    assert(first==word(address));
    if(address==0x1ce8f70){assert(replacement==(void *)fx_receive);*original=(void *)original_receive;}
    else{assert(address==0x1d0e8c8&&replacement==(void *)fx_serialize);*original=(void *)original_serialize;}
    return 1;
}
static int yes(void){return 1;}
static uint64_t now(void){return clock_ms;}
static int bind_service(const char *s){return !strcmp(s,"fx-frequency");}
static int get(const char *p,int fallback){
    if(!strcmp(p,"protocol"))return 2;
    if(!strcmp(p,"engine_external"))return (int)external_cap;
    if(!strcmp(p,"generation"))return 1;
    if(!strcmp(p,"applied.selection"))return (int)revision;
    if(!strcmp(p,"selection"))return native_selection;
    return fallback;
}
static int set(const char *p,int value){if(!strcmp(p,"selection")){native_selection=value;revision++;}return 1;}
static int setting_get(const char *p,int fallback){return !strcmp(p,"selection")?preference:fallback;}
static void setting_set(const char *p,int value){assert(!strcmp(p,"selection"));preference=value;setting_writes++;}
static int choice(const char *k,const char *label,const char *const *names,unsigned n,int value){
    (void)k;(void)label;(void)names;assert(n==3||n==2);return value;
}
static void number(int n){(void)n;}
static void text(const char *s){(void)s;}
static void redraw(void){}
static int surface(unsigned which,struct oqmod_surface *out){
    assert(which==OQMOD_SURFACE_FX_FREQUENCY);
    *out=(struct oqmod_surface){1090,306,180,66,0x1a1a1a,0x343434,0x007de1,0xffffff,0x808080,0x404040};
    return (int)surface_visible;
}
static int use_surface(unsigned which){return which==OQMOD_SURFACE_FX_FREQUENCY;}
static void fill(struct oqmod_canvas *c,int x,int y,int w,int h,uint32_t color){
    (void)c;(void)color;assert(x>=0&&y>=0&&x+w<=180&&y+h<=66);paint_calls++;
}
static void outline(struct oqmod_canvas *c,int x,int y,int w,int h,int thickness,uint32_t color){assert(thickness==1);fill(c,x,y,w,h,color);}
static void label(struct oqmod_canvas *c,int x,int y,int w,int h,const char *s,int pixels,uint32_t color){assert(s&&pixels>0);fill(c,x,y,w,h,color);}
int main(void){
    struct oqmod_api a={.version=1,.size=sizeof a,.ep136_ok=yes,.hook=hook,.now_ms=now,
        .native_bind=bind_service,.native_get=get,.native_set=set,.menu_order=number,.default_enabled=number,
        .choice_buttons=choice,.setting_get=setting_get,.setting_set=setting_set,.log=text,.set_status=text,
        .redraw=redraw,.fill=fill,.outline=outline,.text=label,.surface=surface,.use_surface=use_surface,.native_engine=yes,
        .panel_renderer=renderer,.panels_set=set_panels};
    struct oqmod_info info={0};
    struct oqmod_api old=a;old.size=offsetof(struct oqmod_api,surface);
    assert(oqmod_init(&old,&info)==1&&hook_calls==0);
    external_cap=0;assert(oqmod_init(&a,&info)==1&&hook_calls==0);external_cap=1;
    assert(oqmod_init(&a,&info)==0&&hook_calls==2&&fx_engine.hooks==3);
    assert(!info.w&&!info.h&&draw_panel&&touch_panel);
    assert(!touch(20,30));info.enabled(1);info.tick();
    unsigned before=setting_writes;assert(touch_panel(0,20,30));assert(setting_writes==before); /* touch does no I/O */
    info.tick();assert(preference==0&&native_selection==1&&fx_engine.selection==1);
    assert(!touch(59,30)&&!touch(80,23)&&!touch(180,30)&&!touch(-1,30));
    draw_panel(NULL,0,180,66);assert(paint_calls>0&&panels==1);
    surface_visible=0;info.tick();before=paint_calls;draw_panel(NULL,0,180,66);assert(paint_calls==before&&!panels);
    surface_visible=1;info.tick();assert(panels==1);
    _Alignas(8) unsigned char mixer[0x700]={0},builder[0x28]={0},foreign[0x700]={0};
    *(uintptr_t *)builder=0x299db28;*(uintptr_t *)(builder+0x20)=(uintptr_t)mixer;
    *(uintptr_t *)(mixer+0x520)=*(uintptr_t *)(foreign+0x520)=0x2995bb8;
    memset(native_bytes,0xa7,9);native_bytes[1]&=0xcf;native_bytes[3]=127;
    unsigned char out[9];
    fx_serialize(builder,out);assert(!memcmp(out,native_bytes,9)); /* unknown feedback */
    fx_receive(foreign+0x520,1);fx_serialize(builder,out);assert(!memcmp(out,native_bytes,9));
    fx_receive(mixer+0x520,1);fx_serialize(builder,out);assert(out[3]==255);
    for(int i=0;i<9;i++)if(i!=3)assert(out[i]==native_bytes[i]);
    assert(touch_panel(0,140,30));info.tick();fx_serialize(builder,out);assert(out[3]==0);
    assert(touch_panel(0,140,30));info.tick();fx_serialize(builder,out);assert(!memcmp(out,native_bytes,9)); /* active HIGH -> ALL */
    assert(touch_panel(0,20,30));info.tick();clock_ms+=501;fx_serialize(builder,out);assert(out[3]==255);
    /* No further event notification: sustained ON must still isolate at 60s. */
    clock_ms+=60000;fx_serialize(builder,out);assert(out[3]==255);
    mixer[0x593]=0;fx_serialize(builder,out);assert(!memcmp(out,native_bytes,9));
    mixer[0x593]=1;mixer[0x592]=0;fx_serialize(builder,out);assert(!memcmp(out,native_bytes,9));
    mixer[0x592]=1;mixer[0x593]=2;fx_serialize(builder,out);assert(!memcmp(out,native_bytes,9));
    mixer[0x593]=1;
    fx_receive(mixer+0x520,1);native_bytes[1]|=0x10;fx_serialize(builder,out);assert(!memcmp(out,native_bytes,9));
    native_bytes[1]&=0xcf;info.enabled(0);fx_serialize(builder,out);assert(!memcmp(out,native_bytes,9));
    fx_receive(mixer+0x520,1);info.enabled(1);fx_serialize(builder,out);assert(out[3]==255); /* retained native ON survives resume */
    fx_receive(mixer+0x520,1);fx_serialize(builder,out);assert(out[3]==255);
    unsigned long cases=0;
    for(unsigned en=0;en<2;en++)for(unsigned sel=0;sel<5;sel++)for(unsigned known=0;known<2;known++)
    for(unsigned on=0;on<2;on++)for(unsigned age=0;age<4;age++)for(unsigned mode=0;mode<4;mode++)for(unsigned y=0;y<256;y++){
        const unsigned ages[]={0,500,501,2000};
        unsigned char p[11],saved[11];memset(p,0xa5,sizeof p);p[2]=(p[2]&0xcf)|(mode<<4);p[4]=(unsigned char)y;memcpy(saved,p,11);
        struct fp_state state={en,sel,known,on,1000};unsigned result=fp_apply(&state,1000+ages[age],p+1,9);
        unsigned expected=en&&sel>=1&&sel<=2&&known&&on&&age<=1&&mode==0,endpoint=sel==1?255:0;
        assert((result==FP_CHANGED)==(expected&&y!=endpoint));
        for(int i=0;i<11;i++){
            unsigned wanted=saved[i];
            if(expected&&i==4)wanted=endpoint;
            assert(p[i]==wanted);
        }cases++;
    }
    /* Hook guard and partial install must never permit modification. */
    fx_engine=(struct oq_fx_engine){0};hook_calls=0;bad_signature=1;assert(fx_install(&a)==0&&hook_calls==0);
    bad_signature=0;fail_hook=2;assert(fx_install(&a)==1);fx_engine_set(1,1);
    fx_engine.serialize_original=original_serialize;fx_receive(mixer+0x520,1);fx_serialize(builder,out);assert(!memcmp(out,native_bytes,9));
    assert(original_calls==15&&feedback_calls==6);
    /* Diagnostic capture must be byte-for-byte passive across every range,
     * all native modes and all Y coordinates, even while FX reports ON. */
    fx_engine.hooks=3;fx_engine.capture=1;fx_engine_set(1,1);
    unsigned captured_calls=original_calls;
    for(unsigned sel=1;sel<=3;sel++)for(unsigned mode=0;mode<4;mode++)for(unsigned y=0;y<256;y++){
        fx_engine_set(1,sel);native_bytes[1]=(native_bytes[1]&0xcf)|(mode<<4);native_bytes[3]=(unsigned char)y;
        fx_serialize(builder,out);assert(!memcmp(out,native_bytes,9));
    }
    assert(original_calls-captured_calls==3072&&fx_trace_count==64);
    for(unsigned i=0;i<64;i++)assert(fx_trace[i].ready==1);
    printf("PASS: USB FX engine and UI; %lu packet cases; guards, original forwarding, selection persistence, no touch I/O, popup draw suppression, disable/resume and partial hook safety\n",cases);
}

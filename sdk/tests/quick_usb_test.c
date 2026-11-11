#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/quick-usb/mod.c"
static struct oqmod_usb_snapshot sources;
static struct oqmod_surface slot={410,8,500,40,0x1a1a1a,0x343434,0x007de1,0xffffff,0x808080,0x404040};
static int visible=1,external=1,area_x,area_w,area_h;
static unsigned redraws,draws,bold_calls,open_calls,pink_strips,blue_strips,black_strips,unknown_strips;
static uint64_t opened;static char last_label[64];
static int bind_service(const char *s){return !strcmp(s,"quick-usb");}
static int get(const char *p,int fallback){
    if(!strcmp(p,"protocol"))return 2;
    if(!strcmp(p,"generation"))return 1;
    if(!strcmp(p,"engine_external"))return external;
    return fallback;
}
static int set(const char *p,int n){assert(!strcmp(p,"enabled")&&(n==0||n==1));return 1;}
static int setting_get(const char *k,int fallback){(void)k;return fallback;}
static void setting_set(const char *k,int n){(void)k;(void)n;assert(0);}
static void number(int n){(void)n;}
static int engine(void){return 1;}
static void text(const char *s){(void)s;}
static int choices(const char *k,const char *l,const char *const *v,unsigned count,int fallback){(void)k;(void)l;(void)v;(void)count;return fallback;}
static void redraw(void){redraws++;}
static int surface(unsigned id,struct oqmod_surface *s){assert(id==OQMOD_SURFACE_USB_SHORTCUTS);*s=slot;return visible;}
static int use_surface(unsigned id){return id==OQMOD_SURFACE_USB_SHORTCUTS;}
static int snapshot(struct oqmod_usb_snapshot *out){assert(out->size==sizeof *out);*out=sources;return 1;}
static int open_usb(uint64_t key,uint32_t gen){
    open_calls++;if(gen!=sources.generation)return 0;
    for(unsigned i=0;i<sources.count;i++)if(sources.sources[i].key==key){opened=key;return 1;}
    return 0;
}
static int area(int x,int y,int w,int h){assert(y==8&&w>=0&&x>=slot.x&&x+w<=slot.x+slot.w);area_x=x;area_w=w;area_h=h;return 1;}
static void fill(struct oqmod_canvas *c,int x,int y,int w,int h,uint32_t rgb){
    (void)c;assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=area_w&&y+h<=area_h);draws++;
    if(rgb==0xd60090&&w==6)pink_strips++;
    if(rgb==0x0000ff&&w==6)blue_strips++;
    if(rgb==0&&w==6)black_strips++;
    if(rgb==0x25dac9&&w==6)unknown_strips++;
}
static void outline(struct oqmod_canvas *c,int x,int y,int w,int h,int thickness,uint32_t rgb){assert(thickness==1||thickness==3);fill(c,x,y,w,h,rgb);}
static void label(struct oqmod_canvas *c,int x,int y,int w,int h,const char *s,int px,uint32_t rgb){
    assert(px==15||px==17);snprintf(last_label,sizeof last_label,"%s",s);fill(c,x,y,w,h,rgb);
}
static void weighted(struct oqmod_canvas *c,int x,int y,int w,int h,const char *s,int px,int bold,uint32_t rgb){assert(bold==1);bold_calls++;label(c,x,y,w,h,s,px,rgb);}
int main(void){
    struct oqmod_api api={.version=1,.size=sizeof api,.native_bind=bind_service,.native_get=get,.native_set=set,
        .setting_get=setting_get,.setting_set=setting_set,.menu_order=number,.default_enabled=number,
        .choice_buttons=choices,.log=text,.set_status=text,.redraw=redraw,.surface=surface,.use_surface=use_surface,
        .usb_sources=snapshot,.usb_open=open_usb,.set_area=area,.fill=fill,.outline=outline,.text=label,.text_weight=weighted,.native_engine=engine};
    struct oqmod_info info={0};struct oqmod_api old=api;old.size=offsetof(struct oqmod_api,usb_sources);
    assert(oqmod_init(&old,&info));external=0;assert(oqmod_init(&api,&info));external=1;
    assert(!oqmod_init(&api,&info)&&info.draw&&info.touch&&info.tick);
    sources.size=sizeof sources;sources.count=2;sources.generation=1;
    sources.sources[0]=(struct oqmod_usb_source){.key=0x12200000001ull,.port=1,.color=0xffd60090,.name="ROBSMUSIC"};
    sources.sources[1]=(struct oqmod_usb_source){.key=0x16200000001ull,.port=3,.color=0xff0000ff,.name="TESTUSB"};
    sources.active=sources.sources[0].key;info.enabled(1);info.tick();assert(area_x==410&&area_w==500);
    info.draw(NULL,area_w,40);assert(bold_calls==1&&pink_strips==1);
    assert(!info.touch(247,20)&&!info.touch(500,20)&&!info.touch(-1,20)&&!info.touch(15,40));
    assert(info.touch(260,20)&&open_calls==0);info.tick();assert(opened==sources.sources[1].key&&open_calls==1);
    unsigned before=redraws;info.tick();assert(redraws==before); /* no redraw when unchanged */
    sources.active=sources.sources[1].key;info.tick();info.draw(NULL,area_w,40);assert(blue_strips==1);
    /* A newer list is not what the user saw: retain painted key+generation and
     * reject it at the API rather than retargeting the same button index. */
    uint64_t old_key=opened;sources.generation++;sources.sources[1].key=0x14200000001ull;
    info.tick();assert(info.touch(260,20));info.tick();assert(opened==old_key&&open_calls==2);
    info.draw(NULL,area_w,40);assert(info.touch(260,20));info.tick();assert(opened==sources.sources[1].key);
    slot.x=574;slot.w=336;sources.count=3;sources.sources[2]=(struct oqmod_usb_source){.key=100,.port=2,.name="THIRD"};sources.generation++;
    info.tick();assert(area_x==574&&area_w==336);info.draw(NULL,area_w,40);
    slot.w=221;info.tick();assert(area_w==219);info.draw(NULL,area_w,40);
    sources.count=1;sources.generation++;slot.x=410;slot.w=500;info.tick();assert(area_w==250);
    info.draw(NULL,area_w,40);assert(strstr(last_label,"1 ROBSMUSIC"));
    sources.active=sources.sources[0].key;sources.sources[0].color=0xff000000;sources.generation++;
    info.tick();info.draw(NULL,area_w,40);assert(black_strips==1);
    sources.sources[0].color=0;sources.generation++;info.tick();info.draw(NULL,area_w,40);assert(unknown_strips==1);
    struct oqmod_usb_source utf={.port=1,.name="éééééééééééééééééé"};char title[64];quick_title(title,&utf,100);
    assert(strstr(title,"...")&&strlen(title)<25);
    memcpy(utf.name,"A\xf0\x80\x80\x80",6);quick_title(title,&utf,250);assert(!strcmp(title,"1 A..."));
    visible=0;before=draws;info.draw(NULL,area_w,40);assert(draws==before&&!info.touch(20,20));info.tick();assert(area_w==0);
    visible=1;info.tick();info.draw(NULL,area_w,40);assert(info.touch(20,20));before=open_calls;
    info.enabled(0);info.enabled(1);info.tick();assert(open_calls==before&&!info.touch(20,20)); /* stale frame/request cleared */
    info.draw(NULL,area_w,40);info.enabled(0);assert(!info.touch(20,20));
    for(int width=0;width<=500;width++)for(unsigned count=0;count<=4;count++){
        int button=quick_width(width,count);assert(button>=0&&(!button||(button>=64&&button<=250&&button*(int)count<=width)));
    }
    puts("PASS: external Quick USB layout/UTF-8/colours/active state, 250px single-drive cap, FPS/STEMS geometry, no touch navigation/I-O, painted generation identity, popup/disabled guards and redraw suppression");
}

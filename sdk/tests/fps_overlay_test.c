#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/fps-overlay/mod.c"
struct oqmod_canvas {int marker;};
static int metric=299,theme,redraws,draws,mutations;
static uint32_t accent;static char label[32];
static int bind_service(const char *s){return !strcmp(s,"fps-overlay");}
static int read_property(const char *p,int fallback){
    if(!strcmp(p,"protocol"))return 2;
    if(!strcmp(p,"initial.enabled"))return 1;
    if(!strcmp(p,"waveform_fps_x10"))return metric;
    if(!strcmp(p,"light_theme"))return theme;
    return fallback;
}
static int write_property(const char *p,int v){(void)p;(void)v;mutations++;return 0;}
static void order(int v){assert(v==200);}
static void defaults(int v){assert(v==1);}
static void redraw(void){redraws++;}
static void filled(struct oqmod_canvas *c,int x,int y,int w,int h,uint32_t rgb){
    assert(c&&x==0&&y==0&&w==96&&h==25&&rgb==0x1a1a1a);draws++;
}
static void outlined(struct oqmod_canvas *c,int x,int y,int w,int h,int t,uint32_t rgb){
    assert(c&&x==0&&y==0&&w==96&&h==25&&t==1);accent=rgb;draws++;
}
static void text(struct oqmod_canvas *c,int x,int y,int w,int h,const char *s,int px,uint32_t rgb){
    assert(c&&x==0&&y==0&&w==96&&h==25&&px==13&&rgb==0xffffff);
    snprintf(label,sizeof label,"%s",s);draws++;
}
int main(void){
    struct oqmod_api a={.version=OQMOD_API_VERSION,.size=sizeof a,.native_bind=bind_service,.native_get=read_property,
        .native_set=write_property,.menu_order=order,.default_enabled=defaults,.redraw=redraw,.fill=filled,.outline=outlined,.text=text};
    struct oqmod_info info={0};struct oqmod_canvas canvas={0};
    assert(!oqmod_init(&a,&info)&&info.x==470&&info.y==8&&info.w==96&&info.h==25);
    info.enabled(1);info.tick();int n=redraws;info.draw(&canvas,96,25);
    assert(!strcmp(label,"FPS 29.9")&&accent==0x25dac9&&draws==3&&!mutations);
    info.tick();assert(redraws==n);theme=1;info.tick();assert(redraws==n+1);
    info.draw(&canvas,96,25);assert(accent==0x087d7a);
    metric=20000;info.tick();info.draw(&canvas,96,25);assert(!strcmp(label,"FPS --"));
    info.enabled(0);n=redraws;metric=600;info.tick();assert(redraws==n&&!mutations);
    puts("PASS: FPS rendering entirely in the add-on: bounds, formatting, invalid-rate guard, both theme accents, redraw-on-change, no native writes/hooks");
}

/* Standalone FPS badge: native core supplies only read-only measurements.
 * All formatting, colours, redraw policy, geometry and drawing are in this mod.
 * No native patch addresses, firmware assets, or core built-in badge are used. */
#include <stdio.h>
#include "oqmod.h"
static const struct oqmod_api *api;
static int on,rate,light,last_rate=-1,last_light=-1;
static void tick(void){
    if(!on)return;
    int value=api->native_get("waveform_fps_x10",0);
    int new_rate=value>=0&&value<=10000?value:0,new_light=api->native_get("light_theme",0)!=0;
    __atomic_store_n(&rate,new_rate,__ATOMIC_RELEASE);__atomic_store_n(&light,new_light,__ATOMIC_RELEASE);
    if(new_rate!=last_rate||new_light!=last_light){last_rate=new_rate;last_light=new_light;api->redraw();}
}
static void draw(struct oqmod_canvas *canvas,int width,int height){
    char text[24];
    int value=__atomic_load_n(&rate,__ATOMIC_ACQUIRE),is_light=__atomic_load_n(&light,__ATOMIC_ACQUIRE);
    if(value>0)snprintf(text,sizeof text,"FPS %d.%d",value/10,value%10);
    else snprintf(text,sizeof text,"FPS --");
    api->fill(canvas,0,0,width,height,0x1a1a1a);
    api->outline(canvas,0,0,width,height,1,is_light?0x087d7a:0x25dac9);
    api->text(canvas,0,0,width,height,text,13,0xffffff);
}
static void enabled(int yes){on=!!yes;last_rate=last_light=-1;api->redraw();}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->version!=OQMOD_API_VERSION||a->size<sizeof *a||!a->native_bind||!a->native_get||
       !a->menu_order||!a->default_enabled||!a->redraw||!a->fill||!a->outline||!a->text||
       !a->native_bind("fps-overlay")||a->native_get("protocol",0)<2)return 1;
    api=a;api->menu_order(200);api->default_enabled(api->native_get("initial.enabled",0));
    info->api_version=OQMOD_API_VERSION;info->name="FPS Overlay";
    info->description="Measured native waveform repaint rate, not LCD scanout.\nThe badge is drawn entirely by this USB add-on.";info->version="1.0";
    info->x=470;info->y=8;info->w=96;info->h=25;
    info->tick=tick;info->draw=draw;info->enabled=enabled;return 0;
}

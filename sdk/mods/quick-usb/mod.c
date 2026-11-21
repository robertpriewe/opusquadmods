/* USB-owned shortcut rendering/navigation. Shared SDK provides native source
 * snapshots and a queued open-source action, never a track-loading operation. */
#include "oqmod_native_feature.h"
#include <pthread.h>
#include <string.h>
static const struct oq_native_option oq_feature_options[]={
};
static const struct oq_native_feature oq_feature={"quick-usb","Quick USB Access","Named, coloured USB shortcuts above the waveforms.\nTap a drive to open its native browser. No track is loaded.", 40};
#define OQ_NATIVE_CUSTOM_ENTRY
#define OQ_NATIVE_FEATURE_IMPLEMENTATION
#include "oqmod_native_feature.h"

struct quick_view {struct oqmod_usb_snapshot_link usb;struct oqmod_surface surface;int button_width,width,valid;unsigned shown;};
static pthread_mutex_t quick_lock=PTHREAD_MUTEX_INITIALIZER;
static struct quick_view quick_view,quick_painted;
static uint64_t quick_request;static uint32_t quick_request_generation;
static int quick_on,quick_link_missing;
/* R124: with LINK USBs there can be more drives than fit; show the first that do
 * (the OPUS's own USBs come first). */
static unsigned quick_fit(int available,unsigned count){
    if(available<=0)return 0;
    unsigned most=(unsigned)(available/64);
    return count<most?count:most;
}
static int quick_width(int available,unsigned count){
    if(!count||count>OQMOD_USB_MAX_LINK||available<=0)return 0;
    int width=available/(int)count;if(width>250)width=250;
    return width>=64?width:0;
}
static int quick_active(uint64_t active,uint64_t key){
    return active&&((active&0x0000ffffffffffffull)==(key&0x0000ffffffffffffull));
}
static void quick_title(char title[64],const struct oqmod_usb_source *usb,int width){
    char prefix[16];
    if(oqmod_usb_is_link(usb->key))snprintf(prefix,sizeof prefix,width>=150?"LINK%u ":"L%u ",usb->port);
    else snprintf(prefix,sizeof prefix,"%u ",usb->port);
    unsigned used=(unsigned)strlen(prefix);
    unsigned limit=width>40?(unsigned)(width-40)/10:0;limit=limit>used-2?limit-(used-2):0;if(limit>30)limit=30;
    const unsigned char *name=(const unsigned char *)usb->name;size_t bytes=0;unsigned chars=0;
    while(bytes<sizeof usb->name-1&&name[bytes]&&chars<limit){
        unsigned char c=name[bytes];size_t step;
        if(c>=32&&c<127)step=1;
        else if(c>=0xc2&&c<=0xdf)step=2;
        else if(c>=0xe0&&c<=0xef)step=3;
        else if(c>=0xf0&&c<=0xf4)step=4;
        else break;
        if(bytes+step>=sizeof usb->name)break;
        size_t j=1;for(;j<step;j++)if((name[bytes+j]&0xc0)!=0x80)break;
        if(j<step)break;
        if(step==3&&((c==0xe0&&name[bytes+1]<0xa0)||(c==0xed&&name[bytes+1]>=0xa0)))break;
        if(step==4&&((c==0xf0&&name[bytes+1]<0x90)||(c==0xf4&&name[bytes+1]>=0x90)))break;
        bytes+=step;chars++;
    }
    snprintf(title,64,"%s%.*s%s",prefix,(int)bytes,usb->name,name[bytes]?"...":"");
}
static void tick(void){
    if(!__atomic_load_n(&quick_on,__ATOMIC_ACQUIRE))return;
    uint64_t request;uint32_t generation;
    pthread_mutex_lock(&quick_lock);request=quick_request;generation=quick_request_generation;quick_request=0;pthread_mutex_unlock(&quick_lock);
    if(request)oq_feature_api->set_status(oq_feature_api->usb_open(request,generation)?
        "USB browser requested; no track loaded.":"Source changed or unavailable; select the drive again.");
    oq_feature_tick();
    struct quick_view next={0};next.usb.size=sizeof next.usb;
    int want_link=oq_feature_api->setting_get("link",0)==1;
    next.usb.reserved=want_link?OQMOD_USB_WANT_LINK:0;
    int visible=oq_feature_api->surface(OQMOD_SURFACE_USB_SHORTCUTS,&next.surface);
    if(!oq_feature_api->usb_sources((struct oqmod_usb_snapshot *)&next.usb))next.usb.count=0;
    unsigned cap=next.usb.reserved&OQMOD_USB_WANT_LINK?OQMOD_USB_MAX_LINK:OQMOD_USB_MAX;
    if(next.usb.count>cap)next.usb.count=0;
    int missing=want_link&&!(next.usb.reserved&OQMOD_USB_WANT_LINK);
    if(missing!=quick_link_missing){quick_link_missing=missing;
        oq_feature_api->set_status(missing?"LINK USBs need firmware R124 or later.":"");}
    next.shown=visible?quick_fit(next.surface.w,next.usb.count):0;
    next.button_width=quick_width(next.surface.w,next.shown);
    next.width=next.button_width*(int)next.shown;next.valid=next.width>0;
    pthread_mutex_lock(&quick_lock);int changed=memcmp(&quick_view,&next,sizeof next)!=0;quick_view=next;pthread_mutex_unlock(&quick_lock);
    if(changed){
        oq_feature_api->set_area(next.surface.x,next.surface.y,next.width,40);
        oq_feature_api->redraw();
    }
}
static void enabled(int on){
    __atomic_store_n(&quick_on,!!on,__ATOMIC_RELEASE);
    pthread_mutex_lock(&quick_lock);quick_request=0;quick_painted.valid=0;memset(&quick_view,0,sizeof quick_view);pthread_mutex_unlock(&quick_lock);
    oq_feature_enabled(on);oq_feature_api->redraw();
}
static void draw(struct oqmod_canvas *c,int width,int height){
    if(!__atomic_load_n(&quick_on,__ATOMIC_ACQUIRE))return;
    struct quick_view view;pthread_mutex_lock(&quick_lock);view=quick_view;pthread_mutex_unlock(&quick_lock);
    struct oqmod_surface live;if(!oq_feature_api->surface(OQMOD_SURFACE_USB_SHORTCUTS,&live)||!view.valid||
       width!=view.width||height!=40||live.x!=view.surface.x||live.y!=view.surface.y||width>live.w)return;
    oq_feature_api->fill(c,0,0,width,height,live.background); /* own the gaps, too */
    for(unsigned i=0;i<view.shown;i++){
        const struct oqmod_usb_source *usb=&view.usb.sources[i];int x=(int)i*view.button_width;
        char title[64];quick_title(title,usb,view.button_width);
        uint32_t color=usb->color?usb->color&0xffffffu:0x25dac9;
        if(quick_active(view.usb.active,usb->key)){
            uint32_t fill=((color>>1)&0x7f7f7f)+((color>>2)&0x3f3f3f);
            oq_feature_api->fill(c,x,0,view.button_width-5,40,fill);
            oq_feature_api->outline(c,x,0,view.button_width-5,40,3,live.text);
            oq_feature_api->fill(c,x+3,3,6,34,color);
            oq_feature_api->text_weight(c,x+15,0,view.button_width-25,40,title,17,1,live.text);
        }else{
            uint32_t dim=(color>>1)&0x7f7f7f;
            oq_feature_api->fill(c,x,0,view.button_width-5,40,live.background);
            oq_feature_api->outline(c,x,0,view.button_width-5,40,1,dim);
            oq_feature_api->fill(c,x+2,2,4,36,dim);
            oq_feature_api->text(c,x+10,0,view.button_width-22,40,title,15,live.dim);
        }
    }
    /* Touch refers to the frame actually drawn, not a newer list at the same
     * indices. The host rejects its generation if the media changed meanwhile. */
    pthread_mutex_lock(&quick_lock);quick_painted=view;pthread_mutex_unlock(&quick_lock);
}
static int touch(int x,int y){
    if(!__atomic_load_n(&quick_on,__ATOMIC_ACQUIRE)||x<0||y<0||y>=40)return 0;
    struct oqmod_surface live;if(!oq_feature_api->surface(OQMOD_SURFACE_USB_SHORTCUTS,&live))return 0;
    pthread_mutex_lock(&quick_lock);const struct quick_view *view=&quick_painted;
    int valid=view->valid&&view->button_width>0&&x<view->width&&x%view->button_width<view->button_width-5&&
        live.x==view->surface.x&&live.y==view->surface.y&&view->width<=live.w;
    if(valid){
        unsigned i=(unsigned)(x/view->button_width);
        if(i>=view->shown)valid=0;
        else{quick_request=view->usb.sources[i].key;quick_request_generation=view->usb.generation;}
    }
    pthread_mutex_unlock(&quick_lock);return valid;
}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->size<sizeof *a||!a->surface||!a->use_surface||!a->usb_sources||!a->usb_open||!a->set_area||
       !a->fill||!a->outline||!a->text||!a->text_weight||!a->redraw||!a->set_status||!a->native_get||!a->native_engine)return 1;
    if(oq_feature_init(a,info)||!a->native_get("engine_external",0)||!a->native_engine()||!a->use_surface(OQMOD_SURFACE_USB_SHORTCUTS))return 1;
    a->option("link","Show LINK USBs",0);
    info->x=410;info->y=8;info->w=0;info->h=40;info->version="2.1";
    info->tick=tick;info->enabled=enabled;info->draw=draw;info->touch=touch;return 0;
}

/* USB-owned title visibility, eye drawing, touch policy and stock side-text masks. */
#include "oqmod_native_feature.h"
#include <math.h>
#include <pthread.h>
#include <string.h>
static const struct oq_native_option oq_feature_options[]={};
static const struct oq_native_feature oq_feature={"hide-titles","Hide Track Names","Eye controls on the 2 DECK overviews hide title and artist\non the main and side screens. Tap again to reveal them.", 120};
#define OQ_NATIVE_CUSTOM_ENTRY
#define OQ_NATIVE_FEATURE_IMPLEMENTATION
#include "oqmod_native_feature.h"
#define MM_EYES 8
#define COMP_PARENT 0x18
#define COMP_BOUNDS 0x20
#define COMP_FLAGS 0xc0
#define WFV_PREVIEWS 832
struct mm_eye {int x,y,w,h;};
static unsigned ht_deck_hidden,ht_side_blanked,ht_layout,ls_selected[2],ht_pending;
static pthread_mutex_t ht_ui_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned ht_ui_hidden;
static unsigned mm_take_eye_toggles(void){return __atomic_exchange_n(&ht_pending,0,__ATOMIC_ACQ_REL);}
static int ptr_ok(const void *p){return p&&!((uintptr_t)p&7)&&(uintptr_t)p>0x10000;}
static char *as_component(void *p){return (char *)p+*(const long *)(*(uintptr_t *)p-24);}
static int comp_visible_now(const char *p){return (*(const unsigned *)(p+COMP_FLAGS)>>1)&1;}
static void comp_visible(char *p,int on){if(comp_visible_now(p)!=on)(*(void (***)(void *,int))p)[12](p,on);}
static const char *rtti_name(const void *p){
    if(!ptr_ok(p))return NULL;
    uintptr_t v=*(const uintptr_t *)p;if(v<0x2000000||v>0x3600000||(v&7))return NULL;
    uintptr_t ti=*(const uintptr_t *)(v-8);if(ti<0x2000000||ti>0x3600000)return NULL;
    return *(const char *const *)(ti+8);
}
static int patch_slot(uintptr_t at,void *replacement,void **original){return oq_feature_api->patch(at,*(uintptr_t *)at,replacement,original);}
static int dvh_deck_name(unsigned d,char *s,unsigned n){return oq_feature_api->deck_text(d+1,0,s,n);}
static int dvh_deck_artist(unsigned d,char *s,unsigned n){return oq_feature_api->deck_text(d+1,1,s,n);}
static void mm_publish_eyes(const struct mm_eye *eyes,unsigned hidden){
    struct oqmod_panel panels[OQMOD_PANELS_MAX]={0};
    pthread_mutex_lock(&ht_ui_lock);ht_ui_hidden=hidden;pthread_mutex_unlock(&ht_ui_lock);
    for(unsigned i=0;i<MM_EYES;i++){
        const struct mm_eye *e=&eyes[i];
        if(e->x>=0&&e->y>=0&&e->w>0&&e->h>0&&e->x<=1280-e->w&&e->y<=800-e->h)
            panels[i]=(struct oqmod_panel){e->x,e->y,e->w,e->h,OQMOD_PANEL_TOUCH,6,8};
    }
    for(unsigned s=0;s<2;s++){
        const struct mm_eye *e=&eyes[6+s];int x=e->x+e->w+4,y=e->y-(30-e->h)/2;
        if(e->w>0&&(hidden>>(6+s)&1)&&x>=0&&y>=0&&x<=1280-540&&y<=800-66)
            panels[8+s]=(struct oqmod_panel){x,y,540,66,0,0,0};
    }
    oq_feature_api->panels_set(panels,OQMOD_PANELS_MAX);
}
#include "engine.h"
static void title_draw(struct oqmod_canvas *c,unsigned slot,int w,int h){
    pthread_mutex_lock(&ht_ui_lock);unsigned hidden=ht_ui_hidden;pthread_mutex_unlock(&ht_ui_lock);
    if(slot>=8){oq_feature_api->fill(c,0,0,w,h,0x101010);oq_feature_api->text(c,0,0,w,h,"TRACK HIDDEN",13,0x808080);return;}
    unsigned ink=(hidden>>slot&1)?0xffffff:0x808080;
    oq_feature_api->fill(c,0,0,w,h,0x1a1a1a);
    int cx=w/2,cy=h/2,a=w/2-3,b=h/2-4;if(a<1||b<1)return;
    for(int dx=-a;dx<=a;dx++){
        double t=(double)dx/a;int dy=(int)(b*sqrt(1-t*t)+0.5);
        oq_feature_api->fill(c,cx+dx,cy-dy-1,1,2,ink);oq_feature_api->fill(c,cx+dx,cy+dy,1,2,ink);
    }
    int r=b/2+1;for(int dy=-r;dy<=r;dy++){int half=(int)sqrt((double)(r*r-dy*dy));oq_feature_api->fill(c,cx-half,cy+dy,2*half+1,1,ink);}
    if(hidden>>slot&1)for(int i=0;i<w-6;i++)oq_feature_api->fill(c,3+i,h-4-(i*(h-7))/(w-6),2,2,ink);
}
static int title_touch(unsigned slot,int x,int y){
    (void)x;(void)y;if(slot>=8||!oq_feature_on)return 0;
    __atomic_fetch_or(&ht_pending,1u<<slot,__ATOMIC_RELEASE);oq_feature_api->redraw();return 1;
}
static void title_update(const struct oqmod_waveform_context *ctx){
    ht_layout=ctx->layout;for(unsigned s=0;s<2;s++)__atomic_store_n(&ls_selected[s],ctx->selected[s],__ATOMIC_RELEASE);
    ht_view=ctx->view;ht_tick(oq_feature_on);oq_feature_api->metadata_hide(__atomic_load_n(&ht_deck_hidden,__ATOMIC_ACQUIRE));
}
static void title_filter(unsigned char *data,uint32_t n){ht_side_filter(data,n);}
static void title_enabled(int on){
    oq_feature_enabled(on);
    if(!on){ht_tick(0);oq_feature_api->metadata_hide(0);oq_feature_api->panels_set(NULL,0);}
}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->size<sizeof *a||!a->panel_renderer||!a->panels_set||!a->waveform_observer||!a->side_text_filter||!a->deck_text||!a->metadata_hide||!a->patch||!a->ep136_ok||!a->native_get||!a->native_engine||!a->fill||!a->text||!a->redraw)return 1;
    if(oq_feature_init(a,info)||!a->native_get("engine_external",0)||!a->native_engine()||!a->panel_renderer(title_draw,title_touch)||!a->waveform_observer(title_update)||!a->side_text_filter(title_filter))return 1;
    if(a->ep136_ok())ht_install();
    info->enabled=title_enabled;info->version="2.0";return 0;
}

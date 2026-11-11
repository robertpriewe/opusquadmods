#define _GNU_SOURCE
/* USB-owned main-Browse gesture, preview, queue, backup and commit implementation. */
#include "oqmod_native_feature.h"
#include <pthread.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <time.h>
static const struct oq_native_option oq_feature_options[]={
};
static const struct oq_native_feature oq_feature={"playlist-editor","Playlist Editing","Reorder tracks in USB playlists using main Browse.\nThis writes the library; keep a separate USB backup.\nA per-boot backup is made before the first save.", 150};
#define OQ_NATIVE_CUSTOM_ENTRY
#define OQ_NATIVE_FEATURE_IMPLEMENTATION
#include "oqmod_native_feature.h"
#define OQ_PLAYLIST_REORDER 1
#define olog(...) ((void)0)
static struct oqmod_playlist_services editor_io;
static unsigned editor_authorized,editor_native_ready;
static uint64_t rt_now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
static int patch_slot(uintptr_t slot,void *replacement,void **original){return oq_feature_api->patch(slot,*(const uintptr_t *)slot,replacement,original);}
static int inline_hook(uintptr_t fn,void *replacement,void **original){return oq_feature_api->hook(fn,*(const uint32_t *)fn,replacement,original);}
#include "native_playlist_reorder.h"
#include "playlist_reorder_gesture.h"
#include "playlist_reorder_knob.h"
static void editor_gui_tick(int allowed){
    __atomic_store_n(&editor_authorized,allowed&&oq_feature_on&&editor_native_ready,__ATOMIC_RELEASE);
    /* Disabling before a request is claimed cancels it; an already-running
     * native transaction must finish/rollback on its library thread. */
    if(!allowed){
        int want=PLR_PENDING;
        if(__atomic_compare_exchange_n(&g_plr_state,&want,PLR_IDLE,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)){
            plk_cur.pending=0;
            uintptr_t list=plk_list(plk_cur.widget);if(list&&list==plk_list(plk_seen))plk_refresh(list);
            plk_banner("Move cancelled: Playlist Editing is off");
        }
    }
    plk_tick();
}
static void editor_enabled(int on){oq_feature_enabled(on);if(!on)__atomic_store_n(&editor_authorized,0,__ATOMIC_RELEASE);}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->size<sizeof *a||!a->playlist_services||!a->playlist_provider||!a->native_engine||!a->ep136_ok||!a->hook||!a->patch)return 1;
    if(oq_feature_init(a,info)||!a->native_get("engine_external",0)||!a->native_engine())return 1;
    editor_io.size=sizeof editor_io;if(!a->playlist_services(&editor_io))return 1;
    if(!a->playlist_provider(editor_gui_tick))return 1;
    if(a->ep136_ok()){
        int commit=plr_install(patch_slot);unsigned gesture=commit==1?plk_install():0;
        editor_native_ready=commit==1&&gesture==15;
        a->log(editor_native_ready?"external playlist gesture/commit hooks installed":"playlist native guards refused; editor stays inactive");
    }
    info->enabled=editor_enabled;info->version="2.0";return 0;
}

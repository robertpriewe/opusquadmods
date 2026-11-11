/* SPDX-License-Identifier: GPL-3.0-only
 * OPUS integration for Kyle Hosman's native Day theme, pinned source in upstream/.
 * Addresses are independently checked against EP136 stock 1.32 before installing.
 * No AZ bus, audio, watcher, settings, or geometry is assumed to apply to OPUS.
 */
#define _GNU_SOURCE
#include "lightmode.h"
#include <math.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define AZMOD_HAVE_DAYMODE 1
#define EP_23af140 0x1fc29e0ul
#define EP_23b04e0 0x1fc3d80ul
#define EP_23ed7c0 0x2001060ul
#define EP_23ee2a0 0x2001b40ul
#define EP_23f1370 0x2004c10ul
#define EP_23fde20 0x2011740ul
#define EP_23fe810 0x20127c0ul
#define EP_2401dd0 0x2015d80ul
#define EP_2402ff0 0x2016fa0ul
#define EP_3142bc0 0x2c9ad90ul
#define EP_3142d48 0x2c9af18ul
#define EP_1dfed18 0x1a29a00ul
#define EP_1dff558 0x1a2a240ul
#define OPUS_GRID_MAKE 0x1a2aaf8ul
static _Atomic int g_dm_on;
static _Atomic int g_dm_blue;
static int ready;
static opus_patch_fn patch_slot_fn;
static int azm_patch_vtable(uintptr_t slot,void *hook,void **orig) {
    return patch_slot_fn(slot,hook,orig);
}
static FILE *azlog_open(const char *path,const char *mode) {return fopen(path,mode);}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "az_day_native.inc"
#pragma GCC diagnostic pop

#define SCCI_VT 0x2ca4cc0ul
#define SCCI_PAINT 0x2109c00ul
#define SCCI_INVALIDATE 0x20f4e70ul
static void (*original_cache_paint)(void *,void *);
static void cache_paint(void *cache,void *graphics) {
    dm_scci_pre(cache);
    original_cache_paint(cache,graphics);
}
/* Preserve the light-theme treatment of the native waveform ruler. This hook
 * only remaps the ruler canvas background/ticks; BPM/grid editing and density
 * controls are intentionally not installed in the public build. */
static uintptr_t (*original_grid_light)(void *,void *,int,int,int,int,float);
static uintptr_t grid_lightmode(void *gm,void *a,int b,int c,int d,int e,float f) {
    uint32_t keep=0;
    int changed=dm_ruler_begin(gm,&keep);
    uintptr_t result=original_grid_light(gm,a,b,c,d,e,f);
    if(changed)(void)dm_ruler_end(gm,keep);
    return result;
}
int opus_light_install(opus_patch_fn patch,opus_patch_fn inline_patch) {
    const uintptr_t offsets[]={0x90,0xe8,0xa8,0xb0,0xc0};
    const uintptr_t funcs[]={DM_SWR_SETFILL,DM_SWR_DRAWGLYPH,DM_SWR_FILLRECT_I,
                            DM_SWR_FILLRECT_F,DM_SWR_FILLPATH};
    /* Identity was already checked by opusmod_init before this is called. */
    for(unsigned i=0;i<sizeof offsets/sizeof offsets[0];i++)
        if(*(uintptr_t *)(DM_SWR_VTABLE+offsets[i])!=funcs[i])return 0;
    if(((uintptr_t *)SCCI_VT)[2]!=SCCI_PAINT ||
       ((uintptr_t *)SCCI_VT)[3]!=SCCI_INVALIDATE ||
       *(const uint32_t *)DM_GFX_DRAWIMAGE_T!=0xa9bc7bfdU ||
       *(const uint32_t *)DM_IMAGECACHE_GETFROMMEMORY!=0xa9b77bfdU ||
       *(const uint32_t *)DM_IMAGECACHE_GETFROMFILE!=0xa9b97bfdU ||
       *(const uint32_t *)DM_WF_RASTER!=0xd286a810U ||
       *(const uint32_t *)DM_WF3_RASTER!=0xd28f7010U ||
       *(const uint32_t *)OPUS_GRID_MAKE!=0xa9af7bfdU)return 0;
    patch_slot_fn=patch;
    int ok=dm_hook_setfill()==1 && dm_hook_drawglyph()==1 &&
        dm_patch_slot(DM_SWR_FILLRECT_I_OFF,DM_SWR_FILLRECT_I,
                      (void *)dm_fillrect_i_hook,(void **)&g_dm_orig_fillrect_i)==1 &&
        dm_patch_slot(DM_SWR_FILLRECT_F_OFF,DM_SWR_FILLRECT_F,
                      (void *)dm_fillrect_f_hook,(void **)&g_dm_orig_fillrect_f)==1 &&
        dm_patch_slot(DM_SWR_FILLPATH_OFF,DM_SWR_FILLPATH,
                      (void *)dm_fillpath_hook,(void **)&g_dm_orig_fillpath)==1 &&
        patch(SCCI_VT+16,(void *)cache_paint,(void **)&original_cache_paint)==1 &&
        inline_patch(DM_IMAGECACHE_GETFROMMEMORY,(void *)dm_getfrommem_hook,
                     &g_dm_orig_getfrommem)==1 &&
        inline_patch(DM_IMAGECACHE_GETFROMFILE,(void *)dm_getfromfile_hook,
                     &g_dm_orig_getfromfile)==1 &&
        inline_patch(DM_GFX_DRAWIMAGE_T,(void *)dm_drawimg_t_hook,
                     (void **)&g_dm_orig_drawimg_t)==1 &&
        inline_patch(DM_WF_RASTER,(void *)dm_wfraster_hook,&g_dm_orig_wfraster)==1 &&
        inline_patch(DM_WF3_RASTER,(void *)dm_wf3raster_hook,&g_dm_orig_wf3raster)==1 &&
        inline_patch(OPUS_GRID_MAKE,(void *)grid_lightmode,
                     (void **)&original_grid_light)==1;
    ready=ok;
    return ok;
}
int opus_light_set(int on) {
    on=!!on && ready;
    if(on!=atomic_load(&g_dm_on)) {
        atomic_store(&g_dm_on,on);
        atomic_fetch_add(&g_dm_gen,1);
        /* Old colored-background records must not survive a palette change. */
        memset(t_dm_satfills,0,sizeof t_dm_satfills);
    }
    return on;
}
int opus_light_on(void) {return atomic_load(&g_dm_on);}
int opus_light_palette_set(int blue) {
    blue=!!blue;
    if(blue!=atomic_load(&g_dm_blue)) {
        atomic_store(&g_dm_blue,blue);
        atomic_fetch_add(&g_dm_gen,1);
        memset(t_dm_satfills,0,sizeof t_dm_satfills);
    }
    return blue;
}
int opus_light_palette(void) {return atomic_load(&g_dm_blue);}
uint32_t opus_light_surface(uint32_t c) {return azmod_dm_map(c);}
uint32_t opus_light_ink(uint32_t c) {
    if(!opus_light_on())return c;
    unsigned r=(c>>16)&255,g=(c>>8)&255,b=c&255;
    if(abs((int)r-(int)g)<=12 && abs((int)b-(int)g)<=12 && dm_unL((int)g)>=0) {
        uint32_t v=(uint32_t)dm_text_L(dm_unL((int)g));
        return (c&0xff000000U)|(v<<16)|(v<<8)|v;
    }
    return azmod_dm_ink(c);
}

/* USB-owned native paint, palette, glyph, image-cache and waveform theme. */
#include "oqmod_native_feature.h"
/* Match the existing GCC7/native engine's libm ABI, and keep the standalone
 * SDK output loadable by both the test runtime and the player's glibc. */
#if defined(__aarch64__) && defined(__linux__)
__asm__(".symver pow,pow@GLIBC_2.17");
#endif
static const char *const values0[]={"Original Light", "Blue/Gray"};
static const struct oq_native_option oq_feature_options[]={
    {"palette","Palette",values0,2,0,0},
};
static const struct oq_native_feature oq_feature={"light-theme","Light Theme","Light backgrounds with dark text. Original or Blue/Gray\nwaveform palette. Disable to restore the stock dark theme.", 50};
#define OQ_NATIVE_CUSTOM_ENTRY
#define OQ_NATIVE_FEATURE_IMPLEMENTATION
#include "oqmod_native_feature.h"
#include "native/engine.inc"
static int lt_patch(uintptr_t slot,void *replacement,void **original){
    return oq_feature_api->patch(slot,*(const uintptr_t *)slot,replacement,original);
}
static int lt_hook(uintptr_t function,void *replacement,void **original){
    return oq_feature_api->hook(function,*(const uint32_t *)function,replacement,original);
}
static void light_enabled(int on){
    if(!on)opus_light_set(0); /* hooks immediately pass through on USB loss */
    oq_feature_enabled(on);
}
static void light_tick(void){
    oq_feature_tick();
    static int reported=-1;
    if(reported!=ready){reported=ready;oq_feature_api->set_status(ready?"USB native theme engine ready.":"Native paint hooks unavailable; stock colours preserved.");}
}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->size<sizeof *a||!a->theme_provider||!a->native_engine||!a->native_get||!a->ep136_ok||!a->patch||!a->hook||!a->set_status)return 1;
    if(oq_feature_init(a,info)||!a->native_get("engine_external",0)||!a->native_engine())return 1;
    /* Register before patching: refusal cannot leave hooks whose owner failed. */
    const struct oqmod_theme_provider provider={sizeof provider,opus_light_set,opus_light_on,
        opus_light_palette_set,opus_light_palette,opus_light_surface,opus_light_ink};
    if(!a->theme_provider(&provider))return 1;
    if(a->ep136_ok())(void)opus_light_install(lt_patch,lt_hook);
    info->enabled=light_enabled;info->tick=light_tick;info->version="2.2";return 0;
}

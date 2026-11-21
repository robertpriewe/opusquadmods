/* 1.77 (research): which usecase::load::TrackLoader call unloads a deck. The 4-deck header's
 * eject button (gui::waveform::DeckInfoFieldWidget "ejectBtn") reaches the deck's TrackLoader
 * through "GUI->UsecaseTrackLoader" (the same object xd_loader() finds). Interface methods
 * (vt 0x2329070, bodies on the primary object): 0x8a79e8, 0x8a7478 (load), 0x8a6b80,
 * 0x8a9ac0, 0x8a68d8. Each call is logged with its arguments and the first 32 bytes behind
 * x1..x3 when they look like pointers; the user presses EJECT on a deck once. */
#ifndef PRODJLINK_LOADTRACE_H
#define PRODJLINK_LOADTRACE_H
static void *lt_orig[5];static unsigned lt_installed,lt_lines;
static const uintptr_t lt_at[5]={0x8a79e8,0x8a7478,0x8a6b80,0x8a9ac0,0x8a68d8};
static void lt_note(unsigned k,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uintptr_t from) {
    if(lt_lines>=120)return;
    lt_lines++;
    char hb[80]="",hc[80]="",hd[80]="";unsigned char m[32];
    if(b>0x10000&&nt_peek(b,m,32))lp_hex(hb,sizeof hb,m,32);
    if(c>0x10000&&nt_peek(c,m,32))lp_hex(hc,sizeof hc,m,32);
    if(d>0x10000&&nt_peek(d,m,32))lp_hex(hd,sizeof hd,m,32);
    lp_log("loadtrace %lx(this %llx, %llx, %llx, %llx, %llx, %llx) from %lx | x1:%s | x2:%s | x3:%s",(unsigned long)lt_at[k],(unsigned long long)a,
        (unsigned long long)b,(unsigned long long)c,(unsigned long long)d,(unsigned long long)e,(unsigned long long)f,(unsigned long)from,hb,hc,hd);
}
#define LT_HOOK(k) static uint64_t lt_hook##k(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h) { \
    lt_note(k,a,b,c,d,e,f,(uintptr_t)__builtin_return_address(0)); \
    return ((uint64_t (*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t))lt_orig[k])(a,b,c,d,e,f,g,h);}
LT_HOOK(0) LT_HOOK(1) LT_HOOK(2) LT_HOOK(3) LT_HOOK(4)
static void lt_install(const struct oqmod_api *api) {
    if(lt_installed||!api->hook||!api->ep136_ok||!api->ep136_ok())return;
    lt_installed=1;
    static const uint32_t first[5]={0xa9a97bfdu,0xa9ab7bfdu,0xa9b37bfdu,0xa9b87bfdu,0xa9bc7bfdu};
    static void *const fn[5]={(void *)lt_hook0,(void *)lt_hook1,(void *)lt_hook2,(void *)lt_hook3,(void *)lt_hook4};
    unsigned ok=0;for(unsigned i=0;i<5;i++)ok+=api->hook(lt_at[i],first[i],fn[i],&lt_orig[i])==1;
    lp_log("loadtrace: %u of 5 TrackLoader hooks",ok);
}
#endif

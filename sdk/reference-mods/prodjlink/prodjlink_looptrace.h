/* 1.82 (research): the orange loop markers. dj_player::CueLoadFacade (vt 0x24b02f8): slot 4
 * updateActiveLoop(this, const CueProperty &) 0x11033d8, slot 5 claerActiveLoop(this) 0x1102168.
 * Each call is logged with its object, caller and the first 160 bytes of the CueProperty, so the
 * mod can set the same loop on a PRO DJ LINK deck when the XDJ loops (the XDJ-1000MK2 only says
 * that it loops, not where). The user sets a loop on a local deck once. Option looptrace. */
#ifndef PRODJLINK_LOOPTRACE_H
#define PRODJLINK_LOOPTRACE_H
static void *lo_orig[2];static unsigned lo_installed,lo_lines;
static uint64_t lo_hook_update(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h) {
    if(lo_lines<60){lo_lines++;
        unsigned char m[160];char hex[330]="";if(b>0x10000&&nt_peek(b,m,sizeof m))lp_hex(hex,sizeof hex,m,sizeof m);
        uint64_t vt=0;nt_peek(a,(unsigned char *)&vt,8);
        lp_log("looptrace updateActiveLoop(this %llx vt %llx) from %lx: %s",(unsigned long long)a,(unsigned long long)vt,(unsigned long)(uintptr_t)__builtin_return_address(0),hex);}
    return ((uint64_t (*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t))lo_orig[0])(a,b,c,d,e,f,g,h);
}
static uint64_t lo_hook_clear(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h) {
    if(lo_lines<60){lo_lines++;lp_log("looptrace claerActiveLoop(this %llx) from %lx",(unsigned long long)a,(unsigned long)(uintptr_t)__builtin_return_address(0));}
    return ((uint64_t (*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t))lo_orig[1])(a,b,c,d,e,f,g,h);
}
static void lo_install(const struct oqmod_api *api) {
    if(lo_installed||!api->hook||!api->ep136_ok||!api->ep136_ok())return;
    lo_installed=1;
    int u=api->hook(0x11033d8,0xd10b43ffu,(void *)lo_hook_update,&lo_orig[0])==1;
    int c=api->hook(0x1102168,0xa9b37bfdu,(void *)lo_hook_clear,&lo_orig[1])==1;
    lp_log("looptrace: updateActiveLoop hook %d, claerActiveLoop hook %d",u,c);
}
#endif
/* 1.83 (research): 1.82's updateActiveLoop was never called for loops set on deck 1. Every
 * method of dj_player::CueLoopSetFacade (vt 0x24b0bc0: setPoint = loop in, setLoopOutPoint,
 * setBeatLoop, setActiveLoop(CueProperty), ...) and CueLoopControlFacade (vt 0x24af1c8:
 * loopExit, reloop, jump, ...) is logged: slot, object, x1..x3 and 128 bytes behind x1/x2. */
#ifndef PRODJLINK_LOOPTRACE2_H
#define PRODJLINK_LOOPTRACE2_H
#define LS_N 26
static const uintptr_t ls_at[LS_N]={0x110aa80,0x110b198,0x110a4c8,0x11073b8,0x110bc48,0x110c650,0x1107990,0x110e5f0,0x110dac0,0x110e0c0,
    0x1107e70,0x11083b0,0x1108900,0x1108ed0,0x110c168,0x11093a0,
    0x10f7e68,0x11004d0,0x10f8420,0x10f89f0,0x10fb9c0,0x10fe7a0,0x10f9000,0x10f9e28,0x10f96c8,0x10f6970};
static const uint32_t ls_first[LS_N]={0xa9b27bfd,0xa9b27bfd,0xa9b27bfd,0xa9b37bfd,0xa9a17bfd,0xd108c3ff,0xa9b37bfd,0xa9b27bfd,0xa9b27bfd,0xa9b27bfd,
    0xa9b17bfd,0xa9b07bfd,0xa9b27bfd,0xa9b27bfd,0xa9a27bfd,0xa9b37bfd,
    0xa9b37bfd,0xa9b37bfd,0xa9b37bfd,0xa9b37bfd,0xa9b37bfd,0xa9b37bfd,0xa9b37bfd,0xa9b27bfd,0xa9b37bfd,0xa9bd7bfd};
static void *ls_orig[LS_N];static unsigned ls_calls[LS_N],ls_installed;
static void ls_note(unsigned k,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uintptr_t from) {
    if(ls_calls[k]>=((k==3||k==4)?2u:24u))return;   /* slots 5/6 run 8x on every load */
    ls_calls[k]++;
    char hb[260]="",hc[260]="";unsigned char m[128];
    if(b>0x10000&&nt_peek(b,m,sizeof m))lp_hex(hb,sizeof hb,m,sizeof m);
    if(c>0x10000&&nt_peek(c,m,sizeof m))lp_hex(hc,sizeof hc,m,sizeof m);
    lp_log("looptrace %s slot %u %lx(this %llx, %llx, %llx, %llx) from %lx | x1:%s | x2:%s",k<16?"CueLoopSet":"CueLoopControl",k<16?k+2:k-14,
        (unsigned long)ls_at[k],(unsigned long long)a,(unsigned long long)b,(unsigned long long)c,(unsigned long long)d,(unsigned long)from,hb,hc);
}
/* float arguments (setPreviewPoint(float), ...) pass through v0-v3 untouched */
#define LS_HOOK(k) static uint64_t ls_hook##k(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h, \
        double f0,double f1,double f2,double f3) { \
    ls_note(k,a,b,c,d,(uintptr_t)__builtin_return_address(0)); \
    return ((uint64_t (*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,double,double,double,double))ls_orig[k])(a,b,c,d,e,f,g,h,f0,f1,f2,f3);}
LS_HOOK(0) LS_HOOK(1) LS_HOOK(2) LS_HOOK(3) LS_HOOK(4) LS_HOOK(5) LS_HOOK(6) LS_HOOK(7) LS_HOOK(8) LS_HOOK(9)
LS_HOOK(10) LS_HOOK(11) LS_HOOK(12) LS_HOOK(13) LS_HOOK(14) LS_HOOK(15) LS_HOOK(16) LS_HOOK(17) LS_HOOK(18) LS_HOOK(19)
LS_HOOK(20) LS_HOOK(21) LS_HOOK(22) LS_HOOK(23) LS_HOOK(24) LS_HOOK(25)
static void ls_install(const struct oqmod_api *api) {
    if(ls_installed||!api->hook||!api->ep136_ok||!api->ep136_ok())return;
    ls_installed=1;
    static void *const fn[LS_N]={(void *)ls_hook0,(void *)ls_hook1,(void *)ls_hook2,(void *)ls_hook3,(void *)ls_hook4,(void *)ls_hook5,(void *)ls_hook6,
        (void *)ls_hook7,(void *)ls_hook8,(void *)ls_hook9,(void *)ls_hook10,(void *)ls_hook11,(void *)ls_hook12,(void *)ls_hook13,(void *)ls_hook14,
        (void *)ls_hook15,(void *)ls_hook16,(void *)ls_hook17,(void *)ls_hook18,(void *)ls_hook19,(void *)ls_hook20,(void *)ls_hook21,(void *)ls_hook22,
        (void *)ls_hook23,(void *)ls_hook24,(void *)ls_hook25};
    unsigned ok=0;for(unsigned i=0;i<LS_N;i++)ok+=api->hook(ls_at[i],ls_first[i],fn[i],&ls_orig[i])==1;
    lp_log("looptrace: %u of %u loop facade hooks",ok,LS_N);
}
#endif

/* 1.85 (research): loops set with the deck's own buttons never reached the facades above. The
 * buttons go through input_devices::LoopHandler / BeatLoopHandler, each press as an async task
 * (meow::AsyncTaskBoxBase::AsyncTask<lambda>); slot 2 of the task's vtable runs it with the task
 * in x0 (+24 LoopHandler, +32 trigger type). Logged: the task, the handler and the vtables of the
 * objects the handler calls (+24, +4008: the loop setter). */
#ifndef PRODJLINK_LOOPTRACE3_H
#define PRODJLINK_LOOPTRACE3_H
#define LT3_N 5
static const uintptr_t lt3_at[LT3_N]={0x1c8c0d0,0x1c8bf58,0x1c8aed0,0x1c8be80,0x1c67c98};
static const uint32_t lt3_first[LT3_N]={0xa9be7bfdu,0xa9bd7bfdu,0xf9400c00u,0xa9bd7bfdu,0xb9402001u};
static const char *const lt3_name[LT3_N]={"loopIn","loopOut","loopExit","reloop","selectedBeatLoop"};
static void *lt3_orig[LT3_N];static unsigned lt3_calls[LT3_N],lt3_installed;
static void lt3_vt(const char *what,uint64_t obj) {
    uint64_t vt=0,sl[14]={0};
    if(obj<0x10000||!nt_peek(obj,(unsigned char *)&vt,8)||vt<0x400000||!nt_peek(vt,(unsigned char *)sl,sizeof sl)){lp_log("looptrace   %s %llx: no object",what,(unsigned long long)obj);return;}
    lp_log("looptrace   %s %llx vt %llx: %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx %llx",what,(unsigned long long)obj,(unsigned long long)vt,
        (unsigned long long)sl[0],(unsigned long long)sl[1],(unsigned long long)sl[2],(unsigned long long)sl[3],(unsigned long long)sl[4],(unsigned long long)sl[5],(unsigned long long)sl[6],
        (unsigned long long)sl[7],(unsigned long long)sl[8],(unsigned long long)sl[9],(unsigned long long)sl[10],(unsigned long long)sl[11],(unsigned long long)sl[12],(unsigned long long)sl[13]);
}
static void lt3_note(unsigned k,uint64_t task) {
    if(lt3_calls[k]>=10)return;
    lt3_calls[k]++;
    unsigned char m[64];char hex[140]="";if(task>0x10000&&nt_peek(task,m,sizeof m))lp_hex(hex,sizeof hex,m,sizeof m);
    uint64_t h=0;if(task>0x10000)nt_peek(task+24,(unsigned char *)&h,8);
    lp_log("looptrace %s task %llx handler %llx: %s",lt3_name[k],(unsigned long long)task,(unsigned long long)h,hex);
    if(h>0x10000&&lt3_calls[k]<=2){uint64_t a=0,b=0;nt_peek(h+24,(unsigned char *)&a,8);nt_peek(h+4008,(unsigned char *)&b,8);lt3_vt("handler",h);lt3_vt("+24",a);lt3_vt("+4008",b);}
}
#define LT3_HOOK(k) static uint64_t lt3_hook##k(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h, \
        double f0,double f1,double f2,double f3) { \
    lt3_note(k,a); \
    return ((uint64_t (*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,double,double,double,double))lt3_orig[k])(a,b,c,d,e,f,g,h,f0,f1,f2,f3);}
LT3_HOOK(0) LT3_HOOK(1) LT3_HOOK(2) LT3_HOOK(3) LT3_HOOK(4)
static void lt3_install(const struct oqmod_api *api) {
    if(lt3_installed||!api->hook||!api->ep136_ok||!api->ep136_ok())return;
    lt3_installed=1;
    static void *const fn[LT3_N]={(void *)lt3_hook0,(void *)lt3_hook1,(void *)lt3_hook2,(void *)lt3_hook3,(void *)lt3_hook4};
    unsigned ok=0;for(unsigned i=0;i<LT3_N;i++)ok+=api->hook(lt3_at[i],lt3_first[i],fn[i],&lt3_orig[i])==1;
    lp_log("looptrace: %u of %u loop button hooks",ok,LT3_N);
}
#endif

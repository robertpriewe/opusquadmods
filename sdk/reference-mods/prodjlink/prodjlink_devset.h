/* 1.72 (research): which device setting decides the waveform colour of another player's
 * tracks. 1.71 showed EP136's 3-band tickets for linked tracks complete with data (result 0),
 * yet they are drawn blue: the colour must come from a per-device setting, and for a linked
 * stick EP136 has the player's (or a default), not the OPUS user's.
 * device_setting_manager::DeviceSettingManager (vt 0x24e77e0) slots 8, 10, 12, 14, 16 are
 * getters get(this, w1 device index (<= 8), w2 kind, w3 flag) -> byte from 12-byte rows in
 * five tables at this+0x90/+0x3fc/+0x768/+0xad4/+0xe40 (0x127a598, 0x127a6d0, 0x127a808,
 * 0x127a940, 0x127aa78; all start a9bb7bfd). Logged: each distinct (table, index, kind,
 * caller) with the value, so local vs. linked decks can be compared. */
#ifndef PRODJLINK_DEVSET_H
#define PRODJLINK_DEVSET_H
static void *ds_orig[5];static unsigned ds_installed,ds_lines;
#define DS_SEEN 512
static uint64_t ds_seen[DS_SEEN];static unsigned ds_nseen;
static pthread_mutex_t ds_lock=PTHREAD_MUTEX_INITIALIZER;
static void ds_note(unsigned t,uint64_t idx,uint64_t kind,uint64_t flag,uint64_t val,uintptr_t from) {
    uint64_t key=(uint64_t)t<<60|(idx&0xff)<<52|(kind&0xff)<<44|(flag&1)<<43|(val&0xff)<<32|(uint32_t)from;
    pthread_mutex_lock(&ds_lock);
    int fresh=1;for(unsigned i=0;i<ds_nseen;i++)if(ds_seen[i]==key){fresh=0;break;}
    if(fresh&&ds_nseen<DS_SEEN)ds_seen[ds_nseen++]=key;
    pthread_mutex_unlock(&ds_lock);
    if(!fresh||ds_lines>=400)return;
    ds_lines++;
    lp_log("devset get table %u device %u kind %u flag %u -> %u (from %lx)",t,(unsigned)(idx&0xffffffffu),(unsigned)(kind&0xffffffffu),(unsigned)(flag&0xff),(unsigned)(val&0xff),(unsigned long)from);
}
/* 1.73: the waveform colour is table 0, column 0 (1.72 trace, 2026-11-13: switching the colour
 * on the OPUS changed only that value for device 0: 4 / 3 / 1; read by the colour selector
 * 0x179c5ac and the waveform drawing 0x1b96964/0x1b96990). Device 0 is the OPUS user's own
 * setting. For any other device row, or a read flagged as "ask the owning player" (w3 != 0,
 * which for a Pro DJ Link stick yields that player's BLUE), the user's value is returned
 * (user rule: on the OPUS everything follows the OPUS settings). Option "linkcolour". */
static unsigned ds_colour_follow=1,ds_overrides;
typedef uint64_t (*ds_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
#define DS_HOOK(k) static uint64_t ds_hook##k(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h) { \
    uint64_t r=((ds_fn)ds_orig[k])(a,b,c,d,e,f,g,h); \
    ds_note(k,b,c,d,r,(uintptr_t)__builtin_return_address(0)); \
    if(k==0&&ds_colour_follow&&(uint32_t)c==0&&((uint32_t)b!=0||(d&0xff)!=0)) { \
        uint64_t own=((ds_fn)ds_orig[0])(a,0,0,0,e,f,g,h); \
        if((own&0xff)!=(r&0xff)){unsigned n=__atomic_add_fetch(&ds_overrides,1,__ATOMIC_RELAXED); \
            if(n<=20)lp_log("devset: waveform colour of device %u (flag %u) %u -> OPUS setting %u",(unsigned)(uint32_t)b,(unsigned)(d&0xff),(unsigned)(r&0xff),(unsigned)(own&0xff));} \
        r=(r&~0xffull)|(own&0xff);} \
    return r;}
DS_HOOK(0) DS_HOOK(1) DS_HOOK(2) DS_HOOK(3) DS_HOOK(4)
/* 1.74 (copy removed in 1.75, logging kept): the colour selector shows the selected deck's device; for a linked stick (row 3) the
 * getter above answers with the user's row 0, but picking another colour there was written to
 * row 3 (and to the XDJ, which ignores it), so it never changed. The table-0 setter
 * set(this, w1 device row, w2 kind, w3 value) = slot 7 0x1276798 (a9ba7bfd): a write of the
 * waveform colour (kind 0) to any other row also goes to row 0. */
static void *ds_set_orig;static unsigned ds_set_installed,ds_set_lines;
static uint64_t ds_set_hook(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h) {
    uint64_t r=((ds_fn)ds_set_orig)(a,b,c,d,e,f,g,h);
    if(ds_set_lines<60){ds_set_lines++;lp_log("devset set table 0 device %u kind %u value %u (from %lx)",(unsigned)(uint32_t)b,(unsigned)(uint32_t)c,(unsigned)(d&0xff),(unsigned long)(uintptr_t)__builtin_return_address(0));}
    /* 1.75: no copying. On hardware this writer is called with the XDJ's own colour for row 3
     * at start-up (caller 0x1275558), and 1.74's copy made the OPUS user's setting BLUE; the
     * colour selector's choice for a linked deck does not come through here. Logging only. */
    return r;
}
static void ds_install(const struct oqmod_api *api) {
    if(ds_installed||!api->hook||!api->ep136_ok||!api->ep136_ok())return;
    ds_installed=1;
    static const uintptr_t at[5]={0x127a598,0x127a6d0,0x127a808,0x127a940,0x127aa78};
    static void *const fn[5]={(void *)ds_hook0,(void *)ds_hook1,(void *)ds_hook2,(void *)ds_hook3,(void *)ds_hook4};
    unsigned ok=0;for(unsigned i=0;i<5;i++)ok+=api->hook(at[i],0xa9bb7bfdu,fn[i],&ds_orig[i])==1;
    lp_log("devset: %u of 5 getter hooks",ok);
    if(!ds_set_installed){ds_set_installed=1;lp_log("devset: setter hook %s",api->hook(0x1276798,0xa9ba7bfdu,(void *)ds_set_hook,&ds_set_orig)==1?"on":"off");}
}
#endif

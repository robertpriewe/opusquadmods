/* 1.71 (research): how EP136's waveform request tickets finish, to see why tracks of another
 * player are drawn blue although the 3-band data is delivered (1.70).
 * track_info_repository tickets (EP136 1.32): each calls its completion function
 * complete(ticket, &waveform, result) -> listener at ticket+0x68; ticket+0x58 = TrackID
 * {MediaID (byte 0: 1 local, 2 link player), track id at +0x60}.
 *   3-band detail 0x1495080, 3-band overview 0x14bbe98, RGB detail 0x14a0930,
 *   blue detail 0x149a338, 1200-point overview 0x14c98c0 (all start a9bc7bfd).
 * result as passed by EP136: 2 from doRequest's local path, 1 in the reply handler's failure
 * path; logged as is, with whether a waveform object came along. */
#ifndef PRODJLINK_WAVETRACE_H
#define PRODJLINK_WAVETRACE_H
static void *wt_orig[5];static unsigned wt_installed,wt_lines;
static const char *const wt_name[5]={"3-band detail","3-band overview","RGB detail","blue detail","1200pt overview"};
static void wt_note(unsigned k,uint64_t ticket,uint64_t obj_slot,uint64_t result) {
    if(wt_lines>=240)return;
    unsigned char id[16]={0};uint64_t obj=0;
    nt_peek(ticket+0x58,id,16);nt_peek(obj_slot,(unsigned char *)&obj,8);
    uint32_t track;memcpy(&track,id+8,4);
    wt_lines++;
    lp_log("wavetrace %s: track %u media %02x%02x%02x%02x%02x%02x%02x%02x (%s) result %u waveform %s",wt_name[k],track,
        id[0],id[1],id[2],id[3],id[4],id[5],id[6],id[7],id[0]==2?"link":id[0]==1?"local":"?",(unsigned)(result&0xffffffffu),obj?"yes":"none");
}
#define WT_HOOK(k) static uint64_t wt_hook##k(uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint64_t g,uint64_t h) { \
    wt_note(k,a,b,c);return ((uint64_t (*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t))wt_orig[k])(a,b,c,d,e,f,g,h);}
WT_HOOK(0) WT_HOOK(1) WT_HOOK(2) WT_HOOK(3) WT_HOOK(4)
static void wt_install(const struct oqmod_api *api) {
    if(wt_installed||!api->hook||!api->ep136_ok||!api->ep136_ok())return;
    wt_installed=1;
    static const uintptr_t at[5]={0x1495080,0x14bbe98,0x14a0930,0x149a338,0x14c98c0};
    static void *const fn[5]={(void *)wt_hook0,(void *)wt_hook1,(void *)wt_hook2,(void *)wt_hook3,(void *)wt_hook4};
    unsigned ok=0;for(unsigned i=0;i<5;i++)ok+=api->hook(at[i],0xa9bc7bfdu,fn[i],&wt_orig[i])==1;
    lp_log("wavetrace: %u of 5 ticket hooks",ok);
}
#endif

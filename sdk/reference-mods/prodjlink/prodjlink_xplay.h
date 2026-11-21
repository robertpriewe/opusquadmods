/* 1.48: decks 3/4 PLAY along with link players 3/4, so EP136 scrolls their waveform natively
 * (1.47 kept them paused and moved them; EP136 redraws a paused deck only about once a second).
 *
 * EP136 1.32 (static RE 2026-11-12):
 *  - dj_player::TempoControlFacade, one per deck in the facade holder ([11], next to
 *    PlayPosition [10]); primary vt 0x24b7ff8 / second 0x24b8040, registered in the same
 *    ObjectMap as PlayPositionControlFacade. Slots: 2 tempoReset(bool) 0x1129b40,
 *    3 tempoChange(float) 0x112a168, 4 pitchBend(float) 0x1128a30, 5 tempoLock() 0x1128fb8,
 *    6 setEnableTempoSlider(bool) 0x1129580 (names from their task RTTI). tempoChange stores
 *    the float as the tempo value; its unit (percent or slider position) is measured on the
 *    deck: set 0, then 1, and read the deck's pitch (api->deck pitch_x100).
 *  - The deck's own audio goes to the mixer through snd_pcm_writei (GOT 0x349a130,
 *    interleaved S32, 8 channels = decks 1-4 as stereo pairs). While a deck mirrors a link
 *    player it is silenced there (option xmute), so only the player's own audio is heard.
 *
 * Control while the player plays: deck plays, tempo = player's pitch + a trim from the
 * position error (0.02 % per ms, at most 6 %, dead band 15 ms); errors over 1.5 s (hot cue,
 * search) pause the deck, move it there and start it again. Player paused: deck paused and
 * the 1.47 follower places it. */
#ifndef PRODJLINK_XPLAY_H
#define PRODJLINK_XPLAY_H
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>

/* ---- pure decision step (unit-tested in prodjlink-lab/xdtest/play_test.c) ---------------- */
struct xp_state {
    double off_f,sent_pct;unsigned sent_valid,started,jumps,trims,move_check,move_bad,move_fails;uint64_t hold_until;
};
enum {XP_NONE=0,XP_PLAY=1,XP_PAUSE=2,XP_TEMPO=4,XP_MOVE=8};
#ifndef XP_JUMP_MS
#define XP_JUMP_MS 300.0
#endif
/* in: player playing/pitch, target and the deck's state; out: actions + tempo % + move ms */
static unsigned xp_step(struct xp_state *s,int player_playing,double player_pitch_pct,double target,
                        int deck_playing,double deck_ms,uint64_t now,double *tempo_pct,double *move_ms) {
    unsigned act=XP_NONE;*tempo_pct=0;*move_ms=0;
    double off=target-deck_ms;
    if(!player_playing) {
        if(deck_playing){act|=XP_PAUSE;s->started=0;}
        s->off_f=0;return act;
    }
    if(!deck_playing) {
        /* start: put the deck where the player is (a paused deck can be moved), then play */
        if(now<s->hold_until)return act;
        if(fabs(off)>=5.0){act|=XP_MOVE;*move_ms=off;}
        act|=XP_PLAY;s->started=1;s->off_f=0;s->hold_until=now+1000;  /* let it start and sync before judging */
        *tempo_pct=player_pitch_pct;act|=XP_TEMPO;s->sent_pct=player_pitch_pct;s->sent_valid=1;
        return act;
    }
    if(now<s->hold_until)return act;
    s->off_f=s->off_f*0.92+off*0.08;   /* 1.72: heavier smoothing (the deck reports its position late, noisy) */
    /* 1.88 (user: the first turn of a loop is choppy): a jump is first followed by moving the
     * playing deck (one seek); only if the deck did not get there is it paused, moved and
     * restarted (as before) - and from then on always. */
    if(s->move_check) {
        s->move_check=0;
        if(fabs(off)>XP_JUMP_MS){if(++s->move_fails>=2)s->move_bad=1;act|=XP_PAUSE;s->jumps++;s->hold_until=now+250;s->off_f=0;return act;}   /* 1.89: two tries */
    }
    if(fabs(s->off_f)>XP_JUMP_MS||(!s->move_bad&&fabs(off)>XP_JUMP_MS*3.0)) {   /* a jump on the player (or a whole-beat offset) */
        s->jumps++;s->off_f=0;
        if(!s->move_bad){act|=XP_MOVE;*move_ms=off;s->hold_until=now+600;s->move_check=1;return act;}
        act|=XP_PAUSE;s->hold_until=now+250;return act;
    }
    /* 1.72 (user: deck wobbles): 0.008 % per ms past a 25 ms dead band up to 50 ms, beyond that 0.02 %/ms to catch up (at most 3 %), on a
     * heavily smoothed offset (1.48-1.71: 0.02 %/ms up to 6 %, light smoothing: swung the deck
     * by +-2.5 % around a steady player). Tuned in prodjlink-lab/xdtest/lag_test.c (report and
     * tempo delays + noise): swing ~2 % -> ~0.7 % at heavy noise. */
    double ao=fabs(s->off_f),sg=s->off_f>0?1.0:-1.0,trim;
    if(ao<25.0)trim=0.0;
    else if(ao<50.0)trim=sg*(ao-25.0)*0.008;                  /* near: gentle, at most 0.2 % */
    else trim=sg*(0.2+(ao-50.0)*0.02);                         /* clearly off (after a start): catch up */
    if(trim>3.0)trim=3.0;
    if(trim<-3.0)trim=-3.0;
    double want=player_pitch_pct+trim;
    if(!s->sent_valid||fabs(want-s->sent_pct)>=0.05){*tempo_pct=want;act|=XP_TEMPO;s->sent_pct=want;s->sent_valid=1;if(trim!=0.0)s->trims++;}
    return act;
}

#ifndef XP_PURE_TEST
/* ---- EP136 calls ---------------------------------------------------------------------- */
static uint64_t xp_tempo_facade[4];static unsigned xp_tempo_found;
static void xp_find_tempo(void) {
    unsigned char ready=0;if(!nt_peek(0x34e94c8,&ready,1)||!(ready&1))return;
    uint64_t buckets=0;int32_t count=0;
    if(!nt_peek(0x34e94d8,(unsigned char *)&buckets,8)||!nt_peek(0x34e94e8,(unsigned char *)&count,4)||!buckets||count<=0||count>4096)return;
    uint64_t key[16],obj[16];unsigned n=0;
    for(int b=0;b<count;b++) {
        uint64_t node=0;nt_peek(buckets+8u*(unsigned)b,(unsigned char *)&node,8);
        for(unsigned hop=0;node&&hop<64;hop++) {
            uint64_t f[3]={0},vt[2]={0};if(!nt_peek(node,(unsigned char *)f,24))break;
            if(f[1]&&nt_peek(f[1],(unsigned char *)vt,16)&&vt[0]==0x24b7ff8&&vt[1]==0x24b8040&&n<16){key[n]=f[0];obj[n]=f[1];n++;}
            node=f[2];
        }
    }
    unsigned found=0;uint64_t pick[4]={0};
    for(unsigned i=0;i<n;i++){unsigned k=(unsigned)(key[i]&255);if(k<4){unsigned same=0;for(unsigned j=0;j<n;j++)if((key[j]>>8)==(key[i]>>8)&&(key[j]&255)<4)same++;if(same==4&&!pick[k]){pick[k]=obj[i];found++;}}}
    static unsigned logged;
    if(!logged){logged=1;XD_LOG("xplay: %u tempo facades in the map",n);for(unsigned i=0;i<n;i++)XD_LOG("xplay: tempo facade key %016llx at %llx",(unsigned long long)key[i],(unsigned long long)obj[i]);}
    if(found!=4&&n==4){for(unsigned i=0;i<4;i++){unsigned r=0;for(unsigned j=0;j<4;j++)if(key[j]<key[i])r++;pick[r]=obj[i];}found=4;}
    /* 1.49: not in that map (1.48 found 0). The facade holder (0x1235488) creates each deck's
     * facades one after another: [10] PlayPosition, then [11] Tempo, so each Tempo facade sits
     * just after that deck's PlayPosition facade on the heap. Both start with the same base
     * (0x122ef20 at +8: +0x10/+0x18/+0x20 are the deck's, +0x28 the facade's key): a candidate
     * must carry the same words at +0x10 and +0x18. */
    if(found!=4&&xd_facades_found) {
        found=0;memset(pick,0,sizeof pick);
        static unsigned char window[0x6000];
        for(unsigned d=0;d<4;d++) {
            uint64_t pp=xd_facade[d],start=pp>=0x1000?pp-0x1000:0,best=0,owner[2]={0};
            if(!pp||!nt_peek(pp+0x10,(unsigned char *)owner,16))continue;
            /* page by page (one unmapped page must not lose the rest); missing pages stay zero */
            memset(window,0,sizeof window);
            for(unsigned o=0;o<sizeof window;) {
                uint64_t a=start+o,end=(a|0xfff)+1;size_t len=(size_t)(end-a);
                if(o+len>sizeof window){len=sizeof window-o;}
                (void)nt_peek(a,window+o,len);o+=(unsigned)len;
            }
            for(unsigned o=0;o+0x20<=sizeof window;o+=8) {
                uint64_t w[4];memcpy(w,window+o,32);
                if(w[0]!=0x24b7ff8||w[1]!=0x24b8040||w[2]!=owner[0]||w[3]!=owner[1])continue;
                uint64_t at=start+o;
                if(!best||(at>pp?at-pp:pp-at)<(best>pp?best-pp:pp-best))best=at;
            }
            XD_LOG("xplay: deck %u tempo facade next to position facade %llx: %llx (%+lld bytes)",d+1,(unsigned long long)pp,(unsigned long long)best,best?(long long)(best-pp):0LL);
            if(best){pick[d]=best;found++;}
        }
    }
    if(found==4){memcpy(xp_tempo_facade,pick,sizeof pick);xp_tempo_found=1;XD_LOG("xplay: tempo facades for decks 1-4 found");}
}
/* 1.50: tempoChange is the tempo slider's input: it only applies while the deck has a
 * physical slider (1.49: deck 4's pitch stayed 0.00 %), and calling it on deck 4 is the prime
 * suspect for 1.49's frozen deck 4. Not used any more. Deck 4 now gets BEAT SYNC instead:
 * BeatSyncControlFacade (holder [12], created right after Tempo; vt 0x24ae778/0x24ae7c0),
 * slot 2 setSyncMode(int) 0x10f47c8 (first insn a9b37bfd). It maps 0->1, 1->0, 2->2 before
 * calling the player, so which value means ON is measured: deck 4's speed against the player's. */
static uint64_t xp_sync_facade[4];static unsigned xp_sync_found;
static int xp_sync_set(unsigned deck,int mode) {
    if(!xp_sync_found||deck<1||deck>4||!xp_sync_facade[deck-1])return 0;
    if(*(const uint32_t *)0x10f47c8!=0xa9b37bfdu)return 0;
    ((void (*)(uint64_t,int))0x10f47c8)(xp_sync_facade[deck-1],mode);
    return 1;
}
/* 1.52: tempoChange's inner 0x11706f0 applies the value only if byte X+0x30 is set
 * (X = *(facade+0x68)); setEnableTempoSlider(bool) (slot 6, 0x1129580) writes exactly that
 * byte. Deck 4 has it off (it is on no side), which is why 1.49 saw no effect. Enable it, then
 * tempoChange sets the deck's tempo like its own slider (shown on the main screen too). */
static int xp_tempo_enable(unsigned deck,int on) {
    if(!xp_tempo_found||deck<1||deck>4||!xp_tempo_facade[deck-1])return 0;
    if(*(const uint32_t *)0x1129580!=0xa9b37bfdu)return 0;
    ((void (*)(uint64_t,int))0x1129580)(xp_tempo_facade[deck-1],on?1:0);
    return 1;
}
static int xp_tempo_change(unsigned deck,float value) {
    if(!xp_tempo_found||deck<1||deck>4||!xp_tempo_facade[deck-1])return 0;
    if(*(const uint32_t *)0x112a168!=0xa9b37bfdu)return 0;   /* stp x29,x30,[sp,#-208]! */
    ((void (*)(uint64_t,float))0x112a168)(xp_tempo_facade[deck-1],value);
    return 1;
}
/* 1.76: TempoControlFacade slot 4 pitchBend(float) 0x1128a30 (a9b37bfd), the jog-nudge style
 * bend. Its unit is measured on the deck (xp_bend_calibrate). Used so the deck's TEMPO stays
 * exactly the player's (the BPM display no longer flickers) and drift is corrected by bending. */
static int xp_pitch_bend(unsigned deck,float value) {
    if(!xp_tempo_found||deck<1||deck>4||!xp_tempo_facade[deck-1])return 0;
    if(*(const uint32_t *)0x1128a30!=0xa9b37bfdu)return 0;
    ((void (*)(uint64_t,float))0x1128a30)(xp_tempo_facade[deck-1],value);
    return 1;
}
/* 1.54: deck tempo range. StateSetFacade (holder [6], vt 0x24b7310/0x24b7368) slot 6
 * setTempoRange(TempoRange 0..3) 0x1127920 (first insn a9b37bfd): its task (0x1127ee8 ->
 * 0x1170808) calls the deck state's vt+0x60 with the range still in w1 - a setter returning the
 * applied range (Ghidra hides that argument). Which value is WIDE is measured (tempoChange 1 at
 * the new range). The StateSet facade is created a little before PlayPosition (same base words). */
static uint64_t xp_state_facade[4];static unsigned xp_state_found;
static void xp_find_state(void) {
    if(!xd_facades_found)return;
    static unsigned char window[0x3000];unsigned found=0;uint64_t pick[4]={0};
    for(unsigned d=0;d<4;d++) {
        uint64_t pp=xd_facade[d],owner[2]={0},best=0,start=pp>=0x3000?pp-0x3000:0;
        if(!pp||!nt_peek(pp+0x10,(unsigned char *)owner,16))continue;
        memset(window,0,sizeof window);
        for(unsigned o=0;o<sizeof window;){uint64_t a=start+o,e=(a|0xfff)+1;size_t len=(size_t)(e-a);if(o+len>sizeof window){len=sizeof window-o;}(void)nt_peek(a,window+o,len);o+=(unsigned)len;}
        for(unsigned o=0;o+0x20<=sizeof window;o+=8){uint64_t w[4];memcpy(w,window+o,32);
            if(w[0]==0x24b7310&&w[1]==0x24b7368&&w[2]==owner[0]&&w[3]==owner[1])best=start+o;}   /* the nearest before pp */
        XD_LOG("xplay: deck %u state facade before position facade %llx: %llx (%+lld bytes)",d+1,(unsigned long long)pp,(unsigned long long)best,best?(long long)(best-pp):0LL);
        if(best){pick[d]=best;found++;}
    }
    if(found==4){memcpy(xp_state_facade,pick,sizeof pick);xp_state_found=1;XD_LOG("xplay: state facades for decks 1-4 found");}
}
static int xp_tempo_range(unsigned deck,int range) {
    if(!xp_state_found||deck<1||deck>4||!xp_state_facade[deck-1]||range<0||range>3)return 0;
    if(*(const uint32_t *)0x1127920!=0xa9b37bfdu)return 0;
    ((void (*)(uint64_t,int))0x1127920)(xp_state_facade[deck-1],range);
    return 1;
}
/* The BeatSync facade sits just after that deck's Tempo facade (same deck base words). */
static void xp_find_sync(void) {
    if(!xp_tempo_found)return;
    static unsigned char window[0x2000];unsigned found=0;uint64_t pick[4]={0};
    for(unsigned d=0;d<4;d++) {
        uint64_t tf=xp_tempo_facade[d],owner[2]={0},best=0;
        if(!tf||!nt_peek(tf+0x10,(unsigned char *)owner,16))continue;
        memset(window,0,sizeof window);
        for(unsigned o=0;o<sizeof window;){uint64_t a=tf+o,e=(a|0xfff)+1;size_t len=(size_t)(e-a);if(o+len>sizeof window){len=sizeof window-o;}(void)nt_peek(a,window+o,len);o+=(unsigned)len;}
        for(unsigned o=8;o+0x20<=sizeof window;o+=8){uint64_t w[4];memcpy(w,window+o,32);
            if(w[0]==0x24ae778&&w[1]==0x24ae7c0&&w[2]==owner[0]&&w[3]==owner[1]){best=tf+o;break;}}
        XD_LOG("xplay: deck %u sync facade after tempo facade %llx: %llx (%+lld bytes)",d+1,(unsigned long long)tf,(unsigned long long)best,best?(long long)(best-tf):0LL);
        if(best){pick[d]=best;found++;}
    }
    if(found==4){memcpy(xp_sync_facade,pick,sizeof pick);xp_sync_found=1;XD_LOG("xplay: sync facades for decks 1-4 found");}
}

/* ---- silence a mirroring deck's own audio ------------------------------------------------- */
static unsigned xp_mute_mask;            /* bit d-1: deck d silent */
struct xp_fmt {void *pcm;int ok,channels,format;};
static struct xp_fmt xp_fmts[4];
static size_t (*xp_al_sizeof)(void);static int (*xp_al_current)(void *,void *);
static int (*xp_al_channels)(const void *,unsigned *);static int (*xp_al_format)(const void *,int *);
static int xp_deck_output(void *pcm) {
    for(unsigned i=0;i<4;i++)if(xp_fmts[i].pcm==pcm)return xp_fmts[i].ok&&xp_fmts[i].channels==8&&xp_fmts[i].format==10;
    for(unsigned i=0;i<4;i++)if(!xp_fmts[i].pcm) {
        struct xp_fmt *f=&xp_fmts[i];f->pcm=pcm;
        if(!xp_al_sizeof||!xp_al_current||!xp_al_channels||!xp_al_format)return 0;
        size_t n=xp_al_sizeof();if(!n||n>4096)return 0;
        unsigned char params[4096] __attribute__((aligned(16)));memset(params,0,n);unsigned ch=0;int fmt=-1;
        if(xp_al_current(pcm,params)<0||xp_al_channels(params,&ch)<0||xp_al_format(params,&fmt)<0)return 0;
        f->channels=(int)ch;f->format=fmt;f->ok=1;return ch==8&&fmt==10;
    }
    return 0;
}
typedef long (*xp_writei_t)(void *,const void *,unsigned long);
static xp_writei_t xp_orig_writei;
static int32_t xp_scratch[8*1024];
static unsigned xp_muted_periods;
/* 1.93: loudest sample per deck pair since the last read (audio probe, prodjlink_audioprobe.h) */
static unsigned xp_peak[4],xp_probe_calls,xp_probe_deck_calls;
static long xp_writei(void *pcm,const void *buf,unsigned long frames) {
    unsigned mask=__atomic_load_n(&xp_mute_mask,__ATOMIC_RELAXED);
    __atomic_fetch_add(&xp_probe_calls,1,__ATOMIC_RELAXED);
    if(!buf||!frames||frames>1024||!xp_deck_output(pcm))return xp_orig_writei(pcm,buf,frames);
    __atomic_fetch_add(&xp_probe_deck_calls,1,__ATOMIC_RELAXED);
    {const int32_t *s=buf;unsigned pk[4]={0,0,0,0};
     for(unsigned long i=0;i<frames;i++)for(unsigned d=0;d<4;d++){
        int32_t l=s[i*8+d*2],r=s[i*8+d*2+1];unsigned a=l<0?(unsigned)-(int64_t)l:(unsigned)l,b=r<0?(unsigned)-(int64_t)r:(unsigned)r;
        if(a>pk[d])pk[d]=a;
        if(b>pk[d])pk[d]=b;}
     for(unsigned d=0;d<4;d++)if(pk[d]>__atomic_load_n(&xp_peak[d],__ATOMIC_RELAXED))__atomic_store_n(&xp_peak[d],pk[d],__ATOMIC_RELAXED);}
    if(!mask)return xp_orig_writei(pcm,buf,frames);
    memcpy(xp_scratch,buf,frames*8*sizeof(int32_t));
    for(unsigned d=0;d<4;d++)if(mask>>d&1u)for(unsigned long i=0;i<frames;i++){xp_scratch[i*8+d*2]=0;xp_scratch[i*8+d*2+1]=0;}
    __atomic_fetch_add(&xp_muted_periods,1,__ATOMIC_RELAXED);
    return xp_orig_writei(pcm,xp_scratch,frames);
}
static int xp_mute_install(void) {
    static int done;if(done)return done>0;
    void **slot=(void **)0x349a130;void *cur=__atomic_load_n(slot,__ATOMIC_ACQUIRE);
    /* libasound's function, or another mod's hook chained before us (Mono Master, Mobius) */
    if(!cur||(uintptr_t)cur<0x10000){done=-1;XD_LOG("xplay: snd_pcm_writei slot %p unusable: no muting",cur);return 0;}
    void *all=dlopen(NULL,RTLD_NOW);   /* RTLD_DEFAULT needs _GNU_SOURCE */
    xp_al_sizeof=(size_t (*)(void))dlsym(all,"snd_pcm_hw_params_sizeof");
    xp_al_current=(int (*)(void *,void *))dlsym(all,"snd_pcm_hw_params_current");
    xp_al_channels=(int (*)(const void *,unsigned *))dlsym(all,"snd_pcm_hw_params_get_channels");
    xp_al_format=(int (*)(const void *,int *))dlsym(all,"snd_pcm_hw_params_get_format");
    long pg=sysconf(_SC_PAGESIZE);uintptr_t page=(uintptr_t)slot&~(uintptr_t)(pg-1);
    if(mprotect((void *)page,(size_t)pg,PROT_READ|PROT_WRITE)){done=-1;XD_LOG("xplay: GOT page not writable: no muting");return 0;}
    xp_orig_writei=(xp_writei_t)cur;__atomic_store_n(slot,(void *)xp_writei,__ATOMIC_RELEASE);
    done=1;XD_LOG("xplay: deck audio mute hook installed (before: %p, alsa helpers %s)",cur,xp_al_sizeof&&xp_al_current&&xp_al_channels&&xp_al_format?"found":"MISSING");return 1;
}

/* ---- per deck: tempo unit and the step -------------------------------------------------- */
struct xp_deck {
    struct xp_state s;unsigned calib,tempo_ok;uint64_t calib_ms;int calib_p0;double pct_per_unit;
    int range_try,best_range;double best_pct;   /* 1.54: WIDE search */
    unsigned mv_watch;uint64_t mv_ms;double mv_from,mv_by;   /* 1.90: research, a move while playing */
    double last_units;uint64_t last_sent_ms;   /* 1.53: re-measure when the deck's range changes */
    int cur_range,range_known;                 /* 1.71: range in use (WIDE only when needed) */
    /* 1.76: bend calibration: 0 not yet, 1 baseline, 2 bending, 3 usable, 4 not usable */
    int bend_state,bend_try;uint64_t bend_t0,steady_since;double bend_val,bend_unit,base_sum,bend_sum,last_bend,last_base_pitch;unsigned base_n,bend_n;
    unsigned play_tries;                       /* 1.77: PLAY sent while the deck stays paused */
    unsigned reload_given_up;                  /* 1.94 */
    unsigned stall_eps;uint64_t play_since;    /* 1.96: ignored-PLAY episodes, steady play start */
    unsigned at_end_logged;                    /* 1.79 */
};
static struct xp_deck xpd[2];
/* Returns 1 while this deck is played along (the 1.47 paused follower must not run). */
static int xp_tick(const struct oqmod_api *api,unsigned deck,struct xd_deck *x,int player_playing,double player_pitch_pct,double target,uint64_t now) {
    struct xp_deck *p=&xpd[deck-3];
    if(!xp_tempo_found){static uint64_t last;if(now-last>3000){last=now;xp_find_tempo();}if(!xp_tempo_found)return 0;}
    struct oqmod_deck d;memset(&d,0,sizeof d);if(!api->deck(deck,&d)||!d.loaded)return 0;
    /* once: open the deck's tempo input, (1.54) try the tempo ranges for WIDE, and measure one
     * unit of tempoChange at the range kept (deck paused). calib: 0 start, 1 wait 0, 2 wait 1, 3 done */
    /* 1.78: the units are known from the hardware runs (2026-11-12/13): tempoChange 1 = the
     * range's width (range 1 = 10 %, 2 = 16 %, 3 = WIDE 100 %), pitchBend 1 = ~97 % speed. The
     * calibration below set the deck to +100 % for half a second, which the BPM display then
     * slid down from ("starts at 135"). Now: set up directly; the 1.53 re-measure still corrects
     * the tempo scale if a range differs. Option xknown (0 = measure as before). */
    if(p->calib<3&&api->setting_get("xknown",1)&&(xp_state_found||(xp_find_state(),xp_state_found))) {
        double ap=fabs(player_pitch_pct);int r=ap>10.0?3:ap>4.0?2:1;
        xp_tempo_enable(deck,1);xp_tempo_range(deck,r);
        p->tempo_ok=1;p->pct_per_unit=r==3?100.0:r==2?16.0:10.0;p->cur_range=r;p->range_known=1;p->best_pct=100.0;
        double units=player_pitch_pct/p->pct_per_unit;if(units>1.0)units=1.0;if(units<-1.0)units=-1.0;
        xp_tempo_change(deck,(float)units);p->last_units=units;p->last_sent_ms=now;
        if(api->setting_get("xbend",1)){p->bend_state=3;p->bend_unit=97.0;p->bend_val=0.1;}
        p->calib=3;
        XD_LOG("xplay: deck %u tempo input on, range %d (%.0f %% per unit), drift by bending: tempo stays the player's from the start",deck,r,p->pct_per_unit);
    }
    if(p->calib<3) {
        if(d.playing){if(api->deck_pause)api->deck_pause(deck);return 1;}
        if(p->calib==0) {
            static const int order[4]={3,2,0,1};
            int wide=api->setting_get("xwide",1)&&(xp_state_found||(xp_find_state(),xp_state_found));
            if(p->range_try==0){xp_tempo_enable(deck,1);p->best_range=-1;p->best_pct=0;}
            if(wide&&p->range_try<4)xp_tempo_range(deck,order[p->range_try]);
            xp_tempo_change(deck,0.0f);p->calib=1;p->calib_ms=now;return 1;
        }
        if(p->calib==1){if(now-p->calib_ms<500)return 1;p->calib_p0=d.pitch_x100;xp_tempo_change(deck,1.0f);p->calib=2;p->calib_ms=now;return 1;}
        if(now-p->calib_ms<500)return 1;
        static const int order[4]={3,2,0,1};
        double diff=(d.pitch_x100-p->calib_p0)/100.0;
        int wide=api->setting_get("xwide",1)&&xp_state_found;
        XD_LOG("xplay: deck %u tempo input on%s; tempoChange 0 -> pitch %.2f %%, 1 -> %.2f %%",deck,
            wide?(p->range_try==0?", range 3":p->range_try==1?", range 2":p->range_try==2?", range 0":", range 1"):"",p->calib_p0/100.0,d.pitch_x100/100.0);
        if(wide&&p->range_try<4) {
            if(fabs(diff)>p->best_pct){p->best_pct=fabs(diff);p->best_range=order[p->range_try];}
            p->range_try++;
            if(fabs(diff)<50.0&&p->range_try<4){p->calib=0;return 1;}       /* not WIDE yet: next range */
            if(fabs(diff)<50.0&&p->best_range>=0){xp_tempo_range(deck,p->best_range);p->calib=0;p->range_try=4;return 1;}   /* measure the widest again */
        }
        if(fabs(diff)>=0.05){p->tempo_ok=1;p->pct_per_unit=diff;XD_LOG("xplay: deck %u: 1 unit = %.3f %%, tempo follows the player",deck,diff);}
        else XD_LOG("xplay: deck %u tempo still does not follow: playing at the track's tempo (with jumps)",deck);
        xp_tempo_change(deck,0.0f);p->calib=3;
    }
    /* 1.71: WIDE (+-100 %) moves the tempo in 0.5 % steps, so the small corrections that keep
     * the deck in step were visible as a wobble. WIDE is now used only while the player's own
     * tempo needs it; otherwise +-10 % (player within 4 %) or +-16 % (within 10 %), which are
     * fine-grained. Range numbers as setTempoRange takes them: 3 = WIDE (measured), 1 and 2
     * taken as 10 % and 16 %; the 1.53 re-measure below corrects the scale if that is off. */
    if(p->tempo_ok&&p->best_pct>=50.0&&xp_state_found&&api->setting_get("xwide",1)) {
        if(!p->range_known){p->range_known=1;p->cur_range=3;}
        double ap=fabs(player_pitch_pct);
        int want=ap>10.0?3:ap>4.0?2:1;
        if(want<p->cur_range&&ap>(p->cur_range==3?9.0:3.0))want=p->cur_range;   /* a little slack before narrowing */
        if(want!=p->cur_range&&xp_tempo_range(deck,want)) {
            XD_LOG("xplay: deck %u tempo range %d -> %d (player %+.2f %%)",deck,p->cur_range,want,player_pitch_pct);
            p->cur_range=want;p->pct_per_unit=want==3?100.0:want==2?16.0:10.0;p->s.sent_valid=0;p->last_units=0;
        }
    }
    /* 1.76: measure the bend's unit once the deck runs steadily (tempo = player's, |off| < 40 ms
     * for 3 s): 1 s of baseline offset, then 1.5 s with a bend; the offset change gives % per
     * unit. Tries 0.1, 1 and 10. Not usable -> tempo trims as before. */
    double raw_off=target-(double)d.elapsed_ms;
    int use_bend=api->setting_get("xbend",1)&&p->tempo_ok;
    if(use_bend&&p->bend_state<3&&p->bend_try<3) {
        int steady=player_playing&&d.playing&&fabs(p->s.off_f)<40.0&&fabs(player_pitch_pct-p->last_base_pitch)<0.01;
        p->last_base_pitch=player_pitch_pct;
        if(p->bend_state==0){if(!steady){p->steady_since=0;}else if(!p->steady_since)p->steady_since=now;
            else if(now-p->steady_since>=3000){p->bend_state=1;p->bend_t0=now;p->base_sum=0;p->base_n=0;}}
        if(p->bend_state>=1&&(!player_playing||!d.playing||fabs(raw_off)>400.0)){   /* interrupted: start again later */
            xp_pitch_bend(deck,0.0f);p->bend_state=0;p->steady_since=0;
        } else if(p->bend_state==1) {
            if(now-p->bend_t0>=500){p->base_sum+=raw_off;p->base_n++;}
            if(now-p->bend_t0>=1000&&p->base_n){static const double tries[3]={0.1,1.0,10.0};p->bend_val=tries[p->bend_try];
                p->bend_state=2;p->bend_t0=now;p->bend_sum=0;p->bend_n=0;xp_pitch_bend(deck,(float)p->bend_val);}
            return 1;
        } else if(p->bend_state==2) {
            xp_pitch_bend(deck,(float)p->bend_val);   /* kept up every tick (a nudge may decay) */
            if(now-p->bend_t0>=1000){p->bend_sum+=raw_off;p->bend_n++;}
            if(now-p->bend_t0>=1500&&p->bend_n) {
                xp_pitch_bend(deck,0.0f);
                double moved=(p->base_sum/p->base_n)-(p->bend_sum/p->bend_n);   /* ms the deck gained */
                double pct=moved/1250.0*100.0;
                XD_LOG("xplay: deck %u bend %.2f: deck moved %+.0f ms in ~1.25 s = %+.2f %% speed",deck,p->bend_val,moved,pct);
                if(fabs(pct)>=3.0&&fabs(pct)<=60.0){p->bend_unit=pct/p->bend_val;p->bend_state=3;
                    XD_LOG("xplay: deck %u: drift is corrected by bending (1 unit = %.2f %%), tempo stays exactly the player's",deck,p->bend_unit);}
                else if(++p->bend_try>=3){p->bend_state=4;XD_LOG("xplay: deck %u: bend has no usable effect, tempo trims as before",deck);}
                else p->bend_state=0;
                p->steady_since=0;p->s.sent_valid=0;
            }
            return 1;
        }
    }
    double tempo=0,move=0;
    unsigned act=xp_step(&p->s,player_playing,player_pitch_pct,target,d.playing,(double)d.elapsed_ms,now,&tempo,&move);
    if(act&XP_MOVE){xd_move(deck,move);
        if(d.playing&&p->mv_watch<6){p->mv_watch++;p->mv_ms=now;p->mv_from=d.elapsed_ms;p->mv_by=move;
            XD_LOG("xplay: deck %u moved %+.0f ms while playing (deck at %u, player %.0f)",deck,move,d.elapsed_ms,target);}}
    if(p->mv_ms&&now-p->mv_ms>=150&&now-p->mv_ms<700&&((now-p->mv_ms)/150)!=((now-p->mv_ms-33)/150)) {
        XD_LOG("xplay: deck %u %llu ms after the move: at %u (expected about %.0f), player %.0f",deck,(unsigned long long)(now-p->mv_ms),d.elapsed_ms,p->mv_from+p->mv_by+(double)(now-p->mv_ms),target);
    }
    if(p->mv_ms&&now-p->mv_ms>=700)p->mv_ms=0;
    if(use_bend&&p->bend_state==3&&d.playing&&player_playing) {
        /* tempo exactly the player's; the trim xp_step wants goes into the bend */
        double trim=p->s.sent_valid?p->s.sent_pct-player_pitch_pct:0.0;
        double bend=trim/p->bend_unit,lim=fabs(p->bend_val)*2.0;if(bend>lim)bend=lim;if(bend<-lim)bend=-lim;
        if(act&XP_TEMPO){double units=player_pitch_pct/p->pct_per_unit;if(units>1.0)units=1.0;if(units<-1.0)units=-1.0;
            if(fabs(units-p->last_units)>1e-6||!p->last_sent_ms){xp_tempo_change(deck,(float)units);p->last_units=units;p->last_sent_ms=now;}}
        if(bend!=0.0||p->last_bend!=0.0)xp_pitch_bend(deck,(float)bend);
        p->last_bend=bend;
        act&=~(unsigned)XP_TEMPO;
    }
    if((act&XP_TEMPO)&&p->tempo_ok){
        double units=tempo/p->pct_per_unit;if(units>1.0)units=1.0;if(units<-1.0)units=-1.0;   /* slider end */
        xp_tempo_change(deck,(float)units);p->last_units=units;p->last_sent_ms=now;
    }
    /* 1.53: one unit is the deck's tempo range (10 % at +-10 %). If the range was changed (e.g.
     * to WIDE by hand), the deck's pitch no longer matches what was sent: take the new scale. */
    if(p->tempo_ok&&fabs(p->last_units)>=0.05&&fabs(p->last_units)<=0.95&&now-p->last_sent_ms>=400) {
        double measured=(d.pitch_x100/100.0)/p->last_units;
        if(fabs(measured)>=1.0&&fabs(measured-p->pct_per_unit)>0.5){
            XD_LOG("xplay: deck %u tempo range changed: 1 unit %.2f %% -> %.2f %%",deck,p->pct_per_unit,measured);
            p->pct_per_unit=measured;p->s.sent_valid=0;
        }
        p->last_sent_ms=now+1000000000ull;   /* once per sent value */
    }
    if(d.playing)p->play_tries=0;
    /* 1.96: hardware 2026-11-15: after the first load the deck played for a moment after each
     * "pause, then play again" (resetting play_tries) and stalled again, for 90 s, moved every
     * second (choppy, cues jumping). The reload at last fixed it at once. Count the episodes;
     * only 5 s of steady play clears them; the 2nd episode reloads (once per track, 1.94). */
    if(d.playing){if(!p->play_since)p->play_since=now;else if(now-p->play_since>5000)p->stall_eps=0;}
    else p->play_since=0;
    /* 1.79: hardware 2026-11-13: EP136 had the player's track as 7400 ms long; the deck ran to
     * that end and stopped, and 1.78 took it for an ignored PLAY (pause, reload, same length).
     * At its own end the deck cannot follow: say so once and leave it (no reload loop). */
    if((act&XP_PLAY)&&d.loaded&&d.length_ms&&d.elapsed_ms+400>=d.length_ms&&target>d.length_ms+1000.0) {
        if(!p->at_end_logged){p->at_end_logged=1;
            XD_LOG("xplay: deck %u is at the end of its track as the OPUS has it (%u ms), the player is at %.0f ms: not following",deck,d.length_ms,target);}
        p->play_tries=0;act&=~(unsigned)XP_PLAY;
    } else if(d.playing)p->at_end_logged=0;
    if(act&XP_PLAY){
        /* 1.77: hardware 2026-11-13: after the first load the deck ignored PLAY for 25 s; loading
         * the track again (PRO DJ LINK off/on) fixed it. After 4 ignored PLAYs: load it again. */
        /* 1.78: the deck took PLAY (EP136 then refused a reload: "load protection") but did not
         * move. 3rd try: pause first, then play; after 6: pause and reload 0.5 s later. */
        ++p->play_tries;
        if(p->play_tries==3){p->stall_eps++;
            if(p->stall_eps<2){if(api->deck_pause)api->deck_pause(deck);XD_LOG("xplay: deck %u ignores PLAY: pause, then play again",deck);return 1;}
            p->play_tries=7;}                    /* 1.96: second episode: reload below */
        if(p->play_tries>6){p->play_tries=0;p->stall_eps=0;if(api->deck_pause)api->deck_pause(deck);
            /* 1.94: hardware 2026-11-15: deck 3 had lost its track (0 ms), the reload did not
             * take (EP136 load lock), then roots 0/5 gave "media file" errors. Reload only once
             * per player track; after that leave the deck alone and say so. */
            static uint32_t reloaded_track[2];
            if(reloaded_track[deck-3]==x->track){
                if(!p->reload_given_up){p->reload_given_up=1;
                    XD_LOG("xplay: deck %u still ignores PLAY after a reload: not reloading again (load the track by hand or switch PRO DJ LINK off/on)",deck);}
                return 1;
            }
            reloaded_track[deck-3]=x->track;p->reload_given_up=0;
            x->track=0;x->owner=0;x->state=0;x->retry_ms=now+500;
            XD_LOG("xplay: deck %u still ignores PLAY: paused, loading the player's track again",deck);return 1;}
        if(api->deck_play)api->deck_play(deck);
        XD_LOG("xplay: deck %u plays along (moved %.0f ms, tempo %+.2f %%%s)",deck,move,tempo,p->tempo_ok?"":", not applied");}
    if(act&XP_PAUSE){if(api->deck_pause)api->deck_pause(deck);x->cmd_valid=0;x->shown_ms=now;
        XD_LOG("xplay: deck %u paused (%s, jumps %u)",deck,player_playing?"jumped":"player stopped",p->s.jumps);}
    static uint64_t last_log[2];
    if(d.playing&&now-last_log[deck-3]>=5000){last_log[deck-3]=now;
        XD_LOG("xplay: deck %u at %u of %u ms, player %.0f, off %.0f, deck pitch %+.2f %% (sent %+.2f %%, player %+.2f %%, bend %+.3f), trims %u jumps %u, muted periods %u",deck,d.elapsed_ms,d.length_ms,target,p->s.off_f,d.pitch_x100/100.0,p->s.sent_pct,player_pitch_pct,p->last_bend,p->s.trims,p->s.jumps,xp_muted_periods);}
    return player_playing||d.playing||(act&XP_PAUSE);
}
#endif
#endif

/* 1.47: Pro DJ Link players 3/4 shown on OPUS decks 3/4, the native way.
 *
 * The OPUS draws a waveform only for a track loaded on one of its decks, and TYTO's side
 * screens capture those native waveforms. So when link player 3 or 4 (the XDJ) has a
 * rekordbox track from its own USB/SD, the mod loads that same track on OPUS deck 3/4 from
 * the player's LINK media (EP136 already mounts it as /media/playerNN/...), keeps the deck
 * paused, and moves its play position with the player (beat grid + beat packets). EP136
 * then shows the lane, beat grid, cues and phrase colours itself.
 *
 * EP136 1.32 pieces (static RE 2026-11-12, notes in memory opus-prodjlink):
 *  - trackid::MediaID (64 bit) = LinkMemberID word | partition << 32 | ... built by
 *    0xe66ae0(StorageManager vt 0x2470688, {member word, partition}); the manager keeps
 *    8 rows of 80 bytes per member number at +0xb0 + number*0x280 (+0 source, +4 used,
 *    +8 path, +0x2c sub). Local USB = member word 1, link player n = (n-1)<<8 | 2.
 *  - TrackLoader (vt 0x2328f80) per deck from the ObjectMap at 0x34f84a0, key
 *    0x34f401b2227e9700 + deck - 1; 0x8a7478(loader, TrackID {media, id}, hierarchy
 *    {root, rows}, sort, order) posts EP136's own load task. root = rekordbox menu id
 *    (4 = all tracks, 5 = playlist, 17 = folder); rows may be empty.
 *  - dj_player::PlayPositionControlFacade (vt 0x24b6178, one per deck, ObjectMap at
 *    0x34e94d0: 101 buckets of {key, facade, next}); slot 2 0x11200f0 =
 *    adjustPausePosition(this x0, double ms d0, PauseBehavior w1, bool w2): moves a PAUSED
 *    deck BY ms (0x11efe08 returns at |ms| < eps), like the jog in pause.
 *  - The deck widgets' master flag: 0x1bad900 (per-deck refresh, from 0x1bced50) sets
 *    +0x590 = 1 if the MasterPost's deck is this widget's deck (+0x8a8, 0-3), 2 if another
 *    member is master, 0 if none. TYTO's screens read the same field. With an external
 *    master every deck gets 2; option xmaster marks the deck that shows that player. */
#ifndef PRODJLINK_XDECK_H
#define PRODJLINK_XDECK_H
#include <math.h>

struct xd_deck {
    unsigned owner,slot;uint32_t track;uint64_t media;      /* what we asked EP136 to load */
    unsigned state,attempt;uint64_t req_ms,retry_ms;         /* state: 0 idle 1 loading 2 shown 3 gave up */
    char before_title[128];uint32_t before_len;unsigned before_loaded;
    /* 1.59: the deck's own track before PRO DJ LINK took it, loaded again when link is turned off */
    unsigned linked,prev_saved,restore,eject;uint64_t prev_media,prev_track,restore_ms;
    /* position follower */
    double anchor,rate;uint64_t anchor_t;unsigned beat_count_seen,playing,anchored;uint32_t status_beat_seen;
    double cmd,last_off,off_sum,check_target;unsigned off_n,cmd_valid,mismatch,checks,resyncs,moves;uint64_t shown_ms,last_move_ms,last_check_ms,resync_until;
    /* 1.86: the player's loop on the deck */
    unsigned lp_phase,lp_failed,lp_snapped,lp_have_prev,lp_ev;uint32_t lp_bprev,lp_cur_beat,lp_bin,lp_bout,lp_prev_bin,lp_prev_bout,lp_ev_bin,lp_ev_bout,lp_pend_bin,lp_pend_bout,lp_bp_count;double lp_last_t,lp_in,lp_out,lp_from,lp_prev_in,lp_prev_out,lp_ev_in,lp_ev_out;uint64_t lp_ev_ms,lp_loop_since,lp_bconst_ms,lp_bp_ms;double lp_pend_in,lp_pend_out,lp_bp_int,lp_beat_len,lp_peak;uint64_t lp_last_ms,lp_since,lp_jump_ms,lp_move_ms;
};
static struct xd_deck xd[2];
static uint64_t xd_facade[4];static unsigned xd_facades_found;
static _Atomic unsigned xd_master_deck;          /* 3/4: that deck shows the network master */
static void *xd_master_orig;static unsigned xd_master_marks,xd_lines;
#define XD_LOG(...) do{if(xd_lines<200){xd_lines++;lp_log(__VA_ARGS__);}}while(0)

/* ---- the deck master flag (GUI model, read by the deck lanes and TYTO's screens) ---- */
static void xd_master_hook(uint64_t self) {
    ((void (*)(uint64_t))xd_master_orig)(self);
    unsigned d=__atomic_load_n(&xd_master_deck,__ATOMIC_ACQUIRE);
    if(!d)return;
    unsigned char *w=(unsigned char *)(uintptr_t)self;
    /* 1.48: also from 0 ("no master"): EP136's widget for that deck may not know any master */
    int32_t v=*(int32_t *)(w+0x590);
    if(w[0x8a8]==d-1&&(v==2||v==0)){*(int32_t *)(w+0x590)=1;xd_master_marks++;}
}

/* ---- MediaID of another player's USB/SD (as the core's nb_media does for local USB) ---- */
static uint64_t xd_media(unsigned owner,unsigned slot) {
    if(owner<1||owner>8)return 0;
    uintptr_t mapped[2]={0,((uint64_t (*)(const char *))0x612398)((const char *)0x2277b88)};
    if(!((unsigned (*)(void *,unsigned))0x7661f0)(mapped,0)||!mapped[0])return 0;
    unsigned char *manager=(unsigned char *)mapped[0];uint64_t vt=0;
    if(!nt_peek((uint64_t)(uintptr_t)manager,(unsigned char *)&vt,8)||vt!=0x2470688)return 0;
    static unsigned said[9];
    unsigned source=0,sub=0,matches=0,used_rows=0,only_src=0,only_sub=0;
    ((void (*)(void *))0x1f3f920)(manager+0x80);
    for(unsigned k=0;k<8;k++) {
        const unsigned char *row=manager+0xb0+owner*0x280+k*80;
        uint32_t src=*(const uint32_t *)row,used=*(const uint32_t *)(row+4);
        uint64_t pp=*(const uint64_t *)(row+8);char path[64]={0};
        if(!used||!pp||!nt_peek(pp,(unsigned char *)path,sizeof path-1))continue;
        if(said[owner]<16){said[owner]++;XD_LOG("xdeck: player %u storage row %u: source %u sub %u path '%s'",owner,k,src,row[0x2c],path);}
        /* slot 3 = USB, 2 = SD (status 0x29) */
        int usb=strstr(path,"usb")!=0,sd=strstr(path,"sd")!=0&&!usb;
        if((slot==3&&usb)||(slot==2&&sd)){source=src;sub=row[0x2c];matches++;}
        used_rows++;only_src=src;only_sub=row[0x2c];
    }
    if(!matches&&used_rows==1){source=only_src;sub=only_sub;matches=1;}   /* path names unknown: the one row */
    ((void (*)(void *))0x1f3f950)(manager+0x80);
    if(matches!=1)return 0;
    unsigned char number=(unsigned char)owner;
    uint32_t member=((unsigned (*)(const void *,unsigned))0x10bc0a0)(&number,0);
    unsigned partition=((unsigned (*)(unsigned,unsigned))0xe5fd38)(source,sub);
    uint64_t identifier=member|((uint64_t)(partition&255)<<32);
    uint64_t media=((uint64_t (*)(void *,const void *))0xe66ae0)(manager,&identifier);
    if((uint32_t)media!=member||!((media>>32)&255))return 0;
    return media;
}
/* ---- the deck's TrackLoader (core nb_loader, without its R51 install gate) ---- */
static void *xd_loader(unsigned deck) {
    if(deck<1||deck>4||!__atomic_load_n((unsigned char *)0x34f8498,__ATOMIC_ACQUIRE))return 0;
    void *mutex=(void *)0x34f84c8;
    if(!((unsigned (*)(void *))0x1581610)(mutex))return 0;
    const unsigned char *map=(const unsigned char *)0x34f84a0;
    int count=*(const int *)(map+24);uintptr_t iface=0;
    uint64_t key=0x34f401b2227e9700ull+deck-1;
    if(count>0&&count<=100000) {
        uintptr_t buckets=*(const uintptr_t *)(map+8);
        uintptr_t node=buckets?*(const uintptr_t *)(buckets+8*(key%(unsigned)count)):0;
        for(unsigned n=0;node&&n<1024;n++) {
            if(*(const uint64_t *)node==key){iface=*(const uintptr_t *)(node+8);break;}
            node=*(const uintptr_t *)(node+16);
        }
    }
    ((void (*)(void *))0x5e6ec8)(mutex);
    if(!iface)return 0;
    uintptr_t primary=iface-0xd8;
    if(*(uintptr_t *)iface!=0x2329070ul||*(uintptr_t *)primary!=0x2328f80ul)return 0;
    uintptr_t ctrl=*(uintptr_t *)(primary+0xe8);
    if(!ctrl||*(uintptr_t *)ctrl!=0x2316dc8ul)return 0;
    return (void *)primary;
}
/* 1.84: our own USB as a loader media id. A deck holding a local track shows it (TrackLoader
 * +0x108, low word 1); else the value seen for local tracks on hardware (media bytes
 * 01 00 00 00 02 01 01 00, the link form 02 02 .. with the same upper half). */
static uint64_t xd_local_media(void) {
    for(unsigned deck=1;deck<=4;deck++) {
        void *l=xd_loader(deck);if(!l)continue;
        uint64_t m=*(const uint64_t *)((uintptr_t)l+0x108);
        if((uint32_t)m==1u&&(m>>32))return m;
    }
    return 0x0001010200000001ull;
}
static const uint16_t xd_roots[3]={4,0,5};   /* all tracks, none, playlist: tried in turn */
static void xd_load(void *loader,uint64_t media,uint32_t track,unsigned attempt) {
    uint64_t id[2]={media,track};
    struct {uint16_t root;unsigned char pad[6];uintptr_t begin,end,capacity;} h={xd_roots[attempt%3],{0},0,0,0};
    uint32_t sort=0;unsigned char order=0;
    ((uintptr_t (*)(void *,const void *,const void *,const void *,const void *))0x8a7478)(loader,id,&h,&sort,&order);
}
/* ---- PlayPositionControlFacade per deck ---- */
static void xd_find_facades(void) {
    unsigned char ready=0;if(!nt_peek(0x34e94c8,&ready,1)||!(ready&1))return;
    uint64_t buckets=0;int32_t count=0;
    if(!nt_peek(0x34e94d8,(unsigned char *)&buckets,8)||!nt_peek(0x34e94e8,(unsigned char *)&count,4)||!buckets||count<=0||count>4096)return;
    uint64_t key[16],obj[16];unsigned n=0;
    for(int b=0;b<count;b++) {
        uint64_t node=0;nt_peek(buckets+8u*(unsigned)b,(unsigned char *)&node,8);
        for(unsigned hop=0;node&&hop<64;hop++) {
            uint64_t f[3]={0},vt[2]={0};if(!nt_peek(node,(unsigned char *)f,24))break;
            if(f[1]&&nt_peek(f[1],(unsigned char *)vt,16)&&vt[0]==0x24b6178&&vt[1]==0x24b61c8&&n<16){key[n]=f[0];obj[n]=f[1];n++;}
            node=f[2];
        }
    }
    /* keys of one family end in the deck index 0..3 */
    unsigned found=0;uint64_t pick[4]={0};
    for(unsigned i=0;i<n;i++){unsigned k=(unsigned)(key[i]&255);if(k<4){unsigned same=0;for(unsigned j=0;j<n;j++)if((key[j]>>8)==(key[i]>>8)&&(key[j]&255)<4)same++;if(same==4&&!pick[k]){pick[k]=obj[i];found++;}}}
    static unsigned logged;
    if(!logged||found==4){logged=1;for(unsigned i=0;i<n;i++)XD_LOG("xdeck: position facade key %016llx at %llx",(unsigned long long)key[i],(unsigned long long)obj[i]);}
    if(found!=4&&n==4) {   /* no deck-index pattern in the keys: key order */
        for(unsigned i=0;i<4;i++){unsigned r=0;for(unsigned j=0;j<4;j++)if(key[j]<key[i])r++;pick[r]=obj[i];}
        found=4;XD_LOG("xdeck: position facades taken in key order");
    }
    if(found==4){memcpy(xd_facade,pick,sizeof pick);xd_facades_found=1;XD_LOG("xdeck: position facades for decks 1-4 found");}
}
static int xd_move(unsigned deck,double ms) {
    if(!xd_facades_found||deck<1||deck>4||!xd_facade[deck-1])return 0;
    if(*(const uint32_t *)0x11200f0!=0xa9b07bfdu)return 0;
    /* EP136 moves by (long)(rate * ms / 1000) samples (truncated). Equal moves would all
     * lose the same fraction (up to 20 ms a minute), so a random 0-1 sample of dither
     * (at 44.1 kHz) is added: the truncation then averages out. */
    static uint32_t seed=0x9e3779b9u;seed=seed*1664525u+1013904223u;
    double u=(double)(seed>>8)/16777216.0/44.1;
    ms+=ms>0?u:-u;
    ((void (*)(uint64_t,double,unsigned,unsigned))0x11200f0)(xd_facade[deck-1],ms,0,0);
    return 1;
}
/* ---- where the player is in its track: grid + status beat + beat packets (lk_lock held) ---- */
static int xd_grid_index(const struct lkdb_beat *g,int n,double at) {
    int lo=0,hi=n;while(lo<hi){int m=(lo+hi)/2;if((double)g[m].time_ms<at)lo=m+1;else hi=m;}
    return lo;
}
static int xd_target(struct xd_deck *x,const struct lk_player *pl,uint64_t now,double *out) {
    if(!pl->grid||pl->grid_track!=pl->track_id||pl->grid_n<2)return 0;
    const struct lkdb_beat *g=pl->grid;int n=pl->grid_n;
    double f=1.0+pl->pitch_x100/10000.0;if(f<0.0)f=0.0;
    uint32_t B=pl->beat;int bi=B>=1&&B<=(uint32_t)n?(int)B-1:-1;
    if(!x->anchored) {
        if(bi<0)return 0;
        x->anchor=g[bi].time_ms;x->anchor_t=pl->last_status_ms;x->status_beat_seen=B;
        x->beat_count_seen=pl->beat_count;x->playing=pl->playing;x->rate=f;x->anchored=1;
    }
    if(pl->playing!=x->playing) {   /* start/stop, seen up to 200 ms late: assume mid-way */
        uint64_t at=pl->last_status_ms>100?pl->last_status_ms-100:pl->last_status_ms;
        if(at<x->anchor_t)at=x->anchor_t;
        x->anchor+=x->playing?(double)(int64_t)(at-x->anchor_t)*x->rate:0.0;x->anchor_t=at;x->playing=pl->playing;
    } else if(x->playing&&fabs(f-x->rate)>1e-6) {
        x->anchor+=(double)(int64_t)(now-x->anchor_t)*x->rate;x->anchor_t=now;
    }
    x->rate=f;
    /* a beat packet: that beat is the grid beat with the same bar position nearest the estimate */
    if(pl->beat_count!=x->beat_count_seen) {
        x->beat_count_seen=pl->beat_count;
        if(x->playing&&pl->beat_in_bar>=1&&pl->beat_in_bar<=4) {
            double at=x->anchor+(double)((int64_t)pl->last_beat_ms-(int64_t)x->anchor_t)*f;
            int c=xd_grid_index(g,n,at),best=-1;double bd=1e18;
            for(int i=c-5;i<=c+5;i++)if(i>=0&&i<n&&g[i].beat_in_bar==pl->beat_in_bar){double d=fabs((double)g[i].time_ms-at);if(d<bd){bd=d;best=i;}}
            if(best>=0&&bd<1500.0){x->anchor=g[best].time_ms;x->anchor_t=pl->last_beat_ms;}
        }
    }
    /* the status beat moved: a jump (cue, hot cue, search) or the paused position changed */
    if(B!=x->status_beat_seen) {
        x->status_beat_seen=B;
        if(bi>=0) {
            double at=x->anchor+(x->playing?(double)((int64_t)pl->last_status_ms-(int64_t)x->anchor_t)*f:0.0);
            double len=bi+1<n?(double)g[bi+1].time_ms-g[bi].time_ms:500.0;
            if(!x->playing||at<g[bi].time_ms-len*0.5||at>g[bi].time_ms+len*1.5){x->anchor=g[bi].time_ms;x->anchor_t=pl->last_status_ms;}
        }
    }
    double est=x->anchor+(x->playing?(double)(int64_t)(now-x->anchor_t)*f:0.0);
    *out=est<0.0?0.0:est;
    return 1;
}

#include "prodjlink_xplay.h"
/* One follower step (pure, unit-tested in prodjlink-lab/xdtest): adjustPausePosition moves
 * BY a delta (a jog in pause), so the mod tracks the position it commanded (cmd) and sends
 * target - cmd. Every 500 ms the deck's own position (it may lag while moving) is compared:
 *  - player still and no move for 400 ms: the deck is still too, so cmd = its position;
 *  - otherwise 3 misses over 400 ms in a row: stop moving for 300 ms (moves in flight
 *    land), then cmd = its position.
 * off_sum/off_n/resyncs go to the log (rounding/lag calibration on hardware).
 * Returns 1 when following starts, 2 after a check, else 0; *delta = the move to send now. */
static int xd_follow_step(struct xd_deck *x,double target,uint32_t elapsed,uint64_t now,double *delta) {
    *delta=0;int r=0;
    if(!x->cmd_valid) {
        if(now-x->shown_ms<800)return 0;   /* let the load settle */
        x->cmd=elapsed;x->cmd_valid=1;x->mismatch=0;x->last_check_ms=now;x->check_target=target;r=1;
    }
    if(x->resync_until) {
        if(now<x->resync_until)return 0;
        x->cmd=elapsed;x->resync_until=0;x->resyncs++;x->last_check_ms=now;x->check_target=target;
    }
    if(now-x->last_check_ms>=500) {
        double off=(double)elapsed-x->cmd;x->last_off=off;x->off_sum+=off;x->off_n++;
        int still=fabs(target-x->check_target)<0.5&&now-x->last_move_ms>=400;
        x->last_check_ms=now;x->check_target=target;x->checks++;r=2;
        if(still){x->mismatch=0;if(fabs(off)>=2.0){x->cmd=elapsed;x->resyncs++;}}
        else {
            x->mismatch=fabs(off)>400.0?x->mismatch+1:0;   /* moving: the deck reports late */
            if(x->mismatch>=3){x->mismatch=0;x->resync_until=now+300;return r;}
        }
    }
    double d=target-x->cmd;
    if(fabs(d)>=1.0){*delta=d;x->cmd=target;x->last_move_ms=now;}
    return r;
}

/* 1.59: PRO DJ LINK turned off for this deck: stop following, tempo back to 0, and load the
 * deck's own track again (EP136 has no plain unload; a deck that was empty keeps the player's
 * track, paused). */
static void xd_release(const struct oqmod_api *api,unsigned deck,struct xd_deck *x,uint64_t now) {
    struct xp_deck *p=&xpd[deck-3];
    if(x->state>=1){if(api->deck_pause)api->deck_pause(deck);if(p->tempo_ok)xp_tempo_change(deck,0.0f);}
    memset(p,0,sizeof *p);
    x->restore=x->prev_saved&&x->prev_track&&x->state>=1;x->restore_ms=now+300;
    x->eject=!x->restore&&x->state>=1;   /* 1.78: nothing to go back to: empty the deck */
    XD_LOG("xdeck: deck %u released (was state %u)%s",deck,x->state,x->restore?", loading its own track again":x->eject?", ejecting the player's track":"");
    x->linked=0;x->prev_saved=0;x->state=0;x->owner=0;x->slot=0;x->track=0;x->media=0;x->cmd_valid=0;x->retry_ms=0;
}
/* 1.78: the deck header's EJECT (gui::waveform::DeckInfoFieldWidget "ejectBtn") calls
 * usecase::load::TrackLoader 0x8a6b80(loader, 0, 1) (seen on hardware 2026-11-13, from
 * 0x1697e80). Same call here when PRO DJ LINK is switched off and the deck had no own track. */
static void xd_eject_step(const struct oqmod_api *api,unsigned deck,struct xd_deck *x,uint64_t now) {
    if(now<x->restore_ms)return;
    struct oqmod_deck d;memset(&d,0,sizeof d);api->deck(deck,&d);
    if(d.playing){if(api->deck_pause)api->deck_pause(deck);x->restore_ms=now+300;return;}
    void *loader=xd_loader(deck);
    if(!loader){if(now-x->restore_ms>5000)x->eject=0;return;}
    x->eject=0;
    /* 1.79: 1.78 checked the first instruction, but the research hook (loadtrace) had already
     * patched it, so the check failed ("eject call not found"). Through the hook's trampoline
     * when it is installed, else directly. */
    void *fn=lt_orig[2];
    if(!fn) {
        if(*(const uint32_t *)0x8a6b80!=0xa9b37bfdu){XD_LOG("xdeck: deck %u: eject call not found",deck);return;}
        fn=(void *)0x8a6b80;
    }
    ((void (*)(void *,unsigned,unsigned))fn)(loader,0,1);
    XD_LOG("xdeck: deck %u ejected (PRO DJ LINK off)",deck);
}
static void xd_restore_step(const struct oqmod_api *api,unsigned deck,struct xd_deck *x,uint64_t now) {
    if(now<x->restore_ms)return;
    struct oqmod_deck d;memset(&d,0,sizeof d);api->deck(deck,&d);
    if(d.playing){if(api->deck_pause)api->deck_pause(deck);x->restore_ms=now+300;return;}
    void *loader=xd_loader(deck);
    if(!loader){if(now-x->restore_ms>5000)x->restore=0;return;}
    xd_load(loader,x->prev_media,(uint32_t)x->prev_track,0);x->restore=0;
    XD_LOG("xdeck: deck %u: own track %llu loaded again",deck,(unsigned long long)x->prev_track);
}
/* 1.86: loops (user: "loops that i do on the fly ... the waveform loops but i dont see the orange
 * markers"). The XDJ-1000MK2 says only that it loops (status 0x7b = 4), not where; its position
 * jumps back at the loop end. The deck gets the same loop the way its own buttons make one
 * (hardware trace 2026-11-13, deck 1): LOOP IN = dj_player::CueLoopSetFacade slot 2 (0x110aa80,
 * args 0,0), LOOP OUT = slot 9 (0x110e5f0, 0,0), EXIT = CueLoopControlFacade slot 3 (0x11004d0).
 * Per deck the two facades sit 0x240 and 0x300 before its PlayPositionControlFacade (decks 1
 * and 3, two boots). After the player's first jump back: LOOP IN when the deck is at the loop
 * start, LOOP OUT when it reaches the point the player jumped from; EXIT when the player stops
 * looping. Option xloop (default 1). */
static int xd_loop_call(unsigned deck,unsigned which) {   /* 0 in, 1 out, 2 exit, 3 reloop, 4 clear the deck's loop */
    if(deck<1||deck>4||!xd_facade[deck-1])return 0;
    uint64_t pos=xd_facade[deck-1],obj=0,vt=0,want_vt,want_fn;unsigned slot;
    if(which==4) {   /* 1.88: dj_player::CueLoadFacade (vt 0x24b02f8) claerActiveLoop, slot 5 0x1102168 (deck 1: position facade - 0x180) */
        want_vt=0x24b02f8;want_fn=0x1102168;slot=5;
        for(uint64_t o=0x180;o<=0x600&&!obj;o+=8){uint64_t v=0;if(nt_peek(pos-o,(unsigned char *)&v,8)&&v==want_vt)obj=pos-o;}
        if(!obj)return 0;
    } else {
        obj=pos-(which==2||which==3?0x300u:0x240u);
        want_vt=which==2||which==3?0x24af1c8ull:0x24b0bc0ull;
        slot=which==0?2u:which==1?9u:which==2?3u:4u;
        want_fn=which==0?0x110aa80ull:which==1?0x110e5f0ull:which==2?0x11004d0ull:0x10f8420ull;
    }
    if(!nt_peek(obj,(unsigned char *)&vt,8)||vt!=want_vt)return 0;
    uint64_t fn=0;if(!nt_peek(vt+8*slot,(unsigned char *)&fn,8)||fn!=want_fn)return 0;
    if(which==2||which==4)((void (*)(uint64_t))fn)(obj);
    else if(which==3)((void (*)(uint64_t,unsigned))fn)(obj,0);
    else ((void (*)(uint64_t,unsigned,unsigned))fn)(obj,0,0);
    return 1;
}
/* 1.89 (hardware 1.88: size changes, RELOOP and loops over earlier ones still wrong, one loop in the
 * wrong place). 1.87/1.88 learned the loop from estimated positions; when those came out wrong the
 * loop was dropped without a word. Now from the player's beat number (status 0xa0): looping, it
 * goes from the loop's last beat back to its first, so the loop is first beat .. last beat + 1 on
 * the player's grid, exactly, at the first jump (no waiting). A jump between other beats while
 * the deck loops = the loop changed size or place: set again. A jump back to the first beat of
 * the last loop from beyond its end = RELOOP: the deck's own RELOOP. Placing as in 1.87 (old
 * loop cleared, pause, LOOP IN at the start, LOOP OUT at the end, play-along restarts).
 * Phases: 2 move to start, 3 LOOP IN, 4 move to end, 5 LOOP OUT, 6 deck loops. */
static void xd_loop_place(unsigned deck,struct xd_deck *x,uint32_t bin,uint32_t bout,double in,double out,uint64_t now,const char *why) {
    x->lp_in=in;x->lp_out=out;x->lp_bin=bin;x->lp_bout=bout;x->lp_phase=2;x->lp_snapped=1;x->lp_since=now;x->lp_loop_since=now;
    XD_LOG("xloop: deck %u: %s: beats %u-%u, %.0f-%.0f ms (%.0f ms)",deck,why,bin,bout,in,out,out-in);
}
/* 1.90: a one-beat loop never sends the beat number back: the beat number stays the same for
 * 1.6 beats while beat packets keep coming one beat apart. (Beat packets half a beat apart: a
 * loop shorter than a beat, not shown.) True when the deck's loop is not already that beat. */
static int xd_loop_one_beat(unsigned deck,struct xd_deck *x,uint64_t now) {
    if(!x->lp_cur_beat||x->lp_beat_len<100.0||!x->lp_pend_out||!x->lp_bconst_ms)return 0;
    if((double)(now-x->lp_bconst_ms)<x->lp_beat_len*1.6||now-x->lp_bp_ms>(uint64_t)(x->lp_beat_len*1.5))return 0;
    if(x->lp_phase==6&&x->lp_bin==x->lp_cur_beat&&x->lp_bout==x->lp_cur_beat+1)return 0;
    double r=x->lp_bp_int/x->lp_beat_len;
    if(r<0.8||r>1.2) {
        static unsigned said;if(r>0.3&&r<0.7&&said<6){said++;XD_LOG("xloop: deck %u: beat packets %.0f ms apart (beat %.0f ms): a loop shorter than a beat, not shown",deck,x->lp_bp_int,x->lp_beat_len);}
        return 0;
    }
    return 1;
}
static void xd_loop_step(const struct oqmod_api *api,unsigned deck,struct xd_deck *x,int looping,double target,double rate,uint64_t now) {
    (void)rate;
    x->lp_last_t=target;x->lp_last_ms=now;   /* 1.90: where the player is, for the turn point */
    if(target>x->lp_peak)x->lp_peak=target;
    struct oqmod_deck d;memset(&d,0,sizeof d);api->deck(deck,&d);
    int ev=x->lp_ev&&now-x->lp_ev_ms<800;
    if(!looping) {
        if(x->lp_phase==6){int ok=xd_loop_call(deck,2);XD_LOG("xloop: deck %u: player left its loop, EXIT %s",deck,ok?"sent":"not possible");
            x->lp_prev_in=x->lp_in;x->lp_prev_out=x->lp_out;x->lp_prev_bin=x->lp_bin;x->lp_prev_bout=x->lp_bout;x->lp_have_prev=1;}
        else if(x->lp_phase>=2&&x->lp_phase<=5)XD_LOG("xloop: deck %u: player left its loop before it was placed",deck);
        x->lp_phase=0;x->lp_failed=0;x->lp_loop_since=0;if(!ev)x->lp_ev=0;return;
    }
    if(!x->lp_loop_since)x->lp_loop_since=now;
    if(x->lp_phase>=2&&x->lp_phase<=5) {
        if(now-x->lp_since>2500){XD_LOG("xloop: deck %u: loop not placed (phase %u, deck at %u ms)",deck,x->lp_phase,d.elapsed_ms);x->lp_phase=0;x->lp_ev=0;return;}
    } else if(x->lp_phase==6) {
        if(ev) {
            x->lp_ev=0;
            if(x->lp_ev_bin==x->lp_bin&&x->lp_ev_bout==x->lp_bout){x->lp_pend_bin=0;return;}   /* a normal turn */
            /* 1.90: a different turn: the loop is set again only when the next turn is the same
             * (right after halving the player can jump from beyond the new end) */
            if(x->lp_pend_bin!=x->lp_ev_bin||x->lp_pend_bout!=x->lp_ev_bout){x->lp_pend_bin=x->lp_ev_bin;x->lp_pend_bout=x->lp_ev_bout;
                XD_LOG("xloop: deck %u: player turned at beats %u-%u (deck loop %u-%u): checking the next turn",deck,x->lp_ev_bin,x->lp_ev_bout,x->lp_bin,x->lp_bout);return;}
            x->lp_pend_bin=0;
            xd_loop_call(deck,2);
            xd_loop_place(deck,x,x->lp_ev_bin,x->lp_ev_bout,x->lp_ev_in,x->lp_ev_out,now,"player's loop changed");
            return;
        }
        if(xd_loop_one_beat(deck,x,now)){xd_loop_call(deck,2);xd_loop_place(deck,x,x->lp_cur_beat,x->lp_cur_beat+1,x->lp_pend_in,x->lp_pend_out,now,"player loops one beat");return;}
        if(x->lp_cur_beat>x->lp_bout) {   /* the player plays past the deck's loop end: longer now */
            xd_loop_call(deck,2);x->lp_phase=0;x->lp_loop_since=now;
            XD_LOG("xloop: deck %u: player's loop got longer (beat %u past %u): waiting for its next turn",deck,x->lp_cur_beat,x->lp_bout);
        }
        return;
    } else {   /* phase 0 */
        if(ev) {
            x->lp_ev=0;
            if(x->lp_have_prev&&x->lp_ev_bin==x->lp_prev_bin&&x->lp_ev_bout>x->lp_prev_bout) {
                int ok=xd_loop_call(deck,3);
                XD_LOG("xloop: deck %u: player reloops beats %u-%u: RELOOP %s",deck,x->lp_prev_bin,x->lp_prev_bout,ok?"sent":"not possible, placing it");
                if(ok){x->lp_in=x->lp_prev_in;x->lp_out=x->lp_prev_out;x->lp_bin=x->lp_prev_bin;x->lp_bout=x->lp_prev_bout;x->lp_phase=6;
                    xpd[deck-3].s.started=0;xpd[deck-3].s.hold_until=0;return;}
                xd_loop_place(deck,x,x->lp_prev_bin,x->lp_prev_bout,x->lp_prev_in,x->lp_prev_out,now,"reloop");
            } else if(x->lp_ev_bout>x->lp_ev_bin&&x->lp_ev_out-x->lp_ev_in>=200.0) {
                xd_loop_place(deck,x,x->lp_ev_bin,x->lp_ev_bout,x->lp_ev_in,x->lp_ev_out,now,"player loops");
            }
        } else if(xd_loop_one_beat(deck,x,now)) {
            xd_loop_place(deck,x,x->lp_cur_beat,x->lp_cur_beat+1,x->lp_pend_in,x->lp_pend_out,now,"player loops one beat");
        } else if(x->lp_loop_since&&now-x->lp_loop_since>4000&&!x->lp_failed) {
            x->lp_failed=1;XD_LOG("xloop: deck %u: the player loops but its beat never went back (a loop shorter than a beat?): not shown",deck);
        }
        if(x->lp_phase!=2)return;
    }
    if(x->lp_phase==2) {
        if(d.playing){if(api->deck_pause)api->deck_pause(deck);return;}
        int cl=xd_loop_call(deck,4);
        XD_LOG("xloop: deck %u: placing %.0f-%.0f (earlier loop %s)",deck,x->lp_in,x->lp_out,cl?"cleared":"not cleared");
        xd_move(deck,x->lp_in-(double)d.elapsed_ms);x->lp_move_ms=now;x->lp_phase=3;return;
    }
    if(x->lp_phase==3) {
        if(now-x->lp_move_ms<200)return;   /* the deck reports its position a little late */
        if(fabs((double)d.elapsed_ms-x->lp_in)>25.0){xd_move(deck,x->lp_in-(double)d.elapsed_ms);x->lp_move_ms=now;return;}
        int ok=xd_loop_call(deck,0);XD_LOG("xloop: deck %u at %u ms: LOOP IN %s",deck,d.elapsed_ms,ok?"sent":"not possible");
        if(!ok){x->lp_phase=0;return;}
        x->lp_phase=4;x->lp_move_ms=now;return;
    }
    if(x->lp_phase==4){if(now-x->lp_move_ms<150)return;xd_move(deck,x->lp_out-(double)d.elapsed_ms);x->lp_move_ms=now;x->lp_phase=5;return;}
    if(x->lp_phase==5) {
        if(now-x->lp_move_ms<200)return;
        if(fabs((double)d.elapsed_ms-x->lp_out)>25.0){xd_move(deck,x->lp_out-(double)d.elapsed_ms);x->lp_move_ms=now;return;}
        int ok=xd_loop_call(deck,1);XD_LOG("xloop: deck %u at %u ms: LOOP OUT %s",deck,d.elapsed_ms,ok?"sent":"not possible");
        x->lp_phase=ok?6:0;
        xpd[deck-3].s.started=0;xpd[deck-3].s.hold_until=0;   /* play-along puts the deck where the player is and starts it */
    }
}
/* 1.87: the deck's own loop is on: the player's position seen from inside that loop */
static double xd_loop_wrap(const struct xd_deck *x,double target,double deck_ms) {
    if(x->lp_phase!=6)return target;
    double L=x->lp_out-x->lp_in;if(L<300.0)return target;
    while(target-deck_ms>L/2)target-=L;
    while(deck_ms-target>L/2)target+=L;
    return target;
}
static void xd_tick(const struct oqmod_api *api) {
    if(!api->ep136_ok||!api->ep136_ok())return;
    static int installed;
    if(!installed&&api->hook) {
        installed=1;
        int m=api->hook(0x1bad900,0xa9bc7bfdu,(void *)xd_master_hook,&xd_master_orig)==1;
        lp_log("xdeck: deck master flag hook %d",m);
    }
    uint64_t now=lk_now_ms();
    unsigned show_master=api->setting_get("xmaster",1)?1u:0u;
    unsigned links=__atomic_load_n(&ch_link_mask,__ATOMIC_ACQUIRE);   /* 1.55: per channel */
    unsigned on=links!=0;
    static uint64_t last_find;
    if(on&&!xd_facades_found&&now-last_find>3000){last_find=now;xd_find_facades();}
    unsigned master_deck=0;
    for(unsigned i=0;i<2;i++) {
        unsigned deck=3+i;struct xd_deck *x=&xd[i];
        unsigned on=(links>>(deck-1))&1u;   /* this deck is a PRO DJ LINK deck */
        if(!on&&x->linked)xd_release(api,deck,x,now);
        if(on)x->linked=1;
        if(!on&&x->restore)xd_restore_step(api,deck,x,now);
        if(!on&&x->eject)xd_eject_step(api,deck,x,now);
        /* the link player with this number, its track and where it is */
        unsigned present=0,owner=0,slot=0,kind=0,master=0,pplaying=0,pstate=0;uint32_t track=0;double target=0,ppitch=0;int have_target=0;
        pthread_mutex_lock(&lk_lock);
        for(unsigned k=0;k<LK_MAX;k++) {
            struct lk_player *pl=&lk_players[k];
            if(!pl->ip||pl->number!=deck||now-pl->last_status_ms>5000)continue;
            present=1;owner=pl->track_player;slot=pl->slot;kind=pl->track_kind;track=pl->track_id;master=pl->master;
            pplaying=pl->playing;ppitch=pl->pitch_x100/100.0;pstate=pl->play_state;
            /* 1.89/1.90: the player's beat number going back = a loop turn (or a jump). The loop starts
             * at the first beat after the jump. It ends at the beat boundary nearest to where the
             * player was when it jumped (with quantize the player sets LOOP OUT to the nearest beat;
             * in later turns that is the end of the loop's last beat). 1.89 always took the end of
             * the last beat seen: one beat too long when LOOP OUT was pressed early in a beat (user:
             * a 1-beat loop shown as 2 beats). */
            if(x->state==2&&pl->track_id==x->track&&pl->grid&&pl->grid_track==pl->track_id&&pl->grid_n>2) {
                uint32_t B=pl->beat;const struct lkdb_beat *g=pl->grid;
                if(B>=1&&x->lp_bprev>=2&&B<x->lp_bprev&&x->lp_bprev<(uint32_t)pl->grid_n&&x->lp_peak>0) {
                    /* the furthest the player got since the last turn (the beat number reaches us up
                     * to 200 ms after the jump, when the position estimate has already gone back) */
                    double from=x->lp_peak;x->lp_peak=0;
                    double a=g[x->lp_bprev-1].time_ms,b=g[x->lp_bprev].time_ms;
                    uint32_t bout=from-a<b-from?x->lp_bprev:x->lp_bprev+1;
                    if(bout<=B)bout=B+1;
                    x->lp_ev=1;x->lp_ev_ms=now;x->lp_ev_bin=B;x->lp_ev_bout=bout;
                    x->lp_ev_in=g[B-1].time_ms;x->lp_ev_out=g[bout-1].time_ms;
                }
                if(B!=x->lp_cur_beat)x->lp_bconst_ms=now;
                if(B&&B<(uint32_t)pl->grid_n)x->lp_bprev=B;   /* 1.91: never 0xffffffff (loading) */
                x->lp_cur_beat=B;
                if(B>=1&&B<(uint32_t)pl->grid_n)x->lp_beat_len=g[B].time_ms-g[B-1].time_ms;
                if(pl->beat_count!=x->lp_bp_count){   /* beat packets: their spacing while looping */
                    if(x->lp_bp_ms&&pl->last_beat_ms>x->lp_bp_ms)x->lp_bp_int=(double)(pl->last_beat_ms-x->lp_bp_ms);
                    x->lp_bp_ms=pl->last_beat_ms;x->lp_bp_count=pl->beat_count;
                }
                /* 1.91: the player reports beat 0xffffffff while it loads a track; 1.90 read the grid at
                 * that index and crashed EP136 (hardware 2026-11-15, uptime 251 s, XDJ state 0x7b=02). */
                {uint32_t pi=B>=1?B-1:0;
                 x->lp_pend_in=pi<(uint32_t)pl->grid_n?g[pi].time_ms:0;x->lp_pend_out=B>=1&&B<(uint32_t)pl->grid_n?g[B].time_ms:0;}   /* the 1-beat candidate */
            }
            if(x->state==2&&x->track==track)have_target=xd_target(x,pl,now,&target);
        }
        pthread_mutex_unlock(&lk_lock);
        /* 1.48: silence the deck's own audio while it mirrors a player (option xmute) */
        unsigned mute=on&&present&&x->state==2&&api->setting_get("xmute",1);
        if(mute&&xp_mute_install()){__atomic_or_fetch(&xp_mute_mask,1u<<(deck-1),__ATOMIC_RELAXED);}
        else {__atomic_and_fetch(&xp_mute_mask,~(1u<<(deck-1)),__ATOMIC_RELAXED);}
        if(!on||!present||!track||kind!=1){if(x->state==1)x->state=0;continue;}
        if(present&&master&&x->state==2&&show_master)master_deck=deck;
        /* the player's track: load it on our deck once */
        if(owner!=x->owner||slot!=x->slot||track!=x->track) {
            if(now<x->retry_ms)continue;
            x->retry_ms=now+2000;
            int own=LK_PLAYER&&owner==LK_PLAYER;   /* 1.84: the XDJ plays a track from our USB (LINK) */
            if(!own&&(owner<3||owner>8)) {
                static unsigned said;if(said<4){said++;XD_LOG("xdeck: player %u plays track %u from player %u's media: not shown on deck %u",deck,track,owner,deck);}
                continue;
            }
            struct oqmod_deck d;memset(&d,0,sizeof d);api->deck(deck,&d);
            if(d.playing&&x->state==2&&xpd[deck-3].s.started) {   /* our play-along: stop it, load next tick */
                if(api->deck_pause){api->deck_pause(deck);}
                xpd[deck-3].s.started=0;x->retry_ms=now+500;continue;
            }
            if(d.playing){XD_LOG("xdeck: deck %u is playing, player %u's track not loaded",deck,deck);continue;}
            uint64_t media=own?xd_local_media():xd_media(owner,slot);
            void *loader=xd_loader(deck);
            if(!media||!loader){XD_LOG("xdeck: player %u track %u slot %u: media %016llx loader %p, not loaded yet",deck,track,slot,(unsigned long long)media,loader);continue;}
            if(!x->prev_saved) {   /* 1.59: remember the deck's own (local USB) track */
                x->prev_saved=1;x->prev_media=*(const uint64_t *)((uintptr_t)loader+0x108);x->prev_track=*(const uint64_t *)((uintptr_t)loader+0x110);
                if(!d.loaded||(uint32_t)x->prev_media!=1u||!x->prev_track)x->prev_media=x->prev_track=0;
                XD_LOG("xdeck: deck %u had %s before PRO DJ LINK",deck,x->prev_track?d.title:"no local track");
            }
            x->owner=owner;x->slot=slot;x->track=track;x->media=media;x->attempt=0;x->anchored=0;x->moves=0;x->cmd_valid=0;x->checks=0;
            snprintf(x->before_title,sizeof x->before_title,"%s",d.title);x->before_len=d.length_ms;x->before_loaded=(unsigned)d.loaded;
            xd_load(loader,media,track,0);x->state=1;x->req_ms=now;
            w3_cue_want(slot,track);   /* 1.81 research */
            XD_LOG("xdeck: loading player %u's track %u (media %016llx, root %u) on deck %u",deck,track,(unsigned long long)media,xd_roots[0],deck);
            continue;
        }
        if(x->state==1) {
            struct oqmod_deck d;memset(&d,0,sizeof d);api->deck(deck,&d);
            int changed=d.loaded&&d.length_ms&&(!x->before_loaded||d.length_ms!=x->before_len||strncmp(d.title,x->before_title,sizeof x->before_title-1));
            if(changed||(d.loaded&&d.length_ms&&now-x->req_ms>6000&&x->before_loaded)) {
                x->state=2;x->shown_ms=now;x->cmd_valid=0;XD_LOG("xdeck: deck %u now shows '%s' (%u ms) after %llu ms",deck,d.title,d.length_ms,(unsigned long long)(now-x->req_ms));
            } else if(now-x->req_ms>8000) {
                /* 1.94: roots 0 and 5 never gave a good load on hardware (only empty-title,
                 * wrong-length tracks or EP136's "media file" error): one more try with root 4. */
                if(++x->attempt<2) {
                    void *loader=xd_loader(deck);
                    if(loader){xd_load(loader,x->media,x->track,0);x->req_ms=now;
                        XD_LOG("xdeck: deck %u did not load, trying once more (root %u)",deck,xd_roots[0]);}
                } else {x->state=3;XD_LOG("xdeck: deck %u: track %u did not load (gave up)",deck,x->track);}
            }
            continue;
        }
        /* follow the player's position (the deck stays paused) */
        if(x->state==2&&!have_target) {   /* 1.51: say once per track when the grid is missing */
            static uint32_t said_track[2];
            if(said_track[i]!=x->track&&now-x->shown_ms>3000){said_track[i]=x->track;XD_LOG("xdeck: deck %u has no player position yet (track %u: beat grid not fetched)",deck,x->track);}
        }
        if(x->state==2&&have_target&&api->setting_get("xloop",1)) {   /* 1.86/1.87 */
            xd_loop_step(api,deck,x,pstate==4,target,1.0+ppitch/100.0,now);
            if(x->lp_phase>=2&&x->lp_phase<=5)continue;   /* placing the loop: the follower waits */
            if(x->lp_phase==6){struct oqmod_deck d;memset(&d,0,sizeof d);api->deck(deck,&d);target=xd_loop_wrap(x,target,(double)d.elapsed_ms);}
        }
        else if(x->lp_phase){if(x->lp_phase==6)xd_loop_call(deck,2);x->lp_phase=0;}
        if(x->state==2&&have_target&&api->setting_get("xfollow",1)) {
            /* 1.48: play along natively; falls back to the paused follower if tempo can't be set */
            if(api->setting_get("xplay",1)&&xp_tick(api,deck,x,(int)pplaying,ppitch,target,now))continue;
            struct oqmod_deck d;memset(&d,0,sizeof d);api->deck(deck,&d);
            if(d.playing||!d.loaded)continue;
            if(d.length_ms&&target>(double)d.length_ms)target=d.length_ms;
            double delta=0;int r=xd_follow_step(x,target,d.elapsed_ms,now,&delta);
            if(r==1)XD_LOG("xdeck: deck %u starts following at %u ms (player at %.0f ms)",deck,d.elapsed_ms,target);
            if(delta!=0.0&&xd_move(deck,delta))x->moves++;
            if(r==2&&(x->checks<=12||x->checks%120==0||x->mismatch==3))
                XD_LOG("xdeck: deck %u at %u ms, commanded %.0f, player %.0f, off %.0f (mean %.1f), moves %u, resyncs %u",deck,d.elapsed_ms,x->cmd,target,x->last_off,x->off_n?x->off_sum/x->off_n:0.0,x->moves,x->resyncs);
        }
    }
    if(master_deck!=__atomic_load_n(&xd_master_deck,__ATOMIC_ACQUIRE)) {
        __atomic_store_n(&xd_master_deck,master_deck,__ATOMIC_RELEASE);
        XD_LOG("xdeck: master shown on deck %u (marks so far %u)",master_deck,xd_master_marks);
    }
}
#endif

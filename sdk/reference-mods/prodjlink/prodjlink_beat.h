#ifndef OQ_PRODJLINK_BEAT_H
#define OQ_PRODJLINK_BEAT_H
#include "oqmod.h"
/* OPUS decks 1-2 as link players 1-2 for tempo/phase: live status (playing, BPM, pitch,
 * beat count, beat in bar, master flag) and a beat packet (type 0x28 on 50001) at every
 * beat of the deck's grid, so other players show the master's beat bars and can SYNC.
 * Position: the core's deck view (anchor_ms at anchor_at, tempo) and its cached beat grid
 * (analysis_beats); the SDK's deck() gives loaded/playing/BPM/pitch.
 * Master (first version): an external player that says it is master and plays keeps it;
 * otherwise the lowest-numbered playing OPUS deck (1-2) is master. */
static const struct oqmod_api *lb_api;
static unsigned lb_beats_sent,lb_master;   /* lb_master: 0 none/external, 1-2 our deck */
/* Master hand-over (Pro DJ Link, as beat-link documents it): a player that wants master
 * sends 0x26 (its number at 0x27) to the master's address on 50001; the master answers
 * 0x27 (accepted = 1 at 0x2b) and puts the requester's number in its status 0x9f until
 * the requester claims master in its own status. */
static unsigned lb_force;          /* our deck (1-2) that took master over, 0 = none */
static unsigned lb_yield_to;       /* we hand over to this player (0 = no hand-over) */
static uint64_t lb_yield_ms;
static unsigned lb_abandon_pending;   /* another player took master: EP136 should give it up */
static unsigned lb_want;           /* our deck waiting for a hand-over (0 = none) */
static uint64_t lb_want_ms;
static unsigned lb_handover_lines;
static int lb_sock=-1;
static void lb_send_to(uint32_t ip_host,unsigned port,const unsigned char *p,size_t n) {
    if(lb_sock<0){lb_sock=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);if(lb_sock<0)return;}
    struct sockaddr_in to;memset(&to,0,sizeof to);to.sin_family=AF_INET;to.sin_port=htons((uint16_t)port);to.sin_addr.s_addr=htonl(ip_host);
    sendto(lb_sock,p,n,0,(struct sockaddr *)&to,sizeof to);
}
static void lb_handover_packet(unsigned char *p,unsigned kind,unsigned player,size_t len) {
    memset(p,0,len);memcpy(p,lk_magic,10);p[0x0a]=(unsigned char)kind;memcpy(p+0x0b,LK_NAME,strlen(LK_NAME));
    p[0x1f]=0x01;p[0x20]=0x00;p[0x21]=(unsigned char)player;p[0x22]=0;p[0x23]=(unsigned char)(len-0x24);
    p[0x27]=(unsigned char)player;
}
/* a hand-over request for one of our players (from lk_packet or EP136's own socket) */
static void lb_on_request(uint32_t from_ip,const unsigned char *b,size_t n) {
    if(n<0x28)return;
    unsigned requester=b[0x21]?b[0x21]:b[0x27];
    unsigned ours=lb_master;
    if(!ours||!requester||requester==ours)return;
    unsigned char r[0x2c];lb_handover_packet(r,0x27,ours,sizeof r);r[0x2b]=1;
    lb_send_to(from_ip,50001,r,sizeof r);
    lb_yield_to=requester;lb_yield_ms=lk_now_ms();lb_force=0;
    if(lb_handover_lines<30){lb_handover_lines++;lp_log("master: player %u asked for master, handing over from %u",requester,ours);}
}
/* another player is tempo master right now (its number, 0 none) */
static unsigned lb_external_master(void) {
    uint64_t now=lk_now_ms();unsigned m=0;
    pthread_mutex_lock(&lk_lock);
    for(unsigned i=0;i<LK_MAX;i++){const struct lk_player *p=&lk_players[i];
        if(p->ip&&p->ip!=lk_my_ip&&p->master&&now-p->last_status_ms<2000)m=p->number;}
    pthread_mutex_unlock(&lk_lock);
    return m;
}
/* our deck asks the current (external) master for master */
static void lb_request(unsigned deck) {
    uint64_t now=lk_now_ms();uint32_t ip=0;unsigned master=0;
    pthread_mutex_lock(&lk_lock);
    for(unsigned i=0;i<LK_MAX;i++){const struct lk_player *p=&lk_players[i];
        if(p->ip&&p->ip!=lk_my_ip&&p->master&&now-p->last_status_ms<2000){ip=p->ip;master=p->number;}}
    pthread_mutex_unlock(&lk_lock);
    lb_want=deck;lb_want_ms=now;
    if(!ip){lb_force=deck;lb_want=0;return;}   /* nobody else is master: just take it */
    unsigned char q[0x28];lb_handover_packet(q,0x26,deck,sizeof q);
    lb_send_to(ip,50001,q,sizeof q);
    if(lb_handover_lines<30){lb_handover_lines++;lp_log("master: deck %u asks player %u for master",deck,master);}
}
struct lb_state {int loaded,playing;uint32_t bpm_x100,track_bpm_x100,beat_no,interval_ms,next_ms;int pitch_x100;unsigned bar,grid;uint32_t t0;};
static int lb_read(unsigned deck,struct lb_state *s) {
    memset(s,0,sizeof *s);
    if(!lb_api||!lb_api->deck)return 0;
    struct oqmod_deck d;memset(&d,0,sizeof d);
    if(!lb_api->deck(deck,&d)||!d.loaded)return 0;
    s->loaded=1;s->playing=d.playing;s->pitch_x100=d.pitch_x100;s->bpm_x100=d.bpm_x100;
    double rate=1.0+d.pitch_x100/10000.0;if(rate<0.05)rate=0.05;
    s->track_bpm_x100=d.bpm_x100?(uint32_t)(d.bpm_x100/rate+0.5):0;
    double pos=d.elapsed_ms;
    struct oqmod_analysis_snapshot snap;memset(&snap,0,sizeof snap);snap.size=sizeof snap;
    if(lb_api->analysis_snapshot&&lb_api->analysis_snapshot(deck,&snap)&&snap.valid) {
        uint64_t now=lk_now_ms();
        pos=snap.anchor_ms+(d.playing&&now>snap.anchor_at?(double)(now-snap.anchor_at)*rate:0);
        struct oqmod_analysis_beats b;
        if(snap.generation&&lb_api->analysis_beats&&lb_api->analysis_beats(deck,snap.generation,(uint32_t)pos,&b)&&b.time_ms[1]>b.time_ms[0]) {
            s->grid=1;s->t0=b.time_ms[0];s->bar=b.beat[0]?b.beat[0]:1;
            s->interval_ms=b.time_ms[1]-b.time_ms[0];
            double left=(b.time_ms[1]-pos)/rate;s->next_ms=left>0?(uint32_t)left:0;
            s->beat_no=s->interval_ms?b.time_ms[0]/s->interval_ms+1:1;
            return 1;
        }
    }
    /* no grid: beats from the BPM alone */
    if(d.bpm_x100) {
        double beat=6000000.0/d.bpm_x100*rate;   /* track ms per beat */
        uint32_t n=(uint32_t)(pos/beat);
        s->interval_ms=(uint32_t)beat;s->t0=(uint32_t)(n*beat);s->beat_no=n+1;s->bar=n%4+1;
        s->next_ms=(uint32_t)(((n+1)*beat-pos)/rate);
    }
    return 1;
}
static uint32_t lb_pitch_word(int pitch_x100) {
    long v=(long)(0x100000*(1.0+pitch_x100/10000.0));if(v<0)v=0;if(v>0x200000)v=0x200000;return (uint32_t)v;
}
static void lb_put32(unsigned char *p,uint32_t v){p[0]=(unsigned char)(v>>24);p[1]=(unsigned char)(v>>16);p[2]=(unsigned char)(v>>8);p[3]=(unsigned char)v;}
/* who is tempo master right now */
static unsigned lb_pick_master(const struct lb_state st[3]) {
    uint64_t now=lk_now_ms();int external=0;unsigned yielded_to_us=0;
    pthread_mutex_lock(&lk_lock);
    for(unsigned i=0;i<LK_MAX;i++){const struct lk_player *p=&lk_players[i];
        if(!p->ip||p->ip==lk_my_ip||now-p->last_status_ms>=2000)continue;
        if(p->master)external=p->number;
        if(lb_want&&p->master&&p->yield_to==lb_want)yielded_to_us=1;}
    pthread_mutex_unlock(&lk_lock);
    /* we asked: the master named us in its 0x9f (or stopped being master) -> take it */
    if(lb_want&&(yielded_to_us||!external)){lb_force=lb_want;lb_want=0;
        if(lb_handover_lines<30){lb_handover_lines++;lp_log("master: deck %u took master",lb_force);}}
    if(lb_want&&now-lb_want_ms>3000)lb_want=0;
    /* we handed over: stop once the new master claims it */
    if(lb_yield_to&&external==(int)lb_yield_to){lb_yield_to=0;lb_abandon_pending=1;}   /* handed over */
    if(lb_yield_to&&now-lb_yield_ms>5000){   /* it never took over */
        if(lb_handover_lines<30){lb_handover_lines++;lp_log("master: player %u did not take over, hand-over cancelled",lb_yield_to);}
        lb_yield_to=0;}
    if(lb_yield_to)return lb_master;          /* keep master (0x9f set) until it takes over */
    /* who plays: our decks and the other players */
    unsigned ours_playing=0,ours_first=0,others_playing=0,external_playing=0;
    for(unsigned p=1;p<=2;p++)if(st[p].loaded&&st[p].playing){ours_playing++;if(!ours_first)ours_first=p;}
    pthread_mutex_lock(&lk_lock);
    for(unsigned i=0;i<LK_MAX;i++){const struct lk_player *p=&lk_players[i];
        if(p->ip&&p->ip!=lk_my_ip&&now-p->last_status_ms<2000&&p->playing){others_playing++;if((int)p->number==external)external_playing=1;}}
    pthread_mutex_unlock(&lk_lock);
    /* automatic master, as players do: the master stopped and exactly one deck plays */
    if(lb_force&&!(st[lb_force].loaded&&st[lb_force].playing)&&ours_playing+others_playing==1) {
        if(ours_playing){lb_force=ours_first;}
        else lb_force=0;   /* only another player plays: let it take master */
    }
    if(lb_force)return lb_force;          /* taken over: ours until we hand it on */
    if(external) {
        if(!external_playing&&ours_playing==1&&others_playing==0&&!lb_want)lb_request(ours_first);
        return 0;
    }
    if(others_playing&&!ours_playing)return 0;   /* only another player plays: it can claim */
    /* master stays where it is while that deck plays (no jumping to the lower deck) */
    if(lb_master>=1&&lb_master<=2&&st[lb_master].loaded&&st[lb_master].playing)return lb_master;
    for(unsigned p=1;p<=2;p++)if(st[p].loaded&&st[p].playing)return p;
    return 0;
}
/* status of player p (deck p): the 0x11c layout of lp_status plus live fields */
static void lb_status(unsigned char *buf,unsigned player,const struct lb_state *s,unsigned counter,int master,int usb) {
    lp_status(buf,counter);
    buf[0x21]=(unsigned char)player;buf[0x24]=(unsigned char)player;
    buf[0x6f]=usb?0x00:0x04;buf[0x75]=usb?0x01:0x00;
    if(s->loaded) {
        buf[0x27]=s->playing?1:0;buf[0x28]=(unsigned char)player;buf[0x29]=3;buf[0x2a]=1;
        buf[0x7b]=s->playing?3:5;
        buf[0x89]=0x84|(s->playing?0x40:0)|(master?0x20:0);
        buf[0x8b]=s->playing?0x7a:0x7e;
        uint32_t pw=lb_pitch_word(s->pitch_x100);
        lb_put32(buf+0x8c,pw);lb_put32(buf+0x98,pw);lb_put32(buf+0xc0,pw);lb_put32(buf+0xc4,pw);
        buf[0x90]=0x80;buf[0x91]=0x00;
        uint32_t bpm=s->track_bpm_x100?s->track_bpm_x100:0xffff;buf[0x92]=(unsigned char)(bpm>>8);buf[0x93]=(unsigned char)bpm;
        buf[0x9d]=s->playing?9:1;buf[0x9e]=master?1:0;buf[0x9f]=master&&lb_yield_to?(unsigned char)lb_yield_to:0xff;
        lb_put32(buf+0xa0,s->beat_no?s->beat_no:0xffffffffu);buf[0xa4]=0x01;buf[0xa5]=0xff;
        buf[0xa6]=(unsigned char)(s->bar?s->bar:0);
    } else {
        buf[0x89]=0x84|(master?0x20:0);
    }
}
static void lb_beat(unsigned char p[0x60],unsigned player,const struct lb_state *s) {
    memset(p,0,0x60);memcpy(p,lk_magic,10);p[0x0a]=0x28;memcpy(p+0x0b,LK_NAME,strlen(LK_NAME));
    p[0x1f]=0x01;p[0x20]=0x00;p[0x21]=(unsigned char)player;p[0x22]=0x00;p[0x23]=0x3c;
    double rate=1.0+s->pitch_x100/10000.0;if(rate<0.05)rate=0.05;
    double beat=s->interval_ms/rate;unsigned left=5-(s->bar?s->bar:1);
    const unsigned offs[6]={0x24,0x28,0x2c,0x30,0x34,0x38},n[6]={1,2,left,4,left+4,8};
    for(int i=0;i<6;i++)lb_put32(p+offs[i],(uint32_t)(beat*n[i]+0.5));
    memset(p+0x3c,0xff,0x18);
    lb_put32(p+0x54,lb_pitch_word(s->pitch_x100));
    uint32_t bpm=s->track_bpm_x100;p[0x5a]=(unsigned char)(bpm>>8);p[0x5b]=(unsigned char)bpm;
    p[0x5c]=(unsigned char)(s->bar?s->bar:1);p[0x5f]=(unsigned char)player;
}
/* beat thread: every 5 ms, a beat packet when a playing deck (1-2) crosses a beat */
static unsigned lb_running;
static void *lb_thread(void *arg) {
    (void)arg;
    int sock=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0),one=1;if(sock<0)return 0;
    setsockopt(sock,SOL_SOCKET,SO_BROADCAST,&one,sizeof one);
    uint32_t last_t0[3]={0,0,0};
    while(__atomic_load_n(&lk_enabled,__ATOMIC_ACQUIRE)) {
        struct lb_state st[3];memset(st,0,sizeof st);
        for(unsigned p=1;p<=2;p++)lb_read(p,&st[p]);
        lb_master=lb_pick_master(st);
        uint32_t ip,bcast;unsigned char mac[6];
        if(lk_interface(&ip,&bcast,mac))
        for(unsigned p=1;p<=2;p++) {
            if(!st[p].loaded||!st[p].playing||!st[p].interval_ms){last_t0[p]=0;continue;}
            if(st[p].t0==last_t0[p])continue;
            int first=!last_t0[p];last_t0[p]=st[p].t0;
            if(first)continue;   /* start sending from the next beat */
            unsigned char pk[0x60];lb_beat(pk,p,&st[p]);
            struct sockaddr_in to;memset(&to,0,sizeof to);to.sin_family=AF_INET;to.sin_port=htons(50001);to.sin_addr.s_addr=bcast;
            if(sendto(sock,pk,sizeof pk,0,(struct sockaddr *)&to,sizeof to)==(ssize_t)sizeof pk)lb_beats_sent++;
            static unsigned said;if(said<6){said++;lp_log("beat: player %u beat %u/4 bpm %u.%02u next %u ms (grid %u)",p,st[p].bar,st[p].track_bpm_x100/100,st[p].track_bpm_x100%100,st[p].next_ms,st[p].grid);}
        }
        usleep(5000);
    }
    close(sock);__atomic_store_n(&lb_running,0,__ATOMIC_RELEASE);
    return 0;
}
static void lb_start(void) {
    if(__atomic_load_n(&lb_running,__ATOMIC_ACQUIRE))return;
    __atomic_store_n(&lb_running,1,__ATOMIC_RELEASE);
    pthread_t t;if(pthread_create(&t,0,lb_thread,0))__atomic_store_n(&lb_running,0,__ATOMIC_RELEASE);else pthread_detach(t);
}
#endif

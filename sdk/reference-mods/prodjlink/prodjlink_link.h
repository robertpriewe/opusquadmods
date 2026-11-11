#ifndef OQ_PRODJLINK_LINK_H
#define OQ_PRODJLINK_LINK_H
/* Pro DJ Link add-on mod (MODS/prodjlink): the OPUS joins the link as a player and follows the others.
 *
 * On hardware (R85) EP136 announced itself once and then stayed quiet, so a real player
 * (XDJ-1000MK2) never sent it status. With the switch on, a thread of ours sends player
 * keep-alives (type 0x06 to 50000, every 1.5 s) as LK_PLAYER from the link interface's
 * address, and listens on 50001 (beats) and 50002 (status); 50000 stays EP136's. It
 * keeps the latest status and beat per player and publishes a summary for the Add-on
 * pane and the diagnostics report. Read only towards EP136: nothing is injected.
 * Offsets: Deep Symmetry's DJ Link analysis, as prodjlink-lab/xdj_capture.py. */
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "prodjlink_db.h"

#define LK_PLAYER 4
#define LK_NAME "OPUS-QUAD OQ"
#define LK_MAX 6
static const unsigned char lk_magic[10]={'Q','s','p','t','1','W','m','J','O','L'};
struct lk_player {
    uint32_t ip;unsigned number,seen_ms,status_count,beat_count,keepalive_count;char name[21];
    unsigned playing,master,synced,slot,track_player,beat_in_bar,status_len;
    uint32_t track_id,beat,bpm_x100;int pitch_x100;uint64_t last_status_ms,last_beat_ms;
    /* stage 3: the loaded track's data from the player's dbserver */
    uint32_t fetched_track,fetch_state;int grid_beats;uint32_t wave_bytes;char wave_kind[8];
};
enum {LK_FETCH_NONE,LK_FETCH_BUSY,LK_FETCH_DONE,LK_FETCH_FAILED};
struct lk_fetch_job {uint32_t ip,track;unsigned number,slot;};
static struct lk_player lk_players[LK_MAX];
static pthread_mutex_t lk_lock=PTHREAD_MUTEX_INITIALIZER;
static unsigned lk_enabled,lk_running,lk_sent,lk_bind_ok,lk_rx,lk_errors;
static char lk_iface[IF_NAMESIZE],lk_addr[16];

static uint64_t lk_now_ms(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;}
static uint32_t lk_be32(const unsigned char *p) {return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int lk_pitch_x100(uint32_t word) {return (int)(((int64_t)(word&0xffffff)-0x100000)*10000/0x100000);}

static struct lk_player *lk_slot(uint32_t ip,unsigned number) {
    struct lk_player *free_slot=0;
    for(unsigned i=0;i<LK_MAX;i++) {
        if(lk_players[i].ip==ip&&lk_players[i].number==number)return &lk_players[i];
        if(!lk_players[i].ip&&!free_slot)free_slot=&lk_players[i];
    }
    if(free_slot){memset(free_slot,0,sizeof *free_slot);free_slot->ip=ip;free_slot->number=number;}
    return free_slot;
}
/* Stage 3 worker: grid and waveform of one track from the player's dbserver. */
static void *lk_fetch_thread(void *arg) {
    struct lk_fetch_job job=*(struct lk_fetch_job *)arg;free(arg);
    int beats=-1;uint32_t wave=0;const char *kind="none";
    int s=lkdb_open(htonl(job.ip),LK_PLAYER);
    if(s>=0) {
        struct lkdb_beat *b=0;beats=lkdb_beat_grid(s,LK_PLAYER,job.slot,job.track,&b);free(b);
        unsigned char *w=0;
        if(!lkdb_anlz_tag(s,LK_PLAYER,job.slot,job.track,"PWV7","2EX",&w,&wave))kind="PWV7";
        else if(!lkdb_anlz_tag(s,LK_PLAYER,job.slot,job.track,"PWV5","EXT",&w,&wave))kind="PWV5";
        else if(!lkdb_wave_detail(s,LK_PLAYER,job.slot,job.track,&w,&wave))kind="blue";
        free(w);lkdb_close(s);
    }
    pthread_mutex_lock(&lk_lock);
    for(unsigned i=0;i<LK_MAX;i++) {
        struct lk_player *pl=&lk_players[i];
        if(pl->ip!=job.ip||pl->number!=job.number)continue;
        pl->fetched_track=job.track;pl->grid_beats=beats;pl->wave_bytes=wave;
        snprintf(pl->wave_kind,sizeof pl->wave_kind,"%s",kind);
        pl->fetch_state=s>=0&&beats>0?LK_FETCH_DONE:LK_FETCH_FAILED;
    }
    pthread_mutex_unlock(&lk_lock);
    return 0;
}
/* Called with lk_lock held. */
static void lk_start_fetch(struct lk_player *pl) {
    struct lk_fetch_job *job=malloc(sizeof *job);if(!job)return;
    *job=(struct lk_fetch_job){pl->ip,pl->track_id,pl->number,pl->slot};
    pthread_t t;pl->fetch_state=LK_FETCH_BUSY;
    if(pthread_create(&t,0,lk_fetch_thread,job)){free(job);pl->fetch_state=LK_FETCH_FAILED;}
    else pthread_detach(t);
}
static void lp_note_packet(unsigned port,uint32_t ip,const unsigned char *p,size_t n);   /* prodjlink_probe.h */
/* One received DJ Link packet (any port); the caller holds no lock. */
static void lk_packet(unsigned port,uint32_t ip,const unsigned char *p,size_t n) {
    if(n<0x24||memcmp(p,lk_magic,10))return;
    unsigned kind=p[0x0a];uint64_t now=lk_now_ms();
    lp_note_packet(port,ip,p,n);
    pthread_mutex_lock(&lk_lock);lk_rx++;
    if(port==50000&&kind==0x06&&n>0x24&&p[0x24]!=LK_PLAYER) {
        struct lk_player *pl=lk_slot(ip,p[0x24]);
        if(pl){pl->keepalive_count++;pl->seen_ms=(unsigned)now;memcpy(pl->name,p+0x0c,20);pl->name[20]=0;}
    } else if(port==50002&&kind==0x0a&&n>=0xa8&&p[0x21]!=LK_PLAYER) {   /* not our own offer status */
        struct lk_player *pl=lk_slot(ip,p[0x21]);
        if(pl) {
            pl->status_count++;pl->last_status_ms=now;pl->status_len=(unsigned)n;
            memcpy(pl->name,p+0x0b,20);pl->name[20]=0;
            pl->track_player=p[0x28];pl->slot=p[0x29];pl->track_id=lk_be32(p+0x2c);
            pl->playing=!!(p[0x89]&0x40);pl->master=!!(p[0x89]&0x20);pl->synced=!!(p[0x89]&0x10);
            pl->pitch_x100=lk_pitch_x100(lk_be32(p+0x8c));
            pl->bpm_x100=(uint32_t)p[0x92]<<8|p[0x93];pl->beat=lk_be32(p+0xa0);pl->beat_in_bar=p[0xa6];
            /* a rekordbox track from this player's own media, not fetched yet: fetch it */
            if(pl->track_id&&p[0x2a]==1&&pl->track_player==pl->number&&pl->fetched_track!=pl->track_id&&pl->fetch_state!=LK_FETCH_BUSY)
                lk_start_fetch(pl);
        }
    } else if(port==50001&&kind==0x28&&n>=0x60) {
        struct lk_player *pl=lk_slot(ip,p[0x21]);
        if(pl){pl->beat_count++;pl->last_beat_ms=now;pl->beat_in_bar=p[0x5c];pl->bpm_x100=(uint32_t)p[0x5a]<<8|p[0x5b];}
    }
    pthread_mutex_unlock(&lk_lock);
}
/* The 0x36-byte player keep-alive (as prodjlink-lab/fake_xdj.py keepalive()). */
static void lk_keepalive(unsigned char p[0x36],const unsigned char mac[6],uint32_t ip_be,unsigned peers) {
    memset(p,0,0x36);memcpy(p,lk_magic,10);p[0x0a]=0x06;
    memcpy(p+0x0c,LK_NAME,strlen(LK_NAME));
    p[0x20]=0x01;p[0x21]=0x02;p[0x22]=0x00;p[0x23]=0x36;
    p[0x24]=LK_PLAYER;p[0x25]=0x01;memcpy(p+0x26,mac,6);memcpy(p+0x2c,&ip_be,4);
    p[0x30]=(unsigned char)(peers?peers:1);p[0x34]=0x01;
}
/* The link interface: an IPv4 address on a non-loopback interface, link-local first. */
static int lk_interface(uint32_t *ip_be,uint32_t *bcast_be,unsigned char mac[6]) {
    struct ifaddrs *list=0;if(getifaddrs(&list))return 0;
    struct ifaddrs *pick=0;
    for(struct ifaddrs *a=list;a;a=a->ifa_next) {
        if(!a->ifa_addr||a->ifa_addr->sa_family!=AF_INET||(a->ifa_flags&IFF_LOOPBACK)||!(a->ifa_flags&IFF_UP))continue;
        if(!strncmp(a->ifa_name,"wlan",4))continue;   /* DJ Link runs on the LAN port */
        uint32_t ip=ntohl(((struct sockaddr_in *)a->ifa_addr)->sin_addr.s_addr);
        if(!pick||(ip>>16)==0xa9fe)pick=a;
    }
    int ok=0;
    if(pick) {
        *ip_be=((struct sockaddr_in *)pick->ifa_addr)->sin_addr.s_addr;
        *bcast_be=pick->ifa_broadaddr?((struct sockaddr_in *)pick->ifa_broadaddr)->sin_addr.s_addr:(*ip_be|htonl(0xffff));
        snprintf(lk_iface,sizeof lk_iface,"%s",pick->ifa_name);
        inet_ntop(AF_INET,ip_be,lk_addr,sizeof lk_addr);
        int s=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);struct ifreq r;memset(&r,0,sizeof r);
        snprintf(r.ifr_name,sizeof r.ifr_name,"%s",pick->ifa_name);
        memset(mac,0,6);
        if(s>=0&&!ioctl(s,SIOCGIFHWADDR,&r))memcpy(mac,r.ifr_hwaddr.sa_data,6);
        if(s>=0)close(s);
        ok=1;
    }
    freeifaddrs(list);return ok;
}
static int lk_bind(unsigned port) {
    int s=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0),one=1;
    if(s<0)return -1;
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
#ifdef SO_REUSEPORT
    setsockopt(s,SOL_SOCKET,SO_REUSEPORT,&one,sizeof one);
#endif
    setsockopt(s,SOL_SOCKET,SO_BROADCAST,&one,sizeof one);
    struct sockaddr_in a;memset(&a,0,sizeof a);a.sin_family=AF_INET;a.sin_port=htons((uint16_t)port);
    if(bind(s,(struct sockaddr *)&a,sizeof a)){close(s);return -1;}
    return s;
}
#include "prodjlink_probe.h"
static void *lk_thread(void *arg) {
    (void)arg;
    /* 50000 stays EP136's: keep-alives go out from an unbound socket of ours, so its
     * own announce traffic is never shared or taken. Status and beats: 50002, 50001. */
    int send_sock=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0),one=1;
    if(send_sock>=0)setsockopt(send_sock,SOL_SOCKET,SO_BROADCAST,&one,sizeof one);
    int sock[3]={-1,lk_bind(50001),lk_bind(50002)};
    lk_bind_ok=(send_sock>=0)|(sock[1]>=0)<<1|(sock[2]>=0)<<2;
    uint64_t next=0,next_status=0,next_census=0;unsigned counter=0;
    while(__atomic_load_n(&lk_enabled,__ATOMIC_ACQUIRE)) {
        uint64_t now=lk_now_ms();
        if(now>=next_census){next_census=now+30000;lp_census();}
        if(__atomic_load_n(&lp_offer,__ATOMIC_ACQUIRE)) {
            lp_open();
            if(now>=next_status&&send_sock>=0) {
                next_status=now+200;uint32_t ip,bcast;unsigned char mac[6];
                if(lk_interface(&ip,&bcast,mac)) {
                    unsigned char st[0xd4];lp_status(st,++counter);
                    struct sockaddr_in to;memset(&to,0,sizeof to);to.sin_family=AF_INET;to.sin_port=htons(50002);to.sin_addr.s_addr=bcast;
                    if(sendto(send_sock,st,sizeof st,0,(struct sockaddr *)&to,sizeof to)==(ssize_t)sizeof st)lp_status_sent++;else lk_errors++;
                }
            }
        } else lp_close();
        if(now>=next) {
            next=now+1500;uint32_t ip,bcast;unsigned char mac[6];
            if(lk_interface(&ip,&bcast,mac)&&send_sock>=0) {
                unsigned peers=1;pthread_mutex_lock(&lk_lock);
                for(unsigned i=0;i<LK_MAX;i++)if(lk_players[i].ip&&now-lk_players[i].seen_ms<5000)peers++;
                pthread_mutex_unlock(&lk_lock);
                unsigned char ka[0x36];lk_keepalive(ka,mac,ip,peers);
                struct sockaddr_in to;memset(&to,0,sizeof to);to.sin_family=AF_INET;to.sin_port=htons(50000);to.sin_addr.s_addr=bcast;
                if(sendto(send_sock,ka,sizeof ka,0,(struct sockaddr *)&to,sizeof to)==(ssize_t)sizeof ka)lk_sent++;else lk_errors++;
            }
        }
        fd_set set;FD_ZERO(&set);int top=-1;
        for(int i=0;i<3;i++)if(sock[i]>=0){FD_SET(sock[i],&set);if(sock[i]>top)top=sock[i];}
        lp_fdset(&set,&top);
        struct timeval tv={0,200000};
        if(top<0){usleep(200000);continue;}
        if(select(top+1,&set,0,0,&tv)<=0)continue;
        lp_serve(&set);
        for(int i=0;i<3;i++)if(sock[i]>=0&&FD_ISSET(sock[i],&set)) {
            unsigned char buf[1500];struct sockaddr_in from;socklen_t fl=sizeof from;
            ssize_t n=recvfrom(sock[i],buf,sizeof buf,0,(struct sockaddr *)&from,&fl);
            if(n>0)lk_packet(50000u+(unsigned)i,ntohl(from.sin_addr.s_addr),buf,(size_t)n);
        }
    }
    for(int i=0;i<3;i++)if(sock[i]>=0)close(sock[i]);
    if(send_sock>=0)close(send_sock);
    lp_close();
    __atomic_store_n(&lk_running,0,__ATOMIC_RELEASE);
    return 0;
}
/* Settings poll: start or stop the thread with the switch. */
static void lk_set_enabled(int on) {
    __atomic_store_n(&lk_enabled,on?1u:0u,__ATOMIC_RELEASE);
    if(on&&!__atomic_load_n(&lk_running,__ATOMIC_ACQUIRE)) {
        pthread_t t;__atomic_store_n(&lk_running,1,__ATOMIC_RELEASE);
        if(pthread_create(&t,0,lk_thread,0)){__atomic_store_n(&lk_running,0,__ATOMIC_RELEASE);lk_errors++;}
        else pthread_detach(t);
    }
}
/* Up to four lines for the Mod Menu pane: our address, then each other player. */
static void lk_summary(char *out,size_t cap) {
    uint64_t now=lk_now_ms();size_t n=0;
    n+=(size_t)snprintf(out+n,cap-n,"OPUS as player %d on %s %s; sent %u, received %u%s\n",LK_PLAYER,
        lk_iface[0]?lk_iface:"-",lk_addr[0]?lk_addr:"(no address)",lk_sent,lk_rx,lk_bind_ok==7?"":" (port in use)");
    pthread_mutex_lock(&lk_lock);
    unsigned shown=0;
    for(unsigned i=0;i<LK_MAX&&n<cap;i++) {
        const struct lk_player *p=&lk_players[i];if(!p->ip)continue;
        if(now-p->last_status_ms>3000) {
            n+=(size_t)snprintf(out+n,cap-n,"Player %u %s: seen, no status yet\n",p->number,p->name);
        } else {
            int a=p->pitch_x100<0?-p->pitch_x100:p->pitch_x100;
            n+=(size_t)snprintf(out+n,cap-n,"Player %u %s: %s %u.%02u BPM %c%d.%02d%% beat %u/4%s%s track %u\n",p->number,p->name,
                p->playing?"PLAY":"stop",p->bpm_x100/100,p->bpm_x100%100,p->pitch_x100<0?'-':'+',a/100,a%100,
                p->beat_in_bar,p->master?" MASTER":"",p->synced?" SYNC":"",p->track_id);
            if(p->fetch_state==LK_FETCH_BUSY&&n<cap)n+=(size_t)snprintf(out+n,cap-n,"  fetching track %u ...\n",p->track_id);
            else if(p->fetch_state==LK_FETCH_DONE&&n<cap)n+=(size_t)snprintf(out+n,cap-n,"  track %u: grid %d beats, waveform %s %u bytes\n",p->fetched_track,p->grid_beats,p->wave_kind,p->wave_bytes);
            else if(p->fetch_state==LK_FETCH_FAILED&&n<cap)n+=(size_t)snprintf(out+n,cap-n,"  track %u: dbserver did not answer\n",p->fetched_track);
        }
        if(++shown>=2)break;
    }
    if(!shown&&n<cap)n+=(size_t)snprintf(out+n,cap-n,"No other DJ Link player heard yet.\n");
    if(lp_offer&&n<cap)snprintf(out+n,cap-n,"Offering the USB: %u status sent, %u requests, NFS %u/%u/%u%s\n",lp_status_sent,lp_requests,ln_calls[0],ln_calls[1],ln_calls[2],ln_root[0]?"":" (no USB found)");
    pthread_mutex_unlock(&lk_lock);
}
static void lk_report(FILE *out) {
    fprintf(out,"prodjlink_link enabled=%u running=%u iface=%s addr=%s bind=%u sent=%u rx=%u errors=%u\n",
            lk_enabled,lk_running,lk_iface,lk_addr,lk_bind_ok,lk_sent,lk_rx,lk_errors);
    pthread_mutex_lock(&lk_lock);
    for(unsigned i=0;i<LK_MAX;i++) {
        const struct lk_player *p=&lk_players[i];if(!p->ip)continue;
        fprintf(out,"prodjlink_player number=%u name=%s ip=%u.%u.%u.%u keepalives=%u status=%u status_len=%u beats=%u playing=%u "
                "bpm_x100=%u pitch_x100=%d beat=%u bar_beat=%u track=%u from_player=%u slot=%u master=%u sync=%u fetch=%u fetched=%u grid=%d wave=%s/%u\n",
                p->number,p->name,p->ip>>24,(p->ip>>16)&255,(p->ip>>8)&255,p->ip&255,p->keepalive_count,p->status_count,p->status_len,
                p->beat_count,p->playing,p->bpm_x100,p->pitch_x100,p->beat,p->beat_in_bar,p->track_id,p->track_player,p->slot,p->master,p->synced,
                p->fetch_state,p->fetched_track,p->grid_beats,p->wave_kind[0]?p->wave_kind:"-",p->wave_bytes);
    }
    pthread_mutex_unlock(&lk_lock);
}
#endif

#ifndef OQ_PRODJLINK_LINK_H
#define OQ_PRODJLINK_LINK_H
/* Pro DJ Link add-on mod (MODS/prodjlink): the OPUS joins the link as a player and follows the others.
 *
 * On hardware (R85) EP136 announced itself once and then stayed quiet, so a real player
 * (XDJ-1000MK2) never sent it status. With the switch on, a thread of ours sends player
 * keep-alives (type 0x06 to 50000, every 1.5 s) as player lk_me from the link interface's
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
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "prodjlink_db.h"

/* Our player number: the lowest of 1-4 that no other device has announced in the last
 * 5 s (a real XDJ/CDJ only lists link media from players 1-4). On the 2026-11-11 test a
 * fixed 4 collided with the XDJ, which was also 4, and the XDJ kept re-claiming it.
 * 50000 is EP136's port, so its keep-alives are read from a packet socket (lk_sniff). */
static _Atomic unsigned lk_me;
static uint64_t lk_taken_ms[16];
static uint32_t lk_my_ip;           /* host order */
static unsigned lk_number_changes,lk_sniff_ok,lk_two;
static char lk_peer_fw[5];
/* A media query (0x05 on 50002) for our USB, answered by the thread (lk_media_reply). */
static uint32_t lk_mq_ip;static unsigned lk_mq_slot,lk_mq_pending,lk_mq_count,lk_mq_sent;        /* firmware text from another player's status */
#define LK_PLAYER (__atomic_load_n(&lk_me,__ATOMIC_ACQUIRE))
#define LK_NAME "OPUS-QUAD OQ"
#define LK_MAX 6
static const unsigned char lk_magic[10]={'Q','s','p','t','1','W','m','J','O','L'};
struct lk_player {
    uint32_t ip;unsigned number,seen_ms,status_count,beat_count,keepalive_count;char name[21];
    unsigned playing,master,synced,slot,track_player,beat_in_bar,status_len;
    uint32_t track_id,beat,bpm_x100;int pitch_x100;uint64_t last_status_ms,last_beat_ms;
    /* stage 3: the loaded track's data from the player's dbserver */
    uint32_t fetched_track,fetch_state;int grid_beats;uint32_t wave_bytes;char wave_kind[8];
    unsigned yield_to;   /* its status 0x9f: the player it hands master to (0xff/0 = none) */
    /* 1.47: the fetched beat grid (prodjlink_xdeck.h places the player's track on our deck) */
    struct lkdb_beat *grid;uint32_t grid_track;int grid_n;unsigned track_kind,play_state;
    /* 1.51: a failed fetch is retried (EP136 may be loading the same track from that player's
     * dbserver at the same moment): every 3 s, up to 6 tries per track */
    uint32_t retry_track;unsigned fetch_tries;uint64_t fetch_retry_ms;
};
enum {LK_FETCH_NONE,LK_FETCH_BUSY,LK_FETCH_DONE,LK_FETCH_FAILED};
struct lk_fetch_job {uint32_t ip,track;unsigned number,slot,local;};
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
    if(free_slot){free(free_slot->grid);memset(free_slot,0,sizeof *free_slot);free_slot->ip=ip;free_slot->number=number;}
    return free_slot;
}
static void lp_log(const char *fmt,...);   /* prodjlink_probe.h */
static void w3_store(uint32_t ip,uint32_t track,unsigned char *data,uint32_t len);   /* prodjlink_wave3.h */
static unsigned char *w3_pwv7_from_pwv5(const unsigned char *p5,uint32_t len,uint32_t *out_len);
static int w3_proxy_fetch(uint32_t ip,unsigned slot,uint32_t track,struct lkdb_beat **grid,int *beats,unsigned char **wave,uint32_t *wave_len,const char **kind);
/* Stage 3 worker: grid and waveform of one track from the player's dbserver. */
static void *lk_fetch_thread(void *arg) {
    struct lk_fetch_job job=*(struct lk_fetch_job *)arg;free(arg);
    int beats=-1;uint32_t wave=0;const char *kind="none";int s=-1;
    struct lkdb_beat *b=0;unsigned char *w=0;
    /* 1.75: through EP136's (proxied) connection when there is one: a player ignores a second client */
    /* 1.84: a track the player loaded from our own USB (LINK on the XDJ): its beat grid comes
     * from the OPUS's own dbserver (127.0.0.1, the one EP136's browser and the XDJ use, same
     * track ids). The deck plays the local file, so EP136 draws its waveform itself. */
    int via_proxy=job.local?0:w3_proxy_fetch(job.ip,job.slot,job.track,&b,&beats,&w,&wave,&kind);
    if(via_proxy)s=0;
    else if(job.local) {
        s=lkdb_open(htonl(0x7f000001u),LK_PLAYER);
        if(s>=0){beats=lkdb_beat_grid(s,LK_PLAYER,job.slot,job.track,&b);lkdb_close(s);}
    }
    else {
        s=lkdb_open(htonl(job.ip),LK_PLAYER);
        if(s>=0) {
            beats=lkdb_beat_grid(s,LK_PLAYER,job.slot,job.track,&b);
            if(!lkdb_anlz_tag(s,LK_PLAYER,job.slot,job.track,"PWV7","2EX",&w,&wave))kind="PWV7";
            else{free(w);w=0;if(!lkdb_anlz_tag(s,LK_PLAYER,job.slot,job.track,"PWV5","EXT",&w,&wave))kind="PWV5";
                else{free(w);w=0;if(!lkdb_wave_detail(s,LK_PLAYER,job.slot,job.track,&w,&wave))kind="blue";}}
            lkdb_close(s);
        }
    }
    pthread_mutex_lock(&lk_lock);
    for(unsigned i=0;i<LK_MAX;i++){struct lk_player *pl=&lk_players[i];
        if(pl->ip!=job.ip||pl->number!=job.number||!b||beats<=0)continue;
        free(pl->grid);pl->grid=b;pl->grid_n=beats;pl->grid_track=job.track;b=0;}
    pthread_mutex_unlock(&lk_lock);
    free(b);
    if(w&&!strcmp(kind,"PWV7")){w3_store(job.ip,job.track,w,wave);w=0;}   /* kept for EP136 */
    else if(w&&!strcmp(kind,"PWV5")){uint32_t l7=0;unsigned char *p7=w3_pwv7_from_pwv5(w,wave,&l7);if(p7)w3_store(job.ip,job.track,p7,l7);}
    free(w);
    pthread_mutex_lock(&lk_lock);
    for(unsigned i=0;i<LK_MAX;i++) {
        struct lk_player *pl=&lk_players[i];
        if(pl->ip!=job.ip||pl->number!=job.number)continue;
        pl->fetched_track=job.track;pl->grid_beats=beats;pl->wave_bytes=wave;
        snprintf(pl->wave_kind,sizeof pl->wave_kind,"%s",kind);
        pl->fetch_state=s>=0&&beats>0?LK_FETCH_DONE:LK_FETCH_FAILED;
        pl->fetch_retry_ms=lk_now_ms()+3000;
    }
    pthread_mutex_unlock(&lk_lock);
    lp_log("fetch player %u track %u%s: %s, grid %d beats, waveform %s %u bytes",job.number,job.track,via_proxy?" (through EP136's connection)":job.local?" (from our own USB, local dbserver)":"",
        s<0?"dbserver not reached":beats>0?"ok":"no grid",beats,kind,wave);
    return 0;
}
/* Called with lk_lock held. */
static void lk_start_fetch(struct lk_player *pl) {
    struct lk_fetch_job *job=malloc(sizeof *job);if(!job)return;
    *job=(struct lk_fetch_job){pl->ip,pl->track_id,pl->number,pl->slot,pl->track_player!=pl->number};
    pthread_t t;pl->fetch_state=LK_FETCH_BUSY;
    if(pthread_create(&t,0,lk_fetch_thread,job)){free(job);pl->fetch_state=LK_FETCH_FAILED;}
    else pthread_detach(t);
}
static void lp_note_packet(unsigned port,uint32_t ip,const unsigned char *p,size_t n);   /* prodjlink_probe.h */
static void lp_log(const char *fmt,...);
static void lp_hex(char *out,size_t cap,const unsigned char *p,size_t n);
static void lb_on_request(uint32_t from_ip,const unsigned char *b,size_t n);   /* prodjlink_beat.h */
/* One received DJ Link packet (any port); the caller holds no lock. */
static void lk_packet(unsigned port,uint32_t ip,const unsigned char *p,size_t n) {
    if(n<0x24||memcmp(p,lk_magic,10))return;
    unsigned kind=p[0x0a];uint64_t now=lk_now_ms();
    lp_note_packet(port,ip,p,n);
    pthread_mutex_lock(&lk_lock);lk_rx++;
    if(port==50000&&kind==0x06&&n>=0x36) {   /* research: every new keep-alive sender */
        static uint32_t seen[24];static unsigned nseen;
        uint32_t key=ip*131u+p[0x24]*7u+p[0x34];unsigned i;
        for(i=0;i<nseen;i++)if(seen[i]==key)break;
        if(i==nseen&&nseen<24) {
            seen[nseen++]=key;char name[21];memcpy(name,p+0x0c,20);name[20]=0;
            lp_log("keepalive new: %u.%u.%u.%u number %u type %u (0x25=%u 0x35=%u) '%s'",ip>>24,(ip>>16)&255,(ip>>8)&255,ip&255,p[0x24],p[0x34],p[0x25],p[0x35],name);
        }
    }
    if(port==50000&&kind==0x06&&n>0x24&&p[0x24]<16&&!(ip==lk_my_ip&&p[0x24]==LK_PLAYER))
        lk_taken_ms[p[0x24]]=now;    /* someone else holds this number */
    if(port==50000&&kind==0x06&&n>0x24&&p[0x24]!=LK_PLAYER&&!(ip==lk_my_ip&&p[0x24]>4)) {
        struct lk_player *pl=lk_slot(ip,p[0x24]);
        if(pl){pl->keepalive_count++;pl->seen_ms=(unsigned)now;memcpy(pl->name,p+0x0c,20);pl->name[20]=0;}
    } else if(port==50002&&kind==0x05&&n>=0x30&&LK_PLAYER&&p[0x2b]==LK_PLAYER) {
        /* media query: 0x24 the asker's IP, 0x28 the player asked (BE32), 0x2c the slot */
        lk_mq_ip=lk_be32(p+0x24);lk_mq_slot=p[0x2f];lk_mq_pending=1;lk_mq_count++;
    } else if(port==50002&&kind==0x0a&&n>=0xa8&&p[0x21]!=LK_PLAYER) {   /* not our own offer status */
        struct lk_player *pl=lk_slot(ip,p[0x21]);
        if(pl) {
            pl->status_count++;pl->last_status_ms=now;pl->status_len=(unsigned)n;
            if(n>=0x80&&p[0x7c]>='0'&&p[0x7c]<='9'&&p[0x7d]=='.')memcpy(lk_peer_fw,p+0x7c,4);
            memcpy(pl->name,p+0x0b,20);pl->name[20]=0;
            pl->track_player=p[0x28];pl->slot=p[0x29];pl->track_kind=p[0x2a];pl->track_id=lk_be32(p+0x2c);pl->play_state=n>0x7b?p[0x7b]:0;
            pl->playing=!!(p[0x89]&0x40);pl->master=!!(p[0x89]&0x20);pl->synced=!!(p[0x89]&0x10);
            pl->pitch_x100=lk_pitch_x100(lk_be32(p+0x8c));
            pl->bpm_x100=(uint32_t)p[0x92]<<8|p[0x93];pl->beat=lk_be32(p+0xa0);pl->beat_in_bar=p[0xa6];
            pl->yield_to=p[0x9f]!=0xff?p[0x9f]:0;
            {   /* research: the master fields of other players' status, on change */
                static uint32_t last[8];unsigned slot=pl->number&7;
                uint32_t now_fields=(uint32_t)p[0x7b]<<24|(uint32_t)p[0x89]<<16|(uint32_t)p[0x9e]<<8|p[0x9f];   /* 1.81: + play state 0x7b (4 = looping) */
                if(last[slot]!=now_fields){last[slot]=now_fields;
                    pthread_mutex_unlock(&lk_lock);lp_log("status player %u: flags 0x89=%02x master 0x9e=%u handoff 0x9f=%02x playing %u state 0x7b=%02x (len %u)",pl->number,p[0x89],p[0x9e],p[0x9f],pl->playing,p[0x7b],(unsigned)n);pthread_mutex_lock(&lk_lock);}
            }
            /* a rekordbox track from this player's own media, not fetched yet: fetch it */
            if(pl->track_id&&p[0x2a]==1&&(pl->track_player==pl->number||(LK_PLAYER&&pl->track_player==LK_PLAYER))&&pl->fetch_state!=LK_FETCH_BUSY) {
                if(pl->retry_track!=pl->track_id){pl->retry_track=pl->track_id;pl->fetch_tries=0;}
                int fresh=pl->fetched_track!=pl->track_id;
                int retry=!fresh&&pl->fetch_state==LK_FETCH_FAILED&&pl->fetch_tries<6&&lk_now_ms()>=pl->fetch_retry_ms;
                if(fresh||retry){pl->fetch_tries++;lk_start_fetch(pl);}
            }
        }
    }
    if(port==50001&&ip!=lk_my_ip) {   /* research: everything another player sends on 50001 */
        static unsigned said;
        if(said<60&&kind!=0x28){said++;char hex[200];lp_hex(hex,sizeof hex,p,n<90?n:90);
            pthread_mutex_unlock(&lk_lock);lp_log("50001 from %u.%u.%u.%u kind 0x%02x len %zu: %s",ip>>24,(ip>>16)&255,(ip>>8)&255,ip&255,kind,n,hex);pthread_mutex_lock(&lk_lock);}
    }
    if(port==50001&&kind==0x26&&n>=0x28) {
        pthread_mutex_unlock(&lk_lock);lb_on_request(ip,p,n);pthread_mutex_lock(&lk_lock);
    } else if(port==50001&&kind==0x28&&n>=0x60) {
        struct lk_player *pl=lk_slot(ip,p[0x21]);
        if(pl){pl->beat_count++;pl->last_beat_ms=now;pl->beat_in_bar=p[0x5c];pl->bpm_x100=(uint32_t)p[0x5a]<<8|p[0x5b];}
    }
    pthread_mutex_unlock(&lk_lock);
}
/* The 0x36-byte player keep-alive (as prodjlink-lab/fake_xdj.py keepalive()). */
static void lk_keepalive_n(unsigned char p[0x36],const unsigned char mac[6],uint32_t ip_be,unsigned peers,unsigned number);
static void lk_keepalive(unsigned char p[0x36],const unsigned char mac[6],uint32_t ip_be,unsigned peers) {lk_keepalive_n(p,mac,ip_be,peers,LK_PLAYER);}
static void lk_keepalive_n(unsigned char p[0x36],const unsigned char mac[6],uint32_t ip_be,unsigned peers,unsigned number) {
    memset(p,0,0x36);memcpy(p,lk_magic,10);p[0x0a]=0x06;
    memcpy(p+0x0c,LK_NAME,strlen(LK_NAME));
    p[0x20]=0x01;p[0x21]=0x02;p[0x22]=0x00;p[0x23]=0x36;
    p[0x24]=(unsigned char)number;p[0x25]=0x01;memcpy(p+0x26,mac,6);memcpy(p+0x2c,&ip_be,4);
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
/* Pick (or keep) our number; called before each keep-alive, under no lock. */
static unsigned lk_pick_number(uint64_t now) {
    pthread_mutex_lock(&lk_lock);
    unsigned me=LK_PLAYER,pick=0;
    int mine_taken=me&&lk_taken_ms[me]&&now-lk_taken_ms[me]<5000;
    if(!me||mine_taken) {
        /* the OPUS's USB 1 is shared as player 1 (decks12: EP136 also holds 1-2 for its
         * decks; same address, so the XDJ sees one OPUS on 1). Otherwise the lowest free. */
        for(unsigned k=1;k<=4&&!pick;k++)if(!lk_taken_ms[k]||now-lk_taken_ms[k]>=5000)pick=k;
    }
    pthread_mutex_unlock(&lk_lock);
    if(pick&&pick!=me){__atomic_store_n(&lk_me,pick,__ATOMIC_RELEASE);lk_number_changes++;}
    return LK_PLAYER;
}
/* Read-only copy of the link's UDP 50000 traffic (keep-alives, number claims): a packet
 * socket on the link interface. EP136 keeps its own socket on 50000 untouched. */
static int lk_sniff_open(void) {
    int s=socket(AF_PACKET,SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK,htons(ETH_P_IP));
    if(s<0)return -1;
    unsigned idx=lk_iface[0]?if_nametoindex(lk_iface):0;
    if(idx){struct sockaddr_ll a;memset(&a,0,sizeof a);a.sll_family=AF_PACKET;a.sll_protocol=htons(ETH_P_IP);a.sll_ifindex=(int)idx;
        if(bind(s,(struct sockaddr *)&a,sizeof a)){close(s);return -1;}}
    return s;
}
static void lk_sniff_read(int s) {
    for(int k=0;k<32;k++) {
        unsigned char b[1600];struct sockaddr_ll from;socklen_t fl=sizeof from;
        ssize_t n=recvfrom(s,b,sizeof b,0,(struct sockaddr *)&from,&fl);
        if(n<=0)return;
        if(from.sll_pkttype==PACKET_OUTGOING||n<28||(b[0]>>4)!=4||b[9]!=17)continue;
        size_t ihl=(size_t)(b[0]&15)*4;if((size_t)n<ihl+8)continue;
        unsigned dport=(unsigned)b[ihl+2]<<8|b[ihl+3];
        uint32_t src=lk_be32(b+12);
        if(dport!=50000) {
            const unsigned char *u=b+ihl+8;size_t un=(size_t)n-ihl-8;
            /* Status (50002) and beats/hand-over (50001) from other players: the kernel hands
             * unicast copies to EP136's socket or ours at random (both bound), so the master
             * logic is fed from here, where every packet is seen. */
            if((dport==50001||dport==50002)&&src!=lk_my_ip&&un>0x24&&!memcmp(u,lk_magic,10)) {
                lk_packet(dport,src,u,un);
                continue;
            }
            static unsigned said;
            if(src!=lk_my_ip&&(src>>16)==0xa9feu&&dport>=50000&&dport<50010&&said<40) {
                said++;char hex[200];lp_hex(hex,sizeof hex,u,un<90?un:90);
                lp_log("sniff udp %u from %u.%u.%u.%u len %zu: %s",dport,src>>24,(src>>16)&255,(src>>8)&255,src&255,un,hex);
            }
            continue;
        }
        lk_packet(50000,src,b+ihl+8,(size_t)n-ihl-8);
    }
}
#include "prodjlink_probe.h"
#include "prodjlink_beat.h"
#include <sys/statvfs.h>
static unsigned lk_db_fixed_count(void);
/* Media details (0x06 on 50002, 0xc0 bytes) as a CDJ answers a media query; layout as
 * beat-link's MediaDetails: 0x27 player, 0x2b slot, 0x2c name and 0x6c date (UTF-16BE),
 * 0xa6 tracks, 0xa8 colour, 0xaa media type (1 = rekordbox), 0xab My Settings,
 * 0xae playlists, 0xb0 total and 0xb8 free bytes (BE64). */
static void lk_put_u16be(unsigned char *p,const char *s,size_t max_chars) {
    for(size_t i=0;s[i]&&i<max_chars;i++){p[2*i]=0;p[2*i+1]=(unsigned char)s[i];}
}
static void lk_put_be64(unsigned char *p,uint64_t v){for(int i=7;i>=0;i--){p[i]=(unsigned char)v;v>>=8;}}
static void lk_media_reply(int sock,unsigned slot) {
    unsigned char p[0xc0];memset(p,0,sizeof p);memcpy(p,lk_magic,10);p[0x0a]=0x06;
    memcpy(p+0x0b,LK_NAME,strlen(LK_NAME));
    p[0x1f]=0x01;p[0x20]=0x00;p[0x21]=(unsigned char)LK_PLAYER;p[0x22]=0x00;p[0x23]=sizeof p-0x24;
    p[0x27]=(unsigned char)LK_PLAYER;p[0x2b]=(unsigned char)slot;
    lk_put_u16be(p+0x2c,"OPUS-QUAD USB",31);
    lk_put_u16be(p+0x6c,"2026-11-11",11);
    p[0xa8]=0x00;p[0xaa]=0x01;p[0xab]=0x00;
    struct statvfs v;
    if(ln_root[0]&&!statvfs(ln_root,&v)) {
        lk_put_be64(p+0xb0,(uint64_t)v.f_blocks*v.f_frsize);lk_put_be64(p+0xb8,(uint64_t)v.f_bavail*v.f_frsize);
    }
    struct sockaddr_in to;memset(&to,0,sizeof to);to.sin_family=AF_INET;to.sin_port=htons(50002);to.sin_addr.s_addr=htonl(lk_mq_ip);
    if(sendto(sock,p,sizeof p,0,(struct sockaddr *)&to,sizeof to)==(ssize_t)sizeof p)lk_mq_sent++;else lk_errors++;
    lp_log("media query from %u.%u.%u.%u for slot %u: answered (%u)",lk_mq_ip>>24,(lk_mq_ip>>16)&255,(lk_mq_ip>>8)&255,lk_mq_ip&255,slot,lk_mq_sent);
}
static void *lk_thread(void *arg) {
    (void)arg;
    /* 50000 stays EP136's: keep-alives go out from an unbound socket of ours, so its
     * own announce traffic is never shared or taken. Status and beats: 50002, 50001. */
    int send_sock=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0),one=1;
    if(send_sock>=0)setsockopt(send_sock,SOL_SOCKET,SO_BROADCAST,&one,sizeof one);
    int sock[3]={-1,lk_bind(50001),lk_bind(50002)};
    lk_bind_ok=(send_sock>=0)|(sock[1]>=0)<<1|(sock[2]>=0)<<2;
    {uint32_t ip,bcast;unsigned char mac[6];if(lk_interface(&ip,&bcast,mac))lk_my_ip=ntohl(ip);}
    int sniff=lk_sniff_open();lk_sniff_ok=sniff>=0;
    /* listen 3.5 s before taking a number, so the keep-alives of everyone present are in */
    uint64_t next=lk_now_ms()+(sniff>=0?3500:0),next_status=next,next_census=0;unsigned counter=0;
    while(__atomic_load_n(&lk_enabled,__ATOMIC_ACQUIRE)) {
        uint64_t now=lk_now_ms();
        if(now>=next_census){next_census=now+30000;lp_census();}
        {static uint64_t next_net;if(now>=next_net){next_net=now+2000;lp_net_census();}}
        if(__atomic_load_n(&lp_offer,__ATOMIC_ACQUIRE)) {
            lp_open();
            if(now>=next_status&&send_sock>=0&&LK_PLAYER) {
                next_status=now+200;uint32_t ip,bcast;unsigned char mac[6];
                if(lk_interface(&ip,&bcast,mac)) {
                    struct sockaddr_in to;memset(&to,0,sizeof to);to.sin_family=AF_INET;to.sin_port=htons(50002);to.sin_addr.s_addr=bcast;
                    /* players 1 (+2 with decks12): live deck status (prodjlink_beat.h) */
                    lb_start();
                    for(unsigned pl=LK_PLAYER;pl<=(lk_two&&LK_PLAYER==1?2u:LK_PLAYER);pl++) {
                        struct lb_state s;lb_read(pl,&s);
                        unsigned char st[LP_STATUS_LEN];lb_status(st,pl,&s,++counter,lb_master==pl,pl==LK_PLAYER);
                        if(sendto(send_sock,st,sizeof st,0,(struct sockaddr *)&to,sizeof to)==(ssize_t)sizeof st)lp_status_sent++;else lk_errors++;
                    }
                }
            }
            if(lk_mq_pending&&send_sock>=0){lk_mq_pending=0;lk_media_reply(send_sock,lk_mq_slot);}
        } else lp_close();
        if(now>=next) {
            next=now+1500;uint32_t ip,bcast;unsigned char mac[6];
            if(lk_interface(&ip,&bcast,mac)&&send_sock>=0) {
                lk_my_ip=ntohl(ip);
                unsigned before=LK_PLAYER,me=lk_pick_number(now);
                if(me!=before)lp_log("player number %u (was %u); taken:%s%s%s%s",me,before,
                    lk_taken_ms[1]&&now-lk_taken_ms[1]<5000?" 1":"",lk_taken_ms[2]&&now-lk_taken_ms[2]<5000?" 2":"",
                    lk_taken_ms[3]&&now-lk_taken_ms[3]<5000?" 3":"",lk_taken_ms[4]&&now-lk_taken_ms[4]<5000?" 4":"");
                if(!me)goto no_keepalive;   /* 1-4 all taken: stay quiet */
                unsigned peers=1;pthread_mutex_lock(&lk_lock);
                for(unsigned i=0;i<LK_MAX;i++)if(lk_players[i].ip&&now-lk_players[i].seen_ms<5000)peers++;
                pthread_mutex_unlock(&lk_lock);
                unsigned char ka[0x36];lk_keepalive(ka,mac,ip,peers);
                struct sockaddr_in to;memset(&to,0,sizeof to);to.sin_family=AF_INET;to.sin_port=htons(50000);to.sin_addr.s_addr=bcast;
                if(sendto(send_sock,ka,sizeof ka,0,(struct sockaddr *)&to,sizeof to)==(ssize_t)sizeof ka)lk_sent++;else lk_errors++;
                /* decks12: deck 2 is player 2 too (same address, like the XDJ-AZ's decks) */
                if(lk_two&&me==1&&!(lk_taken_ms[2]&&now-lk_taken_ms[2]<5000)) {
                    lk_keepalive_n(ka,mac,ip,peers,2);
                    if(sendto(send_sock,ka,sizeof ka,0,(struct sockaddr *)&to,sizeof to)==(ssize_t)sizeof ka)lk_sent++;else lk_errors++;
                }
            }
            no_keepalive:;
        }
        fd_set set;FD_ZERO(&set);int top=-1;
        for(int i=0;i<3;i++)if(sock[i]>=0){FD_SET(sock[i],&set);if(sock[i]>top)top=sock[i];}
        if(sniff>=0){FD_SET(sniff,&set);if(sniff>top)top=sniff;}
        lp_fdset(&set,&top);
        struct timeval tv={0,200000};
        if(top<0){usleep(200000);continue;}
        if(select(top+1,&set,0,0,&tv)<=0)continue;
        lp_serve(&set);
        if(sniff>=0&&FD_ISSET(sniff,&set))lk_sniff_read(sniff);
        for(int i=0;i<3;i++)if(sock[i]>=0&&FD_ISSET(sock[i],&set)) {
            unsigned char buf[1500];struct sockaddr_in from;socklen_t fl=sizeof from;
            ssize_t n=recvfrom(sock[i],buf,sizeof buf,0,(struct sockaddr *)&from,&fl);
            if(n>0)lk_packet(50000u+(unsigned)i,ntohl(from.sin_addr.s_addr),buf,(size_t)n);
        }
    }
    for(int i=0;i<3;i++)if(sock[i]>=0)close(sock[i]);
    if(send_sock>=0)close(send_sock);
    if(sniff>=0)close(sniff);
    __atomic_store_n(&lk_me,0,__ATOMIC_RELEASE);
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
    if(LK_PLAYER)n+=(size_t)snprintf(out+n,cap-n,"OPUS as player %u on %s %s; sent %u, received %u%s\n",LK_PLAYER,
        lk_iface[0]?lk_iface:"-",lk_addr[0]?lk_addr:"(no address)",lk_sent,lk_rx,lk_bind_ok==7?"":" (port in use)");
    else n+=(size_t)snprintf(out+n,cap-n,"Looking for a free player number (1-4) ...\n");
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
    if(lp_offer&&n<cap)snprintf(out+n,cap-n,"Offering the USB: %u status sent, %u requests, NFS %u/%u/%u%s, welcome fixed %u\n",lp_status_sent,lp_requests,ln_calls[0],ln_calls[1],ln_calls[2],ln_root[0]?"":" (no USB found)",lk_db_fixed_count());
    pthread_mutex_unlock(&lk_lock);
}
static void lk_report(FILE *out) {
    fprintf(out,"prodjlink_link enabled=%u running=%u iface=%s addr=%s bind=%u sniff=%u player=%u number_changes=%u media_queries=%u/%u sent=%u rx=%u errors=%u\n",
            lk_enabled,lk_running,lk_iface,lk_addr,lk_bind_ok,lk_sniff_ok,LK_PLAYER,lk_number_changes,lk_mq_count,lk_mq_sent,lk_sent,lk_rx,lk_errors);
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

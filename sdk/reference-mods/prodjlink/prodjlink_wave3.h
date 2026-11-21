/* 1.64: 3-band waveforms (detail PWV7 + overview PWV6) for tracks of other players.
 *
 * EP136 1.32 asks another player's dbserver for them with request kind 0x2d04 (rmst, track,
 * 'PWV7'/'PWV6', '2EX'), built by DetailedWaveform_3BandRequestTicket (0x1495180) and
 * OverviewWaveform_3BandRequestTicket (0x14bbf98). An XDJ-1000MK2 doesn't know 0x2d04, so EP136
 * shows the blue waveform. Changing EP136's request (1.61 on the wire, 1.62 in the ticket)
 * failed on hardware. Here EP136's request never reaches the player: the mod answers it
 * itself on the same connection, with the PWV7 it fetches over its own connection using the
 * classic 0x2c04 request (which the XDJ answers: 141378 bytes per track, seen many times).
 * The overview PWV6 is made from that PWV7 (the XDJ is asked for nothing else).
 *
 * The reply EP136 expects (its argument table 0xd0be10 and both reply parsers, 0x14956d0 and
 * 0x14a0f80): kind 0x4f02, same transaction id, 5 arguments num/num/num/blob/num:
 *   [0] the request kind, [1] 0 (non-zero = failure), [2] blob length, [3] blob, [4] 0.
 * Blob = u32 LITTLE-endian length of what follows, then the ANLZ section as in the file
 * (fourcc, header length, tag length, ... big-endian; EP136 swaps it itself).
 * If the mod can't get the data, EP136's request is sent on as before (blue waveform). */
#ifndef PRODJLINK_WAVE3_H
#define PRODJLINK_WAVE3_H
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
static unsigned w3_on=1;   /* option wave3band */

/* ---- PWV7 cache (filled by the track fetch in prodjlink_link.h and by w3_request) ---- */
#define W3_CACHE 6
struct w3_entry {uint32_t ip,track;unsigned char *pwv7;uint32_t len;uint64_t used;};
static struct w3_entry w3_cache[W3_CACHE];
static pthread_mutex_t w3_lock=PTHREAD_MUTEX_INITIALIZER;
static uint64_t w3_clock;
static void w3_store(uint32_t ip,uint32_t track,unsigned char *data,uint32_t len) {   /* takes data */
    if(!data||len<28||memcmp(data,"PWV7",4)){free(data);return;}
    pthread_mutex_lock(&w3_lock);
    struct w3_entry *e=0;
    for(unsigned i=0;i<W3_CACHE;i++)if(w3_cache[i].ip==ip&&w3_cache[i].track==track){e=&w3_cache[i];break;}
    if(!e){e=&w3_cache[0];for(unsigned i=1;i<W3_CACHE;i++)if(w3_cache[i].used<e->used)e=&w3_cache[i];}
    free(e->pwv7);e->ip=ip;e->track=track;e->pwv7=data;e->len=len;e->used=++w3_clock;
    pthread_mutex_unlock(&w3_lock);
}
static unsigned char *w3_get(uint32_t ip,uint32_t track,uint32_t *len) {   /* a copy, or 0 */
    unsigned char *out=0;*len=0;
    pthread_mutex_lock(&w3_lock);
    for(unsigned i=0;i<W3_CACHE;i++)if(w3_cache[i].ip==ip&&w3_cache[i].track==track&&w3_cache[i].pwv7) {
        out=malloc(w3_cache[i].len);if(out){memcpy(out,w3_cache[i].pwv7,w3_cache[i].len);*len=w3_cache[i].len;w3_cache[i].used=++w3_clock;}
        break;
    }
    pthread_mutex_unlock(&w3_lock);
    return out;
}
static uint32_t w3_be32(const unsigned char *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void w3_put32(unsigned char *p,uint32_t v){p[0]=(unsigned char)(v>>24);p[1]=(unsigned char)(v>>16);p[2]=(unsigned char)(v>>8);p[3]=(unsigned char)v;}

/* PWV6 (1200 columns, 3 bytes each) from a PWV7 section: mean of each column's PWV7 entries,
 * scaled per band (fitted against the PWV6 rekordbox writes, 6 tracks: mean error 3-5 of ~80). */
static unsigned char *w3_pwv6_from_pwv7(const unsigned char *p7,uint32_t len,uint32_t *out_len) {
    *out_len=0;
    if(len<24||memcmp(p7,"PWV7",4))return 0;
    uint32_t hl=w3_be32(p7+4),eb=w3_be32(p7+12),n=w3_be32(p7+16);
    if(hl<20||hl>len||eb!=3||!n||(uint64_t)hl+3ull*n>len)return 0;
    const unsigned char *e=p7+hl;
    const uint32_t cols=1200,size=20+3*cols;
    unsigned char *o=calloc(1,size);if(!o)return 0;
    memcpy(o,"PWV6",4);w3_put32(o+4,20);w3_put32(o+8,size);w3_put32(o+12,3);w3_put32(o+16,cols);
    static const unsigned k1000[3]={600,575,670};
    for(uint32_t c=0;c<cols;c++) {
        uint32_t a=(uint32_t)((uint64_t)c*n/cols),b=(uint32_t)((uint64_t)(c+1)*n/cols);if(b<=a)b=a+1;if(b>n)b=n;
        for(unsigned band=0;band<3;band++) {
            uint32_t sum=0;for(uint32_t i=a;i<b;i++)sum+=e[3*i+band];
            uint32_t v=(uint32_t)(((uint64_t)sum*k1000[band]+(uint64_t)(b-a)*500)/((uint64_t)(b-a)*1000));
            o[20+3*c+band]=(unsigned char)(v>255?255:v);
        }
    }
    *out_len=size;return o;
}

/* 1.67: PWV7 from the colour waveform PWV5 (tracks analysed without 3-band data). Same column
 * count. PWV5 entry (u16 BE): red bits 13-15, green 10-12, blue 7-9, height 2-6. PWV7 bytes
 * follow red*height (low), green*height (mid), blue*height (high); scales fitted on 8 tracks
 * having both (error about a quarter of the mean level). */
static unsigned char *w3_pwv7_from_pwv5(const unsigned char *p5,uint32_t len,uint32_t *out_len) {
    *out_len=0;
    if(len<24||memcmp(p5,"PWV5",4))return 0;
    uint32_t hl=w3_be32(p5+4),eb=w3_be32(p5+12),n=w3_be32(p5+16);
    if(hl<20||hl>len||eb!=2||!n||(uint64_t)hl+2ull*n>len)return 0;
    uint32_t size=24+3*n;unsigned char *o=calloc(1,size);if(!o)return 0;
    memcpy(o,"PWV7",4);w3_put32(o+4,24);w3_put32(o+8,size);w3_put32(o+12,3);w3_put32(o+16,n);w3_put32(o+20,0x00960000u);
    static const unsigned k1000[3]={470,520,290};static const unsigned shift[3]={13,10,7};
    for(uint32_t i=0;i<n;i++) {
        unsigned v=(unsigned)p5[hl+2*i]<<8|p5[hl+2*i+1],h=(v>>2)&31u;
        for(unsigned b=0;b<3;b++){unsigned x=((v>>shift[b])&7u)*h*k1000[b]/1000u;o[24+3*i+b]=(unsigned char)(x>255?255:x);}
    }
    *out_len=size;return o;
}

static unsigned w3_served,w3_fetched,w3_cached,w3_passed,w3_errors;
/* one whole dbserver message (or a lone field, e.g. the 5-byte greeting) as raw bytes */
struct w3_raw {unsigned char *b;size_t n,cap;};
static int w3_raw_room(struct w3_raw *r,size_t n) {
    if(r->n+n>r->cap){size_t c=r->cap?r->cap*2:4096;while(c<r->n+n)c*=2;unsigned char *x=realloc(r->b,c);if(!x)return -1;r->b=x;r->cap=c;}
    return 0;
}
static int w3_raw_add(struct w3_raw *r,const void *p,size_t n) {
    if(w3_raw_room(r,n))return -1;
    memcpy(r->b+r->n,p,n);r->n+=n;return 0;
}
static int w3_raw_field(int fd,struct w3_raw *r,uint32_t *v,unsigned char *tags,uint32_t *tl) {
    unsigned char t;if(lkdb_io(fd,&t,1,0)||w3_raw_add(r,&t,1))return -1;
    if(t==0x0f||t==0x10||t==0x11) {
        unsigned n=t==0x0f?1:t==0x10?2:4;unsigned char b[4];if(lkdb_io(fd,b,n,0)||w3_raw_add(r,b,n))return -1;
        uint32_t x=0;for(unsigned i=0;i<n;i++)x=x<<8|b[i];*v=x;return t;
    }
    if(t==0x14||t==0x26) {
        unsigned char b[4];if(lkdb_io(fd,b,4,0)||w3_raw_add(r,b,4))return -1;
        uint32_t n=w3_be32(b);if(t==0x26)n*=2;if(n>(16u<<20))return -1;
        size_t at=r->n;
        if(n){if(w3_raw_room(r,n)||lkdb_io(fd,r->b+at,n,0))return -1;r->n+=n;}
        if(tags&&tl){uint32_t k=n<64?n:64;memcpy(tags,r->b+at,k);*tl=k;}
        *v=n;return t;
    }
    return -1;
}
/* returns the bytes; *whole = 1 for a message, 0 for a lone field (greeting) */
static unsigned char *w3_read_msg(int fd,size_t *len,int *whole) {
    struct w3_raw r={0};uint32_t v,count=0,kind=0,num[64]={0};unsigned char tags[64];uint32_t tl=0;
    *whole=0;
    int t=w3_raw_field(fd,&r,&v,0,0);
    if(t<0){free(r.b);return 0;}
    if(t!=0x11||v!=LKDB_START){*len=r.n;return r.b;}
    int ok=w3_raw_field(fd,&r,&v,0,0)>0&&w3_raw_field(fd,&r,&kind,0,0)>0&&w3_raw_field(fd,&r,&count,0,0)>0
        &&w3_raw_field(fd,&r,&v,tags,&tl)==0x14&&count<=64;
    for(uint32_t i=tl;i<64;i++)tags[i]=6;
    for(uint32_t i=0;ok&&i<count;i++) {
        unsigned ty=tags[i];
        if(ty==3&&i&&num[i-1]==0)continue;   /* a player leaves an empty blob out */
        if(w3_raw_field(fd,&r,&num[i],0,0)<0)ok=0;
    }
    if(!ok){free(r.b);w3_errors++;return 0;}
    *whole=1;*len=r.n;return r.b;
}
/* the 0x4f02 reply around one ANLZ section */
static unsigned char *w3_reply(uint32_t txid,uint32_t req_kind,const unsigned char *section,uint32_t slen,size_t *out_len) {
    uint32_t blen=4+slen;size_t len=32+5+5+5+5+blen+5;
    unsigned char *m=malloc(len);if(!m)return 0;
    size_t o=0;
    m[o++]=0x11;w3_put32(m+o,0x872349aeu);o+=4;
    m[o++]=0x11;w3_put32(m+o,txid);o+=4;
    m[o++]=0x10;m[o++]=0x4f;m[o++]=0x02;
    m[o++]=0x0f;m[o++]=5;
    m[o++]=0x14;w3_put32(m+o,12);o+=4;
    static const unsigned char tags[12]={6,6,6,3,6,0,0,0,0,0,0,0};memcpy(m+o,tags,12);o+=12;
    m[o++]=0x11;w3_put32(m+o,req_kind);o+=4;
    m[o++]=0x11;w3_put32(m+o,0);o+=4;
    m[o++]=0x11;w3_put32(m+o,blen);o+=4;
    m[o++]=0x14;w3_put32(m+o,blen);o+=4;
    m[o++]=(unsigned char)slen;m[o++]=(unsigned char)(slen>>8);m[o++]=(unsigned char)(slen>>16);m[o++]=(unsigned char)(slen>>24);
    memcpy(m+o,section,slen);o+=slen;
    m[o++]=0x11;w3_put32(m+o,0);o+=4;
    *out_len=o;return m;
}
/* the player's PWV7 for this track: cache, else asked for on the proxy's connection to the
 * player (in step with EP136's requests: the proxy handles one at a time). No PWV7 (track
 * analysed without 3-band data): made from the colour waveform PWV5. */
static unsigned char *w3_pwv7(int real,uint32_t ip,unsigned slot,uint32_t track,uint32_t *len,const char **from) {
    unsigned char *d=w3_get(ip,track,len);
    if(d){*from="cache";w3_cached++;return d;}
    unsigned me=LK_PLAYER;if(!me)return 0;
    unsigned char *w=0;uint32_t wl=0;
    int ok=!lkdb_anlz_tag(real,me,slot,track,"PWV7","2EX",&w,&wl);
    if(ok)*from="player PWV7";
    else {
        free(w);w=0;unsigned char *c=0;uint32_t cl=0;
        if(!lkdb_anlz_tag(real,me,slot,track,"PWV5","EXT",&c,&cl)){w=w3_pwv7_from_pwv5(c,cl,&wl);ok=w!=0;if(ok)*from="made from PWV5";}
        free(c);
    }
    if(!ok){free(w);return 0;}
    w3_fetched++;
    unsigned char *copy=malloc(wl);if(copy){memcpy(copy,w,wl);w3_store(ip,track,copy,wl);}
    *len=wl;return w;
}
/* EP136's 0x2d04 request for PWV7/PWV6 from the 2EX: the reply, or 0 (then it goes on to the
 * player as before). The argument-type blob has its own length (EP136: 4 bytes, 44-byte request). */
static unsigned char *w3_answer(int real,const unsigned char *b,size_t n,uint32_t ip,size_t *rl) {
    *rl=0;
    if(!w3_on||n<44||b[0]!=0x11||b[5]!=0x11||b[10]!=0x10||b[11]!=0x2d||b[12]!=0x04||b[13]!=0x0f||b[14]!=4||b[15]!=0x14)return 0;
    uint32_t tl=w3_be32(b+16);size_t at=20+(size_t)tl;
    if(tl>64||at+20!=n||b[at]!=0x11||b[at+5]!=0x11||b[at+10]!=0x11||b[at+15]!=0x11)return 0;
    uint32_t txid=w3_be32(b+6),rmst=w3_be32(b+at+1),track=w3_be32(b+at+6),tag=w3_be32(b+at+11),ext=w3_be32(b+at+16);
    int detail=tag==0x37565750u,overview=tag==0x36565750u;
    if((!detail&&!overview)||ext!=0x00584532u)return 0;
    uint64_t t0=lk_now_ms();const char *from="none";uint32_t l7=0;
    unsigned char *p7=w3_pwv7(real,ip,(rmst>>8)&255u,track,&l7,&from);
    unsigned char *sec=p7;uint32_t slen=l7;
    if(p7&&overview){sec=w3_pwv6_from_pwv7(p7,l7,&slen);free(p7);}
    unsigned char *reply=sec?w3_reply(txid,0x2d04u,sec,slen,rl):0;
    free(sec);
    if(reply)w3_served++;else w3_passed++;
    static unsigned said;
    if(said<40){said++;lp_log("wave3: %s request track %u: %s (%s, %u bytes, %llu ms)",detail?"PWV7 detail":"PWV6 overview",track,
        reply?"answered by the mod":"sent on to the player",from,(unsigned)slen,(unsigned long long)(lk_now_ms()-t0));}
    return reply;
}

/* ---- 1.69: the proxy ----------------------------------------------------------------------
 * 1.64-1.68 answered on EP136's own socket through its read/select/poll calls; EP136 doesn't
 * wait on the socket only through those (blank or blocky waveforms: its request timed out). Now
 * EP136's connection to another player's dbserver (any port but the 12523 port query) is a
 * local socket pair: the mod holds the real connection and a thread passes everything through,
 * one request at a time, in order - so EP136 sees real data on its socket however it waits.
 * It answers EP136's 3-band requests itself and sets the requester number (as db_req_fix did). */
struct w3_proxy {int ep,real;uint32_t ip;unsigned port;int epfd;};
/* 1.75: the mod's own track fetch (beat grid + waveform, prodjlink_link.h) through the proxy.
 * A player's dbserver ignores a second client while EP136's connection is open (1.66, 1.74:
 * every fetch "no grid"), so the proxy runs the fetch on its connection, between EP136's
 * requests (only when no reply to EP136 is outstanding). */
struct w3_fetch {uint32_t ip,track;unsigned slot;int state;   /* 1 queued, 2 running, 3 done */
    int beats;struct lkdb_beat *grid;unsigned char *wave;uint32_t wave_len;const char *kind;};
static struct w3_fetch w3_fetch_job;
static pthread_mutex_t w3_fetch_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t w3_fetch_cond=PTHREAD_COND_INITIALIZER;
static unsigned w3_live_proxies[8];static uint32_t w3_live_ip[8];
static void w3_proxy_alive(uint32_t ip,int up){
    pthread_mutex_lock(&w3_fetch_lock);
    for(unsigned i=0;i<8;i++)if(w3_live_ip[i]==ip||(!w3_live_ip[i]&&up)){if(up){w3_live_ip[i]=ip;w3_live_proxies[i]++;}else if(w3_live_proxies[i]&&!--w3_live_proxies[i])w3_live_ip[i]=0;break;}
    pthread_mutex_unlock(&w3_fetch_lock);
}
/* from the fetch thread: 1 = done through a proxy (results filled), 0 = no proxy for ip */
static int w3_proxy_fetch(uint32_t ip,unsigned slot,uint32_t track,struct lkdb_beat **grid,int *beats,unsigned char **wave,uint32_t *wave_len,const char **kind) {
    pthread_mutex_lock(&w3_fetch_lock);
    int live=0;for(unsigned i=0;i<8;i++)if(w3_live_ip[i]==ip&&w3_live_proxies[i])live=1;
    if(!live||w3_fetch_job.state==1||w3_fetch_job.state==2){pthread_mutex_unlock(&w3_fetch_lock);return 0;}
    memset(&w3_fetch_job,0,sizeof w3_fetch_job);w3_fetch_job.ip=ip;w3_fetch_job.slot=slot;w3_fetch_job.track=track;w3_fetch_job.state=1;w3_fetch_job.beats=-1;w3_fetch_job.kind="none";
    struct timespec until;clock_gettime(CLOCK_REALTIME,&until);until.tv_sec+=12;
    while(w3_fetch_job.state!=3){if(pthread_cond_timedwait(&w3_fetch_cond,&w3_fetch_lock,&until))break;}
    int done=w3_fetch_job.state==3;
    if(done){*grid=w3_fetch_job.grid;*beats=w3_fetch_job.beats;*wave=w3_fetch_job.wave;*wave_len=w3_fetch_job.wave_len;*kind=w3_fetch_job.kind;}
    else if(w3_fetch_job.state==1)w3_fetch_job.state=0;   /* not picked up: give up the slot */
    w3_fetch_job.grid=0;w3_fetch_job.wave=0;if(done)w3_fetch_job.state=0;
    pthread_mutex_unlock(&w3_fetch_lock);
    return done;
}
/* 1.80 (research, user: "show loops, memory cues and hot cues on the PRO DJ LINK deck"):
 * EP136 asks the player for the extended cue list (0x2b04) and shows none. Everything the
 * player has about the track's cues is saved to cues-<track>.txt beside link-probe.txt: the
 * replies to 0x2104 (cue list) and 0x2b04 (extended), and the ANLZ cue tags PCOB (DAT) and
 * PCO2 (EXT), each argument with its full bytes in hex. */
static void w3_cue_hex(FILE *f,const unsigned char *b,uint32_t n) {
    for(uint32_t i=0;i<n;i++){fprintf(f,"%02x",b[i]);if(i%64==63)fputc('\n',f);}
    fputc('\n',f);
}
static void w3_cue_msg(FILE *f,const char *what,int s,uint32_t kind,const uint32_t *a,unsigned n) {
    struct lkdb_msg r;
    if(lkdb_request(s,7,kind,a,n)){fprintf(f,"%s: request not sent\n",what);return;}
    if(lkdb_response(s,&r)){fprintf(f,"%s: no reply\n",what);return;}
    fprintf(f,"%s: reply %04x, %u args\n",what,r.kind,r.count);
    for(unsigned i=0;i<r.count&&i<LKDB_ARGS;i++) {
        if(r.blob[i]){fprintf(f," arg %u: %u bytes\n",i,r.blob_len[i]);w3_cue_hex(f,r.blob[i],r.blob_len[i]);}
        else fprintf(f," arg %u: %u (0x%x)\n",i,r.num[i],r.num[i]);
    }
    lp_log("cues: %s for this track: reply %04x, %u args, arg 3 %u bytes",what,r.kind,r.count,r.count>3?r.blob_len[3]:0);
    unsigned menu=r.kind==0x4001;uint32_t items=r.count>1?r.num[1]:0;
    lkdb_free(&r);
    if(!menu)return;
    /* 1.81: a menu header (the XDJ-1000MK2's answer to 0x2b04): render it like EP136 does */
    uint32_t lim=items&&items<200?items:64;
    uint32_t ra[6]={a[0],0,lim,0,lim,0};
    if(lkdb_request(s,8,0x3000,ra,6)){fprintf(f," render: not sent\n");return;}
    for(unsigned k=0;k<300;k++) {
        if(lkdb_response(s,&r)){fprintf(f," render: no reply\n");return;}
        fprintf(f," render message %u: %04x, %u args\n",k,r.kind,r.count);
        for(unsigned i=0;i<r.count&&i<LKDB_ARGS;i++) {
            if(r.blob[i]){fprintf(f,"  arg %u: %u bytes\n",i,r.blob_len[i]);w3_cue_hex(f,r.blob[i],r.blob_len[i]);}
            else fprintf(f,"  arg %u: %u (0x%x)\n",i,r.num[i],r.num[i]);
        }
        unsigned kind=r.kind;lkdb_free(&r);
        if(kind!=0x4001&&kind!=0x4101)break;
    }
}
static void w3_cue_capture(int s,unsigned me,unsigned slot,uint32_t track) {
    if(PDL_RELEASE)return;   /* 2.0: research capture off in the release build */
    static uint32_t done[8];static unsigned nd;
    for(unsigned i=0;i<nd;i++)if(done[i]==track)return;
    if(nd>=8)return;
    done[nd++]=track;
    char path[300];snprintf(path,sizeof path,"%s",lp_path);char *slash=strrchr(path,'/');if(!slash)return;
    snprintf(slash+1,sizeof path-(size_t)(slash+1-path),"cues-%u.txt",track);
    FILE *f=fopen(path,"w");if(!f){lp_log("cues: cannot write %s",path);return;}
    fprintf(f,"track %u slot %u\n",track,slot);
    uint32_t a2[2]={lkdb_rmst(me,8,slot),track},a3[3]={lkdb_rmst(me,8,slot),track,0};
    w3_cue_msg(f,"0x2104 cue list",s,0x2104,a2,2);
    w3_cue_msg(f,"0x2b04 extended cue list",s,0x2b04,a3,3);
    static const char *const tags[][2]={{"PCOB","DAT"},{"PCO2","EXT"},{"PCOB","EXT"}};
    for(unsigned i=0;i<3;i++) {
        unsigned char *b=0;uint32_t bl=0;
        if(!lkdb_anlz_tag(s,me,slot,track,tags[i][0],tags[i][1],&b,&bl)){fprintf(f,"ANLZ %s.%s: %u bytes\n",tags[i][0],tags[i][1],bl);w3_cue_hex(f,b,bl);}
        else fprintf(f,"ANLZ %s.%s: none\n",tags[i][0],tags[i][1]);
        free(b);
    }
    fclose(f);sync();
    lp_log("cues: track %u saved to %s",track,path);
}
static void w3_proxy_run_fetch(const struct w3_proxy *px) {   /* proxy thread, between requests */
    pthread_mutex_lock(&w3_fetch_lock);
    if(w3_fetch_job.state!=1||w3_fetch_job.ip!=px->ip){pthread_mutex_unlock(&w3_fetch_lock);return;}
    w3_fetch_job.state=2;unsigned slot=w3_fetch_job.slot;uint32_t track=w3_fetch_job.track;
    pthread_mutex_unlock(&w3_fetch_lock);
    unsigned me=LK_PLAYER;struct lkdb_beat *b=0;int beats=-1;unsigned char *w=0;uint32_t wl=0;const char *kind="none";
    if(me) {
        beats=lkdb_beat_grid(px->real,me,slot,track,&b);
        if(!lkdb_anlz_tag(px->real,me,slot,track,"PWV7","2EX",&w,&wl))kind="PWV7";
        else {free(w);w=0;if(!lkdb_anlz_tag(px->real,me,slot,track,"PWV5","EXT",&w,&wl))kind="PWV5";else{free(w);w=0;wl=0;}}
        w3_cue_capture(px->real,me,slot,track);   /* 1.80 research */
    }
    lp_log("proxy %u.%u.%u.%u:%u: fetched track %u for the mod (grid %d beats, %s)",px->ip>>24,(px->ip>>16)&255,(px->ip>>8)&255,px->ip&255,px->port,track,beats,kind);
    pthread_mutex_lock(&w3_fetch_lock);
    w3_fetch_job.grid=b;w3_fetch_job.beats=beats;w3_fetch_job.wave=w;w3_fetch_job.wave_len=wl;w3_fetch_job.kind=kind;w3_fetch_job.state=3;
    pthread_cond_broadcast(&w3_fetch_cond);pthread_mutex_unlock(&w3_fetch_lock);
}
static unsigned w3_proxies,w3_proxy_lines;
/* EP136's side of each proxied connection, so its getpeername()/setsockopt() on that socket
 * behave as on the TCP connection it thinks it has */
#define W3_PX 16
static struct {int fd;struct sockaddr_in to;} w3_px[W3_PX];
static void w3_px_set(int fd,const struct sockaddr_in *to) {
    pthread_mutex_lock(&w3_lock);int k=-1;
    for(int i=0;i<W3_PX;i++){if(w3_px[i].fd==fd){k=i;break;}if(k<0&&!w3_px[i].fd)k=i;}
    if(k>=0){if(to){w3_px[k].fd=fd;w3_px[k].to=*to;}else w3_px[k].fd=0;}
    pthread_mutex_unlock(&w3_lock);
}
static int w3_px_get(int fd,struct sockaddr_in *to) {
    if(fd<3||!__atomic_load_n(&w3_proxies,__ATOMIC_ACQUIRE))return 0;
    int r=0;pthread_mutex_lock(&w3_lock);
    for(int i=0;i<W3_PX;i++)if(w3_px[i].fd==fd){*to=w3_px[i].to;r=1;break;}
    pthread_mutex_unlock(&w3_lock);
    if(r){struct sockaddr_storage ss;socklen_t sl=sizeof ss;   /* still our socket pair? */
        if(getsockname(fd,(struct sockaddr *)&ss,&sl)||ss.ss_family!=AF_UNIX){w3_px_set(fd,0);r=0;}}
    return r;
}
/* not a dbserver conversation: pass bytes both ways untouched */
static void w3_relay(int a,int b,const unsigned char *first,size_t n) {
    if(n&&lkdb_io(b,(void *)first,n,1))return;
    unsigned char buf[8192];
    for(;;){struct pollfd pf[2]={{a,POLLIN,0},{b,POLLIN,0}};
        if(poll(pf,2,-1)<0){if(errno==EINTR)continue;return;}
        for(int i=0;i<2;i++)if(pf[i].revents){ssize_t r=recv(pf[i].fd,buf,sizeof buf,0);if(r<=0)return;if(lkdb_io(i?a:b,buf,(size_t)r,1))return;}}
}
static int w3_sendall(int fd,const void *b,size_t n){return lkdb_io(fd,(void *)b,n,1);}
static void w3_fix_requester(unsigned char *b,size_t n) {   /* our player number, as db_req_fix */
    unsigned me=LK_PLAYER;
    if(!me||n<20||b[10]!=0x10||b[13]!=0x0f||b[15]!=0x14)return;
    size_t at=20+w3_be32(b+16);if(b[14]<1||at+5>n||b[at]!=0x11)return;
    unsigned kind=(unsigned)b[11]<<8|b[12];
    if(kind==0){b[at+1]=b[at+2]=b[at+3]=0;b[at+4]=(unsigned char)me;}
    else if(b[at+1]!=me&&b[at+1]>=5)b[at+1]=(unsigned char)me;
}
/* 1.70: two-way. A reply can be several messages (0x3000 "render": header 0x4001, one 0x4101
 * per row, footer 0x4201; 1.69 passed one message per request and browsing hung on
 * "waiting..."). Whatever the player sends goes to EP136 at once. The proxy only keeps count of
 * the requests it has sent on whose reply is not complete yet, so that its own 3-band answer
 * is written after them. */
#define W3_OUT 64
struct w3_px_state {unsigned short kinds[W3_OUT];unsigned head,count;int in_menu;unsigned passed,handled,relayed;};
static void w3_out_push(struct w3_px_state *st,unsigned kind) {
    if(st->count<W3_OUT){st->kinds[(st->head+st->count)%W3_OUT]=(unsigned short)kind;st->count++;}
    else {st->count=0;st->in_menu=0;}   /* lost track: start again (order no longer guaranteed) */
}
static void w3_out_reply(struct w3_px_state *st,unsigned reply_kind) {   /* one message from the player */
    if(!st->count)return;
    if(st->kinds[st->head]==0x3000) {
        if(!st->in_menu&&reply_kind==0x4001){st->in_menu=1;return;}
        if(st->in_menu&&reply_kind!=0x4201)return;
    }
    st->in_menu=0;st->head=(st->head+1)%W3_OUT;st->count--;
}
/* 1.79: a menu row (0x4101) of type 0x0b is a track's duration (arg 1, seconds). Logged, to
 * compare with the length EP136 shows on the deck (hardware 2026-11-13: 7400 ms for a track of
 * about 3 minutes; the deck stopped there). */
static unsigned w3_dur_lines;
static void w3_log_duration(const unsigned char *b,size_t n) {
    if(w3_dur_lines>=60||n<20||b[10]!=0x10||b[11]!=0x41||b[12]!=0x01)return;
    size_t at=13;uint32_t num[12]={0};unsigned argc=0;
    if(b[at]!=0x0f)return;
    argc=b[at+1];at+=2;
    if(at+5>n||b[at]!=0x14)return;
    at+=5+w3_be32(b+at+1);
    for(unsigned i=0;i<argc&&i<12&&at<n;i++) {
        unsigned char t=b[at++];
        if(t==0x0f||t==0x10||t==0x11){unsigned k=t==0x0f?1:t==0x10?2:4;if(at+k>n)return;uint32_t x=0;for(unsigned j=0;j<k;j++)x=x<<8|b[at+j];num[i]=x;at+=k;}
        else if(t==0x14||t==0x26){if(at+4>n)return;uint32_t k=w3_be32(b+at);if(t==0x26)k*=2;at+=4+k;}
        else return;
    }
    if(argc>6&&(num[6]&0xffff)==0x0b){w3_dur_lines++;lp_log("proxy: the player gives the track's duration as %u s",num[1]);}
}
/* 1.81 (research): EP136's own cue conversation with the player, every message both ways from
 * its 0x2b04/0x2104 request on (40 messages or 8 s), to cues-ep136-<n>.txt. On hardware the
 * XDJ-1000MK2 answered EP136's 0x2b04 with 0x4001 (a menu header), and EP136 then rendered. */
static FILE *w3_dump;static unsigned w3_dump_left,w3_dumps;static uint64_t w3_dump_until;
static void w3_dump_msg(const char *dir,const unsigned char *b,size_t n) {
    if(!w3_dump)return;
    if(!w3_dump_left||lk_now_ms()>w3_dump_until){fclose(w3_dump);w3_dump=0;sync();return;}
    w3_dump_left--;
    fprintf(w3_dump,"%s %zu bytes, kind %02x%02x\n",dir,n,n>12?b[11]:0,n>12?b[12]:0);
    w3_cue_hex(w3_dump,b,(uint32_t)n);fflush(w3_dump);
}
static void w3_dump_start(void) {
    if(PDL_RELEASE||w3_dump||w3_dumps>=8)return;   /* 2.0: research dump off in the release build */
    char path[300];snprintf(path,sizeof path,"%s",lp_path);char *slash=strrchr(path,'/');if(!slash)return;
    snprintf(slash+1,sizeof path-(size_t)(slash+1-path),"cues-ep136-%u.txt",++w3_dumps);
    w3_dump=fopen(path,"w");w3_dump_left=160;w3_dump_until=lk_now_ms()+20000;   /* 1.91: the load's metadata too */
    if(w3_dump)lp_log("cues: EP136's cue conversation goes to %s",path);
}
/* 1.81: the mod's capture for each track loaded on a PRO DJ LINK deck (1.80 only ran it with the
 * waveform fetch, which a cached track skips) */
static volatile uint32_t w3_cue_track;static volatile unsigned w3_cue_slot;
static void w3_cue_want(unsigned slot,uint32_t track){w3_cue_slot=slot;__atomic_store_n(&w3_cue_track,track,__ATOMIC_RELEASE);}
/* 1.82: hot cues from the player's extended cue list (0x4e02). EP136 1.32 reads each entry in
 * 0x14aeba0 (from track_info_repository::MemoryHot_CueRequestTicket 0x14b9230): the words at
 * entry bytes 36 and 44 (after the 4-byte length: +32/+40) are -1 in entries it places by time
 * alone (a hot cue set on the XDJ-1000MK2 itself: shown). The XDJ sends rekordbox's cues with 0
 * there; EP136 then takes another route (0x14ab548) and drops them or shows them as memory cues
 * (hardware 2026-11-13, Stealing Fire). Both words become -1. Option cuefix (default 1). */
static unsigned w3_cuefix=1,w3_cueshift=1,w3_cuefix_lines;
static void w3_fix_cues(unsigned char *b,size_t n) {
    if(!w3_cuefix||n<20||b[10]!=0x10||b[11]!=0x4e||b[12]!=0x02||b[13]!=0x0f||b[15]!=0x14)return;
    unsigned argc=b[14];size_t at=20+(size_t)w3_be32(b+16);
    for(unsigned i=0;i<argc&&at<n;i++) {
        unsigned char t=b[at++];
        if(t==0x0f||t==0x10||t==0x11){at+=t==0x0f?1:t==0x10?2:4;continue;}
        if(t!=0x14&&t!=0x26)return;
        if(at+4>n)return;
        uint32_t k=w3_be32(b+at);at+=4;if(t==0x26)k*=2;
        if(i!=3||at+k>n){at+=k;continue;}
        unsigned fixed=0,entries=0;
        for(size_t o=0;o+4<=k;) {
            uint32_t len=(uint32_t)b[at+o]|(uint32_t)b[at+o+1]<<8|(uint32_t)b[at+o+2]<<16|(uint32_t)b[at+o+3]<<24;
            if(len<0x34||o+len>k)break;
            entries++;
            unsigned char *e=b+at+o;
            static const unsigned char ff[4]={0xff,0xff,0xff,0xff},z[4]={0,0,0,0};
            if(len>=52&&!memcmp(e+36,z,4)&&!memcmp(e+44,z,4)){memcpy(e+36,ff,4);memcpy(e+44,ff,4);fixed++;}
            o+=len;
        }
        /* 1.83: hardware 2026-11-13 (1.82): rekordbox hot cues A/B still not shown, the hot cue
         * set on the XDJ (alone at its spot) was. A, B and H each share their spot with a memory
         * cue; EP136 keeps positions in 1/150 s. A memory cue on a hot cue's spot moves 10 ms
         * later (the hot cue stays exact). Option cueshift (default 1). */
        unsigned moved=0;
        if(w3_cueshift)for(size_t o=0;o+4<=k;) {
            unsigned char *e=b+at+o;uint32_t len=(uint32_t)e[0]|(uint32_t)e[1]<<8|(uint32_t)e[2]<<16|(uint32_t)e[3]<<24;
            if(len<0x34||o+len>k)break;
            uint32_t t=(uint32_t)e[12]|(uint32_t)e[13]<<8|(uint32_t)e[14]<<16|(uint32_t)e[15]<<24;
            if(e[4]==0&&e[5]==0&&e[6]==1&&e[10]==0xe8&&e[11]==0x03) {   /* memory cue, ms */
                for(size_t q=0;q+4<=k;) {
                    unsigned char *h=b+at+q;uint32_t hl=(uint32_t)h[0]|(uint32_t)h[1]<<8|(uint32_t)h[2]<<16|(uint32_t)h[3]<<24;
                    if(hl<0x34||q+hl>k)break;
                    uint32_t ht=(uint32_t)h[12]|(uint32_t)h[13]<<8|(uint32_t)h[14]<<16|(uint32_t)h[15]<<24;
                    /* 1.85: hardware (1.84): the shift was not enough for B (a 10 ms gap is under
                     * a pixel; the memory marker is drawn over the hot cue). On a PRO DJ LINK deck
                     * cues are only shown (the XDJ is played), so a memory cue within 50 ms of a
                     * hot cue is taken out of the list (its entry becomes a 0-length end). */
                    if((h[4]||h[5])&&h[10]==0xe8&&h[11]==0x03&&(ht>t?ht-t:t-ht)<50) {
                        e[6]=0xff;moved++;break;}   /* type 0xff: EP136's reader rejects the entry */
                    q+=hl;
                }
            }
            o+=len;
        }
        if(w3_cuefix_lines<20){w3_cuefix_lines++;lp_log("cues: player's cue list, %u entries, %u set to time-only, %u memory cues under a hot cue left out",entries,fixed,moved);}
        return;
    }
}
/* one message player -> EP136; 0 ok */
static int w3_px_relay_one(const struct w3_proxy *px,struct w3_px_state *st) {
    size_t rn=0;int whole=0;unsigned char *r=w3_read_msg(px->real,&rn,&whole);
    if(!r)return -1;
    if(whole&&rn>12&&st->count) {   /* 1.80: what the player answers EP136's cue requests */
        unsigned head=st->kinds[st->head];
        static unsigned cue_lines;
        if((head==0x2b04||head==0x2104)&&cue_lines<20){cue_lines++;char hex[260];lp_hex(hex,sizeof hex,r,rn<120?rn:120);
            lp_log("cues: EP136's %04x request answered with %02x%02x (%zu bytes): %s",head,r[11],r[12],rn,hex);}
    }
    if(whole)w3_fix_cues(r,rn);   /* 1.82 */
    w3_dump_msg("XDJ -> OPUS",r,rn);
    if(whole&&rn>12)w3_out_reply(st,(unsigned)r[11]<<8|r[12]);
    if(whole)w3_log_duration(r,rn);
    int e=w3_sendall(px->ep,r,rn);free(r);st->relayed++;
    return e?-1:0;
}
static void *w3_proxy_thread(void *arg) {
    struct w3_proxy px=*(struct w3_proxy *)arg;free(arg);
    struct w3_px_state st;memset(&st,0,sizeof st);
    unsigned greeted=0;const char *why="closed";
    w3_proxy_alive(px.ip,1);
    for(;;) {
        if(greeted&&!st.count)w3_proxy_run_fetch(&px);   /* 1.75: the mod's fetch, between EP136's requests */
        if(greeted&&!st.count&&!w3_dump) {   /* 1.81 */
            uint32_t ct=__atomic_exchange_n(&w3_cue_track,0u,__ATOMIC_ACQ_REL);
            if(ct&&LK_PLAYER)w3_cue_capture(px.real,LK_PLAYER,w3_cue_slot,ct);
        }
        struct pollfd pf[2]={{px.ep,POLLIN,0},{px.real,POLLIN,0}};
        int pr=poll(pf,2,100);
        if(pr<0){if(errno==EINTR)continue;why="poll";break;}
        if(!pr)continue;
        if(pf[1].revents) {   /* the player has something: on to EP136 */
            if(w3_px_relay_one(&px,&st)){why="player closed";break;}
            continue;
        }
        if(!pf[0].revents)continue;
        size_t n=0;int whole=0;
        unsigned char *m=w3_read_msg(px.ep,&n,&whole);
        if(!m){why="EP136 closed";break;}
        if(!whole&&!greeted&&!(n==5&&m[0]==0x11&&w3_be32(m+1)==1)) {   /* not a dbserver */
            lp_log("proxy %u.%u.%u.%u:%u: not a dbserver conversation, passing bytes through",px.ip>>24,(px.ip>>16)&255,(px.ip>>8)&255,px.ip&255,px.port);
            w3_relay(px.ep,px.real,m,n);free(m);why="relay ended";break;}
        greeted=1;
        if(!whole){int e=w3_sendall(px.real,m,n);free(m);if(e){why="player gone (greeting)";break;}continue;}   /* greeting: its reply comes back as any data */
        unsigned kind=n>12?(unsigned)m[11]<<8|m[12]:0;
        if(w3_proxy_lines<200){w3_proxy_lines++;
            char tg[24]="";if((kind==0x2c04||kind==0x2d04)&&n>=20&&m[n-10]==0x11&&m[n-5]==0x11){const unsigned char *t=m+n-9,*e=m+n-4;snprintf(tg,sizeof tg," %c%c%c%c.%c%c%c",t[3],t[2],t[1],t[0],e[3],e[2],e[1]);}
            if(!LP_QUIET)lp_log("proxy %u.%u.%u.%u:%u: request %04x%s (%u waiting for replies)",px.ip>>24,(px.ip>>16)&255,(px.ip>>8)&255,px.ip&255,px.port,kind,tg,st.count);}
        int ours=kind==0x2d04&&w3_on&&n>=44&&m[14]==4;
        if(ours){size_t at=20+(size_t)w3_be32(m+16);uint32_t tag=at+20==n?w3_be32(m+at+11):0,ext=at+20==n?w3_be32(m+at+16):0;
            ours=(tag==0x37565750u||tag==0x36565750u)&&ext==0x00584532u;}
        if(ours) {
            /* first everything the player still owes for earlier requests */
            int bad=0;
            while(st.count){if(w3_px_relay_one(&px,&st)){bad=1;break;}}
            if(bad){static unsigned said;if(said<6){said++;lp_log("proxy: earlier replies did not arrive (%u left): going on",st.count);}
                st.count=0;st.in_menu=0;}
            size_t rl=0;unsigned char *ans=w3_answer(px.real,m,n,px.ip,&rl);
            if(ans){free(m);st.handled++;int e=w3_sendall(px.ep,ans,rl);free(ans);if(e){why="EP136 gone";break;}continue;}
        }
        w3_fix_requester(m,n);
        if(kind==0x2b04||kind==0x2104)w3_dump_start();
        w3_dump_msg("OPUS -> XDJ",m,n);
        if(w3_sendall(px.real,m,n)){free(m);why="player gone";break;}
        free(m);st.passed++;w3_out_push(&st,kind);
    }
    w3_proxy_alive(px.ip,0);
    w3_px_set(px.epfd,0);close(px.ep);close(px.real);
    lp_log("proxy %u.%u.%u.%u:%u ended (%s): %u requests passed, %u messages back, %u 3-band answered",px.ip>>24,(px.ip>>16)&255,(px.ip>>8)&255,px.ip&255,px.port,why,st.passed,st.relayed,st.handled);
    return 0;
}
/* from EP136's connect(): 1 = fd is now connected through the proxy, 0 = connect normally */
static int w3_proxy_connect(int fd,const struct sockaddr_in *to) {
    if(!w3_on)return 0;
    uint32_t ip=ntohl(to->sin_addr.s_addr);unsigned port=ntohs(to->sin_port);
#ifndef W3_TEST_ANY_IP
    if((ip>>16)!=0xa9feu||port==12523)return 0;
#endif
    int real=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);if(real<0)return 0;
    /* connect with a 3 s limit */
    int fl=fcntl(real,F_GETFL);fcntl(real,F_SETFL,fl|O_NONBLOCK);
    int r=connect(real,(const struct sockaddr *)to,sizeof *to);
    if(r<0&&errno==EINPROGRESS){struct pollfd pf={real,POLLOUT,0};int e=0;socklen_t el=sizeof e;
        r=poll(&pf,1,3000)>0&&!getsockopt(real,SOL_SOCKET,SO_ERROR,&e,&el)&&!e?0:-1;}
    fcntl(real,F_SETFL,fl);
    if(r<0){close(real);return 0;}
    {struct timeval tv={10,0};setsockopt(real,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);setsockopt(real,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof tv);
     int one=1;setsockopt(real,IPPROTO_TCP,TCP_NODELAY,&one,sizeof one);}
    int sp[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,sp)){close(real);return 0;}
    int epfl=fcntl(fd,F_GETFL),epfd=fcntl(fd,F_GETFD);
    if(dup2(sp[0],fd)<0){close(sp[0]);close(sp[1]);close(real);return 0;}
    close(sp[0]);
    if(epfl>=0)fcntl(fd,F_SETFL,epfl);
    if(epfd>=0)fcntl(fd,F_SETFD,epfd);
    struct w3_proxy *px=malloc(sizeof *px);pthread_t t;
    if(!px){close(sp[1]);close(real);return -1;}
    *px=(struct w3_proxy){sp[1],real,ip,port,fd};w3_px_set(fd,to);
    __atomic_add_fetch(&w3_proxies,1,__ATOMIC_RELEASE);
    if(pthread_create(&t,0,w3_proxy_thread,px)){free(px);w3_px_set(fd,0);close(sp[1]);close(real);return -1;}
    pthread_detach(t);
    lp_log("proxy %u.%u.%u.%u:%u: EP136 connection %d goes through the mod (%u)",ip>>24,(ip>>16)&255,(ip>>8)&255,ip&255,port,fd,w3_proxies);
    return 1;
}
#endif

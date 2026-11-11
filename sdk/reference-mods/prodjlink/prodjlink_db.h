#ifndef OQ_PRODJLINK_DB_H
#define OQ_PRODJLINK_DB_H
/* Pro DJ Link stage 3: ask a player's dbserver for a track's beat grid and waveform,
 * as a player does (port query on TCP 12523, then the dbserver session). Plain POSIX,
 * blocking with timeouts; meant for a worker thread. Message layout as prodjlink-lab/
 * dbserver.py and xdj_capture.py (Deep Symmetry's DJ Link analysis):
 *   number fields 0x0f/0x10/0x11 (1/2/4 bytes), blob 0x14 (u32 length), string 0x26
 *   (u32 UTF-16 characters); a message is start 0x872349ae, transaction, kind (u16),
 *   argument count (u8), the argument tag blob (12 bytes), then the arguments. */
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0   /* macOS (the Mac-side check); Linux has it */
#endif
#define LKDB_QUERY_PORT 12523
#define LKDB_START 0x872349aeu
#define LKDB_ARGS 12
struct lkdb_msg {uint32_t txn,kind,count;uint32_t num[LKDB_ARGS];unsigned char *blob[LKDB_ARGS];uint32_t blob_len[LKDB_ARGS];};
struct lkdb_beat {uint16_t beat_in_bar,tempo_x100;uint32_t time_ms;};

static void lkdb_free(struct lkdb_msg *m) {for(unsigned i=0;i<LKDB_ARGS;i++){free(m->blob[i]);m->blob[i]=0;m->blob_len[i]=0;}}
static int lkdb_io(int s,void *buf,size_t n,int send_it) {
    unsigned char *p=buf;
    while(n) {
        ssize_t r=send_it?send(s,p,n,MSG_NOSIGNAL):recv(s,p,n,0);
        if(r<=0){if(r<0&&errno==EINTR)continue;return -1;}
        p+=r;n-=(size_t)r;
    }
    return 0;
}
static int lkdb_connect(uint32_t ip_be,unsigned port) {
    int s=socket(AF_INET,SOCK_STREAM,0);if(s<0)return -1;
    struct timeval tv={4,0};setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof tv);
    struct sockaddr_in a;memset(&a,0,sizeof a);a.sin_family=AF_INET;a.sin_port=htons((uint16_t)port);a.sin_addr.s_addr=ip_be;
    if(connect(s,(struct sockaddr *)&a,sizeof a)){close(s);return -1;}
    return s;
}
static size_t lkdb_num(unsigned char *o,uint32_t v,unsigned size) {
    o[0]=size==1?0x0f:size==2?0x10:0x11;
    for(unsigned i=0;i<size;i++)o[1+i]=(unsigned char)(v>>(8*(size-1-i)));
    return 1+size;
}
/* One field into *v (numbers) or a malloc'd blob/string; returns its tag or -1. */
static int lkdb_field(int s,uint32_t *v,unsigned char **blob,uint32_t *len) {
    unsigned char t;if(lkdb_io(s,&t,1,0))return -1;
    if(t==0x0f||t==0x10||t==0x11) {
        unsigned n=t==0x0f?1:t==0x10?2:4;unsigned char b[4];if(lkdb_io(s,b,n,0))return -1;
        uint32_t x=0;for(unsigned i=0;i<n;i++)x=x<<8|b[i];*v=x;return t;
    }
    if(t==0x14||t==0x26) {
        unsigned char b[4];if(lkdb_io(s,b,4,0))return -1;
        uint32_t n=(uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3];
        if(t==0x26)n*=2;
        if(n>(16u<<20))return -1;
        unsigned char *d=malloc(n?n:1);if(!d)return -1;
        if(n&&lkdb_io(s,d,n,0)){free(d);return -1;}
        if(blob){*blob=d;*len=n;}else free(d);
        *v=n;return t;
    }
    return -1;
}
static int lkdb_request(int s,uint32_t txn,uint32_t kind,const uint32_t *args,unsigned n) {
    unsigned char m[16+5*LKDB_ARGS+32];size_t o=0;
    o+=lkdb_num(m+o,LKDB_START,4);o+=lkdb_num(m+o,txn,4);o+=lkdb_num(m+o,kind,2);o+=lkdb_num(m+o,n,1);
    m[o++]=0x14;m[o++]=0;m[o++]=0;m[o++]=0;m[o++]=12;
    for(unsigned i=0;i<12;i++)m[o++]=(unsigned char)(i<n?6:0);
    for(unsigned i=0;i<n;i++)o+=lkdb_num(m+o,args[i],4);
    return lkdb_io(s,m,o,1);
}
static int lkdb_response(int s,struct lkdb_msg *r) {
    memset(r,0,sizeof *r);uint32_t v;unsigned char *tags=0;uint32_t tl=0;
    if(lkdb_field(s,&v,0,0)<0||v!=LKDB_START)return -1;
    if(lkdb_field(s,&r->txn,0,0)<0||lkdb_field(s,&r->kind,0,0)<0||lkdb_field(s,&r->count,0,0)<0)return -1;
    if(lkdb_field(s,&v,&tags,&tl)!=0x14)return -1;
    for(uint32_t i=0;i<r->count;i++) {
        unsigned tag=i<tl?tags[i]:6;
        if(tag==3&&i&&r->num[(i-1)%LKDB_ARGS]==0&&!r->blob[(i-1)%LKDB_ARGS])continue;   /* empty blob after a zero length */
        unsigned char *b=0;uint32_t bl=0;
        if(lkdb_field(s,&v,&b,&bl)<0){free(tags);if(b)free(b);lkdb_free(r);return -1;}
        if(i<LKDB_ARGS){r->num[i]=v;r->blob[i]=b;r->blob_len[i]=bl;}else free(b);
    }
    free(tags);return 0;
}
/* Port query then session setup, posing as player `as_player`. Returns the socket. */
static int lkdb_open(uint32_t ip_be,unsigned as_player) {
    int q=lkdb_connect(ip_be,LKDB_QUERY_PORT);if(q<0)return -1;
    unsigned char ask[19]={0,0,0,15,'R','e','m','o','t','e','D','B','S','e','r','v','e','r',0},port[2];
    int ok=!lkdb_io(q,ask,sizeof ask,1)&&!lkdb_io(q,port,2,0);close(q);
    if(!ok)return -1;
    int s=lkdb_connect(ip_be,(unsigned)port[0]<<8|port[1]);if(s<0)return -1;
    unsigned char hello[5];uint32_t v;size_t n=lkdb_num(hello,1,4);
    struct lkdb_msg r;uint32_t me=as_player;
    if(lkdb_io(s,hello,n,1)||lkdb_field(s,&v,0,0)<0||lkdb_request(s,0xfffffffeu,0x0000,&me,1)||lkdb_response(s,&r)){close(s);return -1;}
    lkdb_free(&r);return s;
}
static void lkdb_close(int s) {if(s<0)return;lkdb_request(s,0xfffffffeu,0x0100,0,0);close(s);}
static uint32_t lkdb_rmst(unsigned player,unsigned menu,unsigned slot) {return (uint32_t)player<<24|menu<<16|slot<<8|1;}
/* Beat grid (0x2204 -> 0x4602): 20 lead bytes, then 16-byte little-endian beats. Returns
 * the beat count stored in *out (malloc'd), or -1. */
static int lkdb_beat_grid(int s,unsigned as_player,unsigned slot,uint32_t track,struct lkdb_beat **out) {
    uint32_t a[2]={lkdb_rmst(as_player,8,slot),track};struct lkdb_msg r;*out=0;
    if(lkdb_request(s,1,0x2204,a,2)||lkdb_response(s,&r))return -1;
    int n=-1;
    if(r.kind==0x4602&&r.blob[3]&&r.blob_len[3]>=20) {
        uint32_t count=(r.blob_len[3]-20)/16;struct lkdb_beat *b=calloc(count?count:1,sizeof *b);
        if(b) {
            for(uint32_t i=0;i<count;i++) {
                const unsigned char *p=r.blob[3]+20+16*i;
                b[i].beat_in_bar=(uint16_t)(p[0]|p[1]<<8);b[i].tempo_x100=(uint16_t)(p[2]|p[3]<<8);
                b[i].time_ms=(uint32_t)p[4]|(uint32_t)p[5]<<8|(uint32_t)p[6]<<16|(uint32_t)p[7]<<24;
            }
            *out=b;n=(int)count;
        }
    }
    lkdb_free(&r);return n;
}
/* An analysis tag (0x2c04 -> 0x4f02), e.g. PWV5 from the EXT: the tag's bytes from its
 * fourcc (the reply's 4-byte length prefix dropped), malloc'd in *out. Older players
 * answer 0x4003 (not available): returns -1. */
static int lkdb_anlz_tag(int s,unsigned as_player,unsigned slot,uint32_t track,const char tag[4],const char ext[3],unsigned char **out,uint32_t *len) {
    uint32_t t=(uint32_t)tag[0]|(uint32_t)tag[1]<<8|(uint32_t)tag[2]<<16|(uint32_t)tag[3]<<24;
    uint32_t e=(uint32_t)ext[0]|(uint32_t)ext[1]<<8|(uint32_t)ext[2]<<16;
    uint32_t a[4]={lkdb_rmst(as_player,8,slot),track,t,e};struct lkdb_msg r;*out=0;*len=0;
    if(lkdb_request(s,2,0x2c04,a,4)||lkdb_response(s,&r))return -1;
    int ok=-1;
    if(r.kind==0x4f02&&r.blob[3]&&r.blob_len[3]>8&&!memcmp(r.blob[3]+4,tag,4)) {
        *len=r.blob_len[3]-4;*out=malloc(*len);
        if(*out){memcpy(*out,r.blob[3]+4,*len);ok=0;}
    }
    lkdb_free(&r);return ok;
}
/* Classic blue waveform detail (0x2904 -> 0x4a02): 19 lead bytes, then 1 byte per column. */
static int lkdb_wave_detail(int s,unsigned as_player,unsigned slot,uint32_t track,unsigned char **out,uint32_t *len) {
    uint32_t a[3]={lkdb_rmst(as_player,8,slot),track,0};struct lkdb_msg r;*out=0;*len=0;
    if(lkdb_request(s,3,0x2904,a,3)||lkdb_response(s,&r))return -1;
    int ok=-1;
    if(r.kind==0x4a02&&r.blob[3]&&r.blob_len[3]>19) {
        *len=r.blob_len[3]-19;*out=malloc(*len);
        if(*out){memcpy(*out,r.blob[3]+19,*len);ok=0;}
    }
    lkdb_free(&r);return ok;
}
#endif

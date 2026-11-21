#ifndef OQ_PRODJLINK_DBFIX_H
#define OQ_PRODJLINK_DBFIX_H
/* The deck's own dbserver (EP136, the one an XDJ browses through for our USB) answers the
 * setup request with player number 0 in its welcome (0x4000, args [0, 0]); a real player
 * answers with its own number (the XDJ: [0, 4]). Players check that number against the
 * player they meant to browse and hang up when it differs (2026-11-11: the XDJ listed
 * "[1] USB", connected, closed the connection, nothing opened).
 * Fix: EP136's GOT entries for send() and write() (0x349a410, 0x349b660 in 1.32) point
 * to wrappers that rewrite that one reply on its way out to carry our player number; all
 * other data passes through unchanged to the real functions. */
#include <sys/socket.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "prodjlink_wave3.h"   /* 1.64: 3-band waveforms answered by the mod */

static const struct oqmod_api *db_api;
static unsigned db_installed,db_fixed,db_seen;
static void *db_orig_send,*db_orig_write;   /* not called: may be lazy-binding stubs */

/* 11 87 23 49 ae | 11 ff ff ff fe | 10 40 00 | 0f argc | 14 len blob | 11 a0 | 11 a1 ... */
static int db_setup_reply(const unsigned char *b,size_t n) {
    static const unsigned char head[]={0x11,0x87,0x23,0x49,0xae,0x11,0xff,0xff,0xff,0xfe,0x10,0x40,0x00,0x0f};
    if(n<sizeof head+1+5||n>128||memcmp(b,head,sizeof head))return -1;
    size_t at=sizeof head+1;unsigned argc=b[sizeof head];
    if(b[at]!=0x14||at+5>n)return -1;
    uint32_t blob=(uint32_t)b[at+1]<<24|(uint32_t)b[at+2]<<16|(uint32_t)b[at+3]<<8|b[at+4];
    at+=5+blob;
    if(argc<2||at+10>n||b[at]!=0x11||b[at+5]!=0x11)return -1;
    return (int)(at+5);      /* the second argument's tag */
}
static const void *db_fix(const void *buf,size_t n,unsigned char *copy) {
    if(n<20||n>128||((const unsigned char *)buf)[0]!=0x11)return buf;
    const unsigned char *b=buf;
    if(b[1]!=0x87||b[2]!=0x23||b[3]!=0x49||b[4]!=0xae)return buf;
    if(db_seen<12){db_seen++;char hex[300];lp_hex(hex,sizeof hex,b,n<100?n:100);lp_log("dbserver out %zu bytes: %s",n,hex);}
    int at=db_setup_reply(b,n);unsigned me=LK_PLAYER;
    if(at<0||!me)return buf;
    memcpy(copy,b,n);copy[at+1]=0;copy[at+2]=0;copy[at+3]=0;copy[at+4]=(unsigned char)me;
    db_fixed++;lp_log("dbserver welcome: player %u -> %u",(unsigned)b[at+4],me);
    return copy;
}
/* Straight to the kernel: calling send()/write() from here can land in EP136's PLT
 * (a non-PIE executable's PLT slot is the canonical address of an imported function),
 * i.e. back in these wrappers, endlessly; R1.6 crashed the deck that way. */
static unsigned lf_other_lines;
static void lf_note_other(const char *dir,const unsigned char *b,size_t n,const struct sockaddr *a);
/* Research: EP136's TCP conversation with another player's dbserver (peer 169.254.x, port
 * 12523 or 1051): every send and recv logged (first 80 lines, 96 bytes each). */
static unsigned db_tcp_lines;
static void db_tcp_note(const char *dir,int fd,const void *b,long n) {
    if(db_tcp_lines>=80||n<0)return;
    struct sockaddr_in pa;socklen_t pl=sizeof pa;int ty=0;socklen_t tl=sizeof ty;
    if(syscall(SYS_getsockopt,fd,SOL_SOCKET,SO_TYPE,&ty,&tl)||ty!=SOCK_STREAM)return;
    if(syscall(SYS_getpeername,fd,(struct sockaddr *)&pa,&pl)||pa.sin_family!=AF_INET)return;
    uint32_t ip=ntohl(pa.sin_addr.s_addr);unsigned port=ntohs(pa.sin_port);
    if((ip>>16)!=0xa9feu||(port!=12523&&port!=1051))return;
    db_tcp_lines++;char hex[200];lp_hex(hex,sizeof hex,b,(size_t)(n<96?n:96));
    lp_log("ep136 tcp %s %u.%u.%u.%u:%u len %ld: %s",dir,ip>>24,(ip>>16)&255,(ip>>8)&255,ip&255,port,n,hex);
}
/* EP136 browsing another player's media talks to that player's dbserver as player 9; an
 * XDJ-1000MK2 only answers requesters 1-4 (checked 2026-11-11 from the Mac: setup as 2
 * welcomed, as 5 or 9 closed). Requests to a 169.254.x peer on 1051 are rewritten to our
 * own link number (LK_PLAYER): the setup argument (txid fffffffe, kind 0) and the
 * requester byte (top byte) of the first number argument of every other request. */
static unsigned db_req_fixed;
static const void *db_req_fix(int fd,const void *buf,size_t n,unsigned char *copy,size_t cap) {
    const unsigned char *b=buf;unsigned me=LK_PLAYER;
    if(!me||n<20||n>cap||b[0]!=0x11||b[1]!=0x87||b[2]!=0x23||b[3]!=0x49||b[4]!=0xae||b[10]!=0x10||b[13]!=0x0f||b[15]!=0x14)return buf;
    struct sockaddr_in pa;socklen_t pl=sizeof pa;
    if(syscall(SYS_getpeername,fd,(struct sockaddr *)&pa,&pl)||pa.sin_family!=AF_INET||(ntohl(pa.sin_addr.s_addr)>>16)!=0xa9feu)return buf;
    uint32_t blob=(uint32_t)b[16]<<24|(uint32_t)b[17]<<16|(uint32_t)b[18]<<8|b[19];
    size_t at=20+blob;
    if(b[14]<1||at+5>n||b[at]!=0x11)return buf;
    unsigned kind=(unsigned)b[11]<<8|b[12];
    {static unsigned seen;if(seen<300){seen++;   /* 1.65: every request EP136 sends to a player, with its size */
        char tg[24]="";   /* 1.68: which analysis tag (last two arguments) */
        if((kind==0x2c04||kind==0x2d04)&&n>=20&&b[n-10]==0x11&&b[n-5]==0x11){const unsigned char *t=b+n-9,*e=b+n-4;
            snprintf(tg,sizeof tg," %c%c%c%c.%c%c%c",t[3],t[2],t[1],t[0],e[3],e[2],e[1]);}
        lp_log("ep136 request %04x %zu bytes argc %u%s%s",kind,n,b[14],kind==0x2d04?" (3-band)":"",tg);}}
    memcpy(copy,b,n);unsigned changed=0;
    if(kind==0) {   /* setup: argument = our player number */
        if(!(copy[at+4]==me&&!copy[at+1]&&!copy[at+2]&&!copy[at+3])){copy[at+1]=copy[at+2]=copy[at+3]=0;copy[at+4]=(unsigned char)me;changed|=1;}
    } else if(!(copy[at+1]==me||copy[at+1]<5)){copy[at+1]=(unsigned char)me;changed|=1;}   /* not yet a player number */
    if(!changed)return buf;
    if(changed&1){db_req_fixed++;
        if(db_req_fixed<=12)lp_log("dbserver request kind %04x: requester -> player %u (%u)",kind,me,db_req_fixed);}
    return copy;
}
static void *db_orig_recv,*db_orig_read;
static ssize_t db_read(int fd,void *buf,size_t n) {
    ssize_t r=(ssize_t)syscall(SYS_read,fd,buf,n);
    if(r>0&&db_tcp_lines<80&&fd>2){int e=errno;db_tcp_note("in(read)",fd,buf,r);errno=e;}
    return r;
}
static ssize_t db_recv(int fd,void *buf,size_t n,int flags) {
    ssize_t r=(ssize_t)syscall(SYS_recvfrom,fd,buf,n,flags,(void *)0,(void *)0);
    if(r>0)db_tcp_note("in",fd,buf,r);
    else if(db_tcp_lines<80&&r<=0){int e=errno;db_tcp_note("in(end)",fd,"",r==0?0:-1);errno=e;}
    return r;
}
static ssize_t db_send(int fd,const void *buf,size_t n,int flags) {
    if(lf_other_lines<60){int ty=0;socklen_t tl=sizeof ty;struct sockaddr_in pa;socklen_t pl=sizeof pa;
        if(!syscall(SYS_getsockopt,fd,SOL_SOCKET,SO_TYPE,&ty,&tl)&&ty==SOCK_DGRAM&&!syscall(SYS_getpeername,fd,(struct sockaddr *)&pa,&pl))
            lf_note_other("out(send)",buf,n,(struct sockaddr *)&pa);}
    if(db_tcp_lines<80)db_tcp_note("out",fd,buf,(long)n);
    unsigned char copy[128];static __thread unsigned char req[4096];
    const void *out=db_fix(buf,n,copy);
    if(out==buf)out=db_req_fix(fd,buf,n,req,sizeof req);
    return (ssize_t)syscall(SYS_sendto,fd,out,n,flags,(void *)0,0);
}
static ssize_t db_write(int fd,const void *buf,size_t n) {
    if(db_tcp_lines<80&&fd>2)db_tcp_note("out(write)",fd,buf,(long)n);
    if(n>=20&&((const unsigned char *)buf)[0]==0x11&&((const unsigned char *)buf)[1]==0x87){
        static __thread unsigned char req[4096];const void *o=db_req_fix(fd,buf,n,req,sizeof req);
        if(o!=buf){unsigned char c2[128];return (ssize_t)syscall(SYS_write,fd,db_fix(o,n,c2),n);}}
    unsigned char copy[128];return (ssize_t)syscall(SYS_write,fd,db_fix(buf,n,copy),n);
}
/* 1.69: EP136's own view of a proxied dbserver connection (prodjlink_wave3.h) */
static void *db_orig_getpeername,*db_orig_setsockopt;
static int db_getpeername(int fd,struct sockaddr *a,socklen_t *l) {
    struct sockaddr_in to;
    if(a&&l&&w3_px_get(fd,&to)){socklen_t k=*l<sizeof to?*l:(socklen_t)sizeof to;memcpy(a,&to,k);*l=sizeof to;return 0;}
    return (int)syscall(SYS_getpeername,fd,a,l);
}
static int db_setsockopt(int fd,int level,int name,const void *v,socklen_t l) {
    long r=syscall(SYS_setsockopt,fd,level,name,v,l);
    if(r<0){struct sockaddr_in to;int e=errno;if(w3_px_get(fd,&to))return 0;errno=e;}   /* TCP options on our socket pair */
    return (int)r;
}
/* GOT slots are ordinary writable data (.got.plt, after RELRO; it shares its pages with
 * other GOT slots and .data). NOT api->patch(): that one is for vtables and leaves the
 * page read-only afterwards, which crashed the deck at start-up (R1.6, 2026-11-11).
 * Here: write only when /proc/self/maps says the page is already writable. */
static int db_writable(uintptr_t a) {
    FILE *f=fopen("/proc/self/maps","r");if(!f)return 0;
    char line[512];int ok=0;
    while(fgets(line,sizeof line,f)) {
        unsigned long lo,hi;char perm[8];
        if(sscanf(line,"%lx-%lx %7s",&lo,&hi,perm)==3&&a>=lo&&a+8<=hi){ok=perm[0]=='r'&&perm[1]=='w';break;}
    }
    fclose(f);return ok;
}
static int db_store(uintptr_t slot,void *rep,void **orig) {
    if(!db_writable(slot))return 0;
    *orig=__atomic_load_n((void **)slot,__ATOMIC_ACQUIRE);
    __atomic_store_n((void **)slot,rep,__ATOMIC_RELEASE);
    return 1;
}
static void db_install(const struct oqmod_api *api) {
    db_api=api;
    if(db_installed||!api->ep136_ok||!api->ep136_ok())return;
    uintptr_t s=0x349a410,w=0x349b660;
    if(db_store(s,(void *)db_send,&db_orig_send))db_installed|=1;
    if(db_store(w,(void *)db_write,&db_orig_write))db_installed|=2;
    if(db_store(0x349ae30,(void *)db_recv,&db_orig_recv))db_installed|=4;
    if(db_store(0x349a580,(void *)db_read,&db_orig_read))db_installed|=8;
    if(db_store(0x349b580,(void *)db_getpeername,&db_orig_getpeername))db_installed|=16;   /* 1.69 */
    if(db_store(0x349a140,(void *)db_setsockopt,&db_orig_setsockopt))db_installed|=32;
    lp_log("dbserver fix: send %s, write %s, recv %s",db_installed&1?"hooked":"NOT hooked",db_installed&2?"hooked":"NOT hooked",db_installed&4?"hooked":"NOT hooked");
}
static unsigned lk_db_fixed_count(void){return db_fixed;}
#endif

#ifndef OQ_PRODJLINK_LINKFILTER_H
#define OQ_PRODJLINK_LINKFILTER_H
/* Research, R122 / EP136 1.32: what EP136's own link layer sends and receives, and an
 * option to hide one packet kind from it.
 * 2026-11-11: with a real XDJ on the link at boot, EP136 sends one hello (device type 7)
 * and gives up; the XDJ answers that hello with kind 0x0b (36 bytes, no payload). Without
 * the XDJ EP136 joins (numbers 1-4 + 0x21 for its decks, 9 for itself). Option "drop0b"
 * keeps 0x0b from EP136, to see whether it then joins with the XDJ present.
 * EP136's GOT: recvfrom 0x349a8b0, sendto 0x349a870 (rw pages, written directly). */
#include <sys/syscall.h>
#include <arpa/inet.h>
static unsigned lf_installed,lf_dropped,lf_drop0b,lf_yield,lf_yielded,lf_upgrade,lf_upgraded;
/* A number another device (not this deck) announced in the last 5 s: EP136 must leave it. */
static unsigned lf_two;   /* option "decks12": EP136 holds only 1-2 (as the XDJ-AZ in PRO DJ LINK Mode) */
static int lf_foreign(unsigned number) {
    if(!number||number>=16)return 0;
    if(lf_two&&(number==3||number==4))return 1;   /* 3-4 are for external players */
    uint64_t now=lk_now_ms(),t=lk_taken_ms[number];
    return t&&now-t<5000;
}
static void *lf_orig_recvfrom,*lf_orig_sendto;
/* Non-link UDP between EP136 and another player (its NFS client: portmap 111, MOUNT, NFS) */
static void lf_note_other(const char *dir,const unsigned char *b,size_t n,const struct sockaddr *a) {
    unsigned lines=lf_other_lines;
    if(lines>=60||!a||a->sa_family!=AF_INET)return;
    uint32_t ip=ntohl(((const struct sockaddr_in *)a)->sin_addr.s_addr);
    if(ip==lk_my_ip||(ip&0xffff)==0xffff||(ip>>24)==127||(ip>>16)!=0xa9fe)return;
    lf_other_lines=lines+1;char peer[20];inet_ntop(AF_INET,&((const struct sockaddr_in *)a)->sin_addr,peer,sizeof peer);
    char hex[100];lp_hex(hex,sizeof hex,b,n<40?n:40);
    lp_log("ep136 %s udp %s %s:%u len %zu: %s",dir,dir[0]=='o'?"to":"from",peer,ntohs(((const struct sockaddr_in *)a)->sin_port),n,hex);
}
static void lf_note(const char *dir,int fd,const unsigned char *b,size_t n,const struct sockaddr *a) {
    if(n<0x0b||memcmp(b,lk_magic,10)){lf_note_other(dir,b,n,a);return;}
    static unsigned char seen[2][4][256];
    struct sockaddr_in me;socklen_t ml=sizeof me;unsigned port=0;
    if(dir[0]=='o'){if(a&&a->sa_family==AF_INET)port=ntohs(((const struct sockaddr_in *)a)->sin_port);}
    else if(!syscall(SYS_getsockname,fd,(struct sockaddr *)&me,&ml))port=ntohs(me.sin_port);
    unsigned pi=port>=50000&&port<=50003?port-50000:3,kind=b[0x0a],o=dir[0]=='o';
    if(seen[o][pi][kind]>=4)return;
    seen[o][pi][kind]++;
    char peer[20]="-";
    if(a&&a->sa_family==AF_INET)inet_ntop(AF_INET,&((const struct sockaddr_in *)a)->sin_addr,peer,sizeof peer);
    char hex[200];lp_hex(hex,sizeof hex,b+0x0b,n-0x0b<80?n-0x0b:80);
    lp_log("ep136 %s port %u kind 0x%02x len %zu %s %s: %s",dir,port,kind,n,o?"to":"from",peer,hex);
}
static ssize_t lf_recvfrom(int fd,void *buf,size_t len,int flags,struct sockaddr *src,socklen_t *sl) {
    for(int tries=0;;tries++) {
        ssize_t r=(ssize_t)syscall(SYS_recvfrom,fd,buf,len,flags,src,sl);
        if(r<=0)return r;
        const unsigned char *b=buf;
        lf_note("in",fd,b,(size_t)r,src);
        if(r>=0x28&&!memcmp(b,lk_magic,10)&&b[0x0a]==0x26&&src&&src->sa_family==AF_INET)
            lb_on_request(ntohl(((const struct sockaddr_in *)src)->sin_addr.s_addr),b,(size_t)r);
        /* "number taken" replies (0x03 to a claim, 0x08 conflict) about a number EP136 should
         * not hold anyway: hidden, so it keeps the other player as a member */
        if(lf_yield&&r>=0x25&&!memcmp(b,lk_magic,10)&&(b[0x0a]==0x03||b[0x0a]==0x08)&&lf_foreign(b[0x24])&&tries<8) {
            lf_yielded++;if(lf_yielded<=20)lp_log("ep136 in: hid 0x%02x about number %u (%u)",b[0x0a],b[0x24],lf_yielded);
            continue;
        }
        /* An older player's CDJ status (0xd4 / 0x11c bytes, version 0x03/0x04) shown to EP136
         * as the 0x415-byte version-0x06 status ("PLink") that the OPUS itself sends; the
         * extra bytes are zero. EP136 learns about other players' USBs from that format. */
        if(lf_upgrade&&(r==0xd4||r==0x11c)&&len>=0x415&&!memcmp(b,lk_magic,10)&&b[0x0a]==0x0a&&b[0x21]>=1&&b[0x21]<=6&&lf_foreign(b[0x21])) {
            unsigned char *w=buf;memset(w+r,0,0x415-(size_t)r);
            w[0x20]=0x06;w[0x22]=0x03;w[0x23]=0xf1;
            lf_upgraded++;if(lf_upgraded<=3)lp_log("ep136 in: status of player %u shown as PLink (%zd -> 1045 bytes, %u)",b[0x21],r,lf_upgraded);
            return 0x415;
        }
        if(lf_drop0b&&r>=0x0b&&!memcmp(b,lk_magic,10)&&b[0x0a]==0x0b&&tries<8) {
            lf_dropped++;if(lf_dropped<=10)lp_log("ep136 in: dropped 0x0b (%u)",lf_dropped);
            continue;   /* EP136 never sees it; next packet (blocks like a normal recvfrom) */
        }
        return r;
    }
}
static ssize_t lf_sendto(int fd,const void *buf,size_t len,int flags,const struct sockaddr *dst,socklen_t dl) {
    lf_note("out",fd,buf,len,dst);
    const unsigned char *b=buf;
    /* EP136 reserves 1-4 for its decks (0x02 claims) and defends them (0x05). Numbers a
     * real player already uses are left to that player: the packet is not sent. */
    if(lf_yield&&len>=0x25&&!memcmp(b,lk_magic,10)) {
        /* 0x02 claim (number at 0x2e), 0x05 defence and 0x06 keep-alive (number at 0x24):
         * EP136 also sends a unicast keep-alive to each player carrying THAT player's number,
         * which the XDJ takes as a conflict (its number flashes) - held back too */
        unsigned kind=b[0x0a],num=kind==0x02&&len>=0x2f?b[0x2e]:(kind==0x05||kind==0x06)?b[0x24]:0;
        if(num&&lf_foreign(num)) {
            lf_yielded++;if(lf_yielded<=20)lp_log("ep136 out: held back 0x%02x for number %u (%u)",kind,num,lf_yielded);
            return (ssize_t)len;
        }
    }
    return (ssize_t)syscall(SYS_sendto,fd,buf,len,flags,dst,dl);
}
static void lf_install(const struct oqmod_api *api) {
    if(lf_installed||!api->ep136_ok||!api->ep136_ok())return;
    lf_installed=1;
    int a=db_store(0x349a8b0,(void *)lf_recvfrom,&lf_orig_recvfrom),b=db_store(0x349a870,(void *)lf_sendto,&lf_orig_sendto);
    lp_log("link filter: recvfrom %s, sendto %s, drop 0x0b %s, yield numbers %s",a?"hooked":"NOT hooked",b?"hooked":"NOT hooked",lf_drop0b?"on":"off",lf_yield?"on":"off");
}
#endif

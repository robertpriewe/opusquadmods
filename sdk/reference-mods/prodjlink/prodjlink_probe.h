#ifndef OQ_PRODJLINK_PROBE_H
#define OQ_PRODJLINK_PROBE_H
/* Link probe (Pro DJ Link mod): what it takes for another player to browse the OPUS's
 * USB under LINK. Written to link-probe.txt next to mod.so on the settings USB.
 *
 * Census (on switch-on, then every 30 s): EP136's own Link/NFS threads (EP136 carries a
 * CDJ-style NFS server, threads NfsServerMgr/NfsPortMapd/NfsMountd/NfsNFSd) and every
 * listening TCP / bound UDP port on the deck (/proc/net).
 * Offer (option "offer"): our player LK_PLAYER also sends CDJ status (0x0a to 50002,
 * 5 a second) saying its USB is loaded and link media is available, and we listen on
 * the ports a player uses next: TCP 12523 (dbserver port query) and TCP 1051 (dbserver)
 * are logged and closed (EP136's own dbserver answers the browse side); the USB's files
 * are served read-only over NFS (prodjlink_nfs.h). Packet kinds we did not see before are logged too.
 * Offsets: Deep Symmetry's DJ Link analysis (beat-link CdjStatus: USB state 0x6f,
 * SD state 0x73, link media 0x75). */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include "prodjlink_nfs.h"

#define LP_MAX_LINES 400
static char lp_path[256];
static unsigned lp_lines,lp_offer,lp_status_sent,lp_requests;
static int lp_tcp[2]={-1,-1};
static pthread_mutex_t lp_lock=PTHREAD_MUTEX_INITIALIZER;

/* link-probe.txt beside our own mod.so, found through /proc/self/maps. */
static void lp_find_path(void) {
    FILE *f=fopen("/proc/self/maps","r");if(!f)return;
    char line[512];uintptr_t me=(uintptr_t)&lp_find_path;
    while(fgets(line,sizeof line,f)) {
        unsigned long lo,hi;char *path=strchr(line,'/');
        if(sscanf(line,"%lx-%lx",&lo,&hi)!=2||me<lo||me>=hi||!path)continue;
        path[strcspn(path,"\n")]=0;char *slash=strrchr(path,'/');
        if(slash){*slash=0;snprintf(lp_path,sizeof lp_path,"%.230s/link-probe.txt",path);}
        break;
    }
    fclose(f);
}
static void lp_log(const char *fmt,...) {
    pthread_mutex_lock(&lp_lock);
    if(!lp_path[0])lp_find_path();
    FILE *f=lp_path[0]&&lp_lines<LP_MAX_LINES?fopen(lp_path,"a"):0;
    if(f) {
        fprintf(f,"[%llu] ",(unsigned long long)lk_now_ms());
        va_list a;va_start(a,fmt);vfprintf(f,fmt,a);va_end(a);fputc('\n',f);fclose(f);lp_lines++;
    }
    pthread_mutex_unlock(&lp_lock);
}
static void lp_hex(char *out,size_t cap,const unsigned char *p,size_t n) {
    size_t k=0;for(size_t i=0;i<n&&k+3<cap;i++)k+=(size_t)snprintf(out+k,cap-k,"%02x",p[i]);
    if(cap)out[k<cap?k:cap-1]=0;
}

/* ---- census ---------------------------------------------------------------------- */
/* Logged at the first call and then only when a line changes (the log is capped). */
static void lp_census_line(unsigned slot,const char *fmt,const char *a,const char *b) {
    static char last[5][1100];char line[1100];snprintf(line,sizeof line,fmt,a,b);
    if(slot<5&&!strcmp(last[slot],line))return;
    if(slot<5)snprintf(last[slot],sizeof last[slot],"%s",line);
    lp_log("%s",line);
}
static void lp_census(void) {
    char names[1024]="";size_t n=0;unsigned total=0;
    DIR *d=opendir("/proc/self/task");
    if(d) {
        struct dirent *e;
        while((e=readdir(d))) {
            if(e->d_name[0]=='.')continue;
            char p[64],comm[32]="";snprintf(p,sizeof p,"/proc/self/task/%.20s/comm",e->d_name);
            FILE *f=fopen(p,"r");if(!f)continue;
            if(fgets(comm,sizeof comm,f))comm[strcspn(comm,"\n")]=0;
            fclose(f);total++;
            static const char *const want[]={"Nfs","Link","Stm","Plink","Udp","Ethr","Client","Socket","Server","Lan"};
            for(unsigned i=0;i<sizeof want/sizeof *want;i++)
                if(strstr(comm,want[i])&&n+strlen(comm)+2<sizeof names){n+=(size_t)snprintf(names+n,sizeof names-n,"%s ",comm);break;}
        }
        closedir(d);
    }
    char count[16];snprintf(count,sizeof count,"%u",total>250?250u:total);   /* ignore small changes in the total */
    lp_census_line(0,"census threads>=%s link_threads: %s",count,names[0]?names:"(none)");
    static const char *const tables[]={"/proc/net/tcp","/proc/net/tcp6","/proc/net/udp","/proc/net/udp6"};
    for(unsigned t=0;t<4;t++) {
        FILE *f=fopen(tables[t],"r");if(!f)continue;
        char line[256],ports[600]="";size_t k=0;
        if(!fgets(line,sizeof line,f)){fclose(f);continue;}
        while(fgets(line,sizeof line,f)) {
            char local[64];unsigned st=0;
            if(sscanf(line,"%*s %63s %*s %x",local,&st)!=2)continue;
            char *colon=strrchr(local,':');if(!colon)continue;
            unsigned port=(unsigned)strtoul(colon+1,0,16);
            if(t<2&&st!=0x0a)continue;                 /* TCP: listening only */
            if(k+8<sizeof ports)k+=(size_t)snprintf(ports+k,sizeof ports-k,"%u ",port);
        }
        fclose(f);
        lp_census_line(1+t,"census %s: %s",tables[t]+10,ports[0]?ports:"(none)");
    }
}

/* ---- offer: status saying our USB is loaded --------------------------------------- */
static void lp_status(unsigned char p[0xd4],unsigned counter) {
    memset(p,0,0xd4);memcpy(p,lk_magic,10);p[0x0a]=0x0a;
    memcpy(p+0x0b,LK_NAME,strlen(LK_NAME));
    p[0x1f]=0x01;p[0x20]=0x03;p[0x21]=LK_PLAYER;p[0x22]=0x00;p[0x23]=0xd4-0x24;
    p[0x24]=LK_PLAYER;p[0x26]=0x01;
    p[0x6f]=0x00;            /* USB: loaded */
    p[0x73]=0x04;            /* SD: empty */
    p[0x75]=0x01;            /* link media available */
    memcpy(p+0x7c,"1.43",4);
    p[0x87]=0x01;            /* sync counter (0x84, BE) = 1 */
    p[0x89]=0x84;p[0x8b]=0x7e;
    p[0x8d]=0x10;p[0x90]=0x80;p[0x92]=0xff;p[0x93]=0xff;p[0x99]=0x10;
    p[0x9f]=0xff;memset(p+0xa0,0xff,4);p[0xa4]=0x01;p[0xa5]=0xff;
    p[0xc1]=0x10;p[0xc5]=0x10;
    p[0xc8]=(unsigned char)(counter>>24);p[0xc9]=(unsigned char)(counter>>16);p[0xca]=(unsigned char)(counter>>8);p[0xcb]=(unsigned char)counter;
}
static int lp_listen_tcp(unsigned port) {
    int s=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0),one=1;if(s<0)return -1;
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    struct sockaddr_in a;memset(&a,0,sizeof a);a.sin_family=AF_INET;a.sin_port=htons((uint16_t)port);
    if(bind(s,(struct sockaddr *)&a,sizeof a)||listen(s,4)) {
        lp_log("offer: TCP %u not available (%s)",port,strerror(errno));close(s);return -1;
    }
    lp_log("offer: listening on TCP %u",port);return s;
}
/* Opens our listeners once per offer session: a port already taken (EP136 holds 12523
 * for its own dbserver) is logged once, not retried. */
static unsigned lp_opened;
static void lp_open(void) {
    if(lp_opened)return;
    lp_opened=1;
    lp_tcp[0]=lp_listen_tcp(12523);
    lp_tcp[1]=lp_listen_tcp(1051);
    ln_log=lp_log;ln_open();
}
static void lp_close(void) {
    for(int i=0;i<2;i++)if(lp_tcp[i]>=0){close(lp_tcp[i]);lp_tcp[i]=-1;}
    ln_close();lp_opened=0;
}
/* Adds our listening sockets to the link thread's select set. */
static void lp_fdset(fd_set *set,int *top) {
    for(int i=0;i<2;i++)if(lp_tcp[i]>=0){FD_SET(lp_tcp[i],set);if(lp_tcp[i]>*top)*top=lp_tcp[i];}
    ln_fdset(set,top);
}
/* A request on one of them: log who asked and what, then hang up. */
static void lp_serve(fd_set *set) {
    for(int i=0;i<2;i++)if(lp_tcp[i]>=0&&FD_ISSET(lp_tcp[i],set)) {
        struct sockaddr_in from;socklen_t fl=sizeof from;
        int c=accept(lp_tcp[i],(struct sockaddr *)&from,&fl);if(c<0)continue;
        struct timeval tv={0,300000};setsockopt(c,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);
        int fl2=fcntl(c,F_GETFL);if(fl2>=0)fcntl(c,F_SETFL,fl2&~O_NONBLOCK);
        unsigned char buf[96];ssize_t n=recv(c,buf,sizeof buf,0);char hex[200];lp_hex(hex,sizeof hex,buf,n>0?(size_t)n:0);
        lp_log("request TCP %u from %s: %zd bytes %s",i?1051u:12523u,inet_ntoa(from.sin_addr),n,hex);
        close(c);lp_requests++;
    }
    ln_serve(set);
}
/* Packet kinds seen for the first time (per port), from any device. */
static void lp_note_packet(unsigned port,uint32_t ip,const unsigned char *p,size_t n) {
    static unsigned char seen[3][256];
    if(port<50000||port>50002||n<0x0b)return;
    unsigned kind=p[0x0a];if(seen[port-50000][kind])return;
    seen[port-50000][kind]=1;
    char hex[260];lp_hex(hex,sizeof hex,p,n<120?n:120);
    lp_log("packet port=%u kind=0x%02x from=%u.%u.%u.%u len=%zu %s",port,kind,ip>>24,(ip>>16)&255,(ip>>8)&255,ip&255,n,hex);
}
#endif

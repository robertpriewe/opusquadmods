#ifndef OQ_PRODJLINK_PROBE_H
#define OQ_PRODJLINK_PROBE_H
/* Link probe (Pro DJ Link mod): what it takes for another player to browse the OPUS's
 * USB under LINK. 1.99: written to MODS/prodjlink/logs/session-NNN.txt on the settings USB.
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
#include <sys/stat.h>
#include <ifaddrs.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include "prodjlink_nfs.h"

#include "prodjlink_crashlog.h"

/* 1.99: no line limit any more; each session's log stops at LP_MAX_BYTES (the crash report keeps
 * its own copy of the last lines). LP_QUIET still hides the research lines that are formatted on
 * EP136's own threads (sync tracer hex, proxy requests, file calls, icon requests). */
#define LP_MAX_BYTES (24u*1024u*1024u)
#define LP_KEEP 10          /* sessions kept in logs/ */
#define LP_QUIET 1
static char lp_path[256];   /* <MODS/prodjlink>/link-probe.txt: other parts use its folder */
static unsigned lp_lines,lp_offer,lp_status_sent,lp_requests;
static int lp_tcp[2]={-1,-1};
static pthread_mutex_t lp_lock=PTHREAD_MUTEX_INITIALIZER;

/* MODS/prodjlink on whichever USB has it (the mod is keep.running, so
 * its mod.so runs from a RAM copy); else beside our own mod.so (/proc/self/maps). */
static void lp_find_path(void) {
    DIR *d=opendir("/media/usb");
    if(d) {
        struct dirent *e;
        while((e=readdir(d))) {
            if(e->d_name[0]=='.')continue;
            char dir[300];struct stat st;snprintf(dir,sizeof dir,"/media/usb/%.200s/MODS/prodjlink",e->d_name);
            if(!stat(dir,&st)&&S_ISDIR(st.st_mode)){snprintf(lp_path,sizeof lp_path,"%.230s/link-probe.txt",dir);break;}
        }
        closedir(d);
        if(lp_path[0])return;
    }
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

/* ---- 1.99: one set of files per session in MODS/prodjlink/logs ---- */
static char lp_logdir[280],lp_log_path[320];static unsigned lp_session;
static pthread_mutex_t lp_sess_lock=PTHREAD_MUTEX_INITIALIZER;
static void lp_tail(FILE *out,const char *path,unsigned lines,const char *label) {
    FILE *f=fopen(path,"r");if(!f)return;
    if(fseek(f,0,SEEK_END)==0){long sz=ftell(f);fseek(f,sz>6000?sz-6000:0,SEEK_SET);}
    char buf[6001];size_t n=fread(buf,1,6000,f);fclose(f);buf[n]=0;
    while(n&&buf[n-1]=='\n')buf[--n]=0;
    size_t i=n;unsigned k=0;while(i>0&&k<lines){i--;if(buf[i]=='\n')k++;}
    if(buf[i]=='\n')i++;
    fprintf(out,"%s:\n",label);
    for(char *s=buf+i;*s;){char *e=strchr(s,'\n');if(e)*e=0;fprintf(out,"  | %s\n",s);if(!e)break;s=e+1;}
}
static void lp_header(FILE *f) {
    char line[512];
    fprintf(f,"==== Pro DJ Link (Experimental) %s log, session %03u ====\n",PDL_VERSION,lp_session);
    FILE *u=fopen("/proc/uptime","r");if(u){if(fgets(line,sizeof line,u)){line[strcspn(line,"\n")]=0;fprintf(f,"uptime: %s s; mod clock %llu ms\n",line,(unsigned long long)lk_now_ms());}fclose(u);}
    time_t now=time(0);struct tm tm;if(gmtime_r(&now,&tm)){strftime(line,sizeof line,"%Y-%m-%d %H:%M:%S",&tm);fprintf(f,"device clock (often wrong): %s UTC\n",line);}
    u=fopen("/proc/version","r");if(u){if(fgets(line,sizeof line,u)){line[strcspn(line,"\n")]=0;fprintf(f,"kernel: %s\n",line);}fclose(u);}
    u=fopen("/proc/meminfo","r");if(u){while(fgets(line,sizeof line,u))if(!strncmp(line,"MemTotal",8)||!strncmp(line,"MemAvailable",12)){line[strcspn(line,"\n")]=0;fprintf(f,"%s\n",line);}fclose(u);}
    /* mods on this stick and their versions */
    char mods[300];snprintf(mods,sizeof mods,"%s",lp_logdir);
    char *s=strrchr(mods,'/');if(s){*s=0;s=strrchr(mods,'/');}
    if(s){*s=0;DIR *d=opendir(mods);
        if(d){fprintf(f,"mods on this USB:");struct dirent *e;
            while((e=readdir(d))){if(e->d_name[0]=='.')continue;
                char vp[440];snprintf(vp,sizeof vp,"%.290s/%.100s/version.txt",mods,e->d_name);
                FILE *v=fopen(vp,"r");char ver[32]="";if(v){if(fgets(ver,sizeof ver,v))ver[strcspn(ver,"\r\n")]=0;fclose(v);}
                if(v||strchr(e->d_name,'.')==0)fprintf(f," %s%s%s",e->d_name,ver[0]?" ":"",ver);}
            fprintf(f,"\n");closedir(d);}
        /* the firmware's own crash notes (USB root) */
        s=strrchr(mods,'/');if(s){*s=0;char cp[340];snprintf(cp,sizeof cp,"%s/OPUS-CRASH.txt",mods);lp_tail(f,cp,3,"OPUS-CRASH.txt (firmware crash notes, last 3)");}
    }
    if(lp_session>1) {
        char p[360];unsigned prev=lp_session-1;
        snprintf(p,sizeof p,"%s/crash-%03u.txt",lp_logdir,prev);
        struct stat st;fprintf(f,"previous session %03u: %s\n",prev,stat(p,&st)?"no crash report":"CRASHED, see crash file");
        snprintf(p,sizeof p,"%s/session-%03u.txt",lp_logdir,prev);lp_tail(f,p,4,"previous session, last lines");
        snprintf(p,sizeof p,"%s/watch-%03u.txt",lp_logdir,prev);lp_tail(f,p,4,"previous watch, last lines (a freeze shows as STALL / GUARD or lines just stopping)");
    }
    fprintf(f,"====\n");
}
static const char lp_readme[]=
"Pro DJ Link (Experimental) logs\n"
"\n"
"Each start of the OPUS gets a number (newest = highest). Per start:\n"
"  session-NNN.txt  what Pro DJ Link did: players seen, tracks loaded, master, errors\n"
"  watch-NNN.txt    once a second: memory, CPU, busy threads; shows freezes\n"
"  crash-NNN.txt    only if the OPUS app crashed: where and why\n"
"The last 10 starts are kept.\n"
"\n"
"Something went wrong? Send the whole logs folder (and OPUS-CRASH.txt from the USB root,\n"
"if there is one) with a short note: what you did, what happened, roughly when.\n"
"\n"
"The logs contain the DJ Link players' network addresses and track details from the link.\n";
/* called from the writer / watch threads, never from EP136's */
static int lp_session_open(void) {
    pthread_mutex_lock(&lp_sess_lock);
    if(lp_session){pthread_mutex_unlock(&lp_sess_lock);return 1;}
    pthread_mutex_lock(&lp_lock);if(!lp_path[0])lp_find_path();char dir[256];snprintf(dir,sizeof dir,"%s",lp_path);pthread_mutex_unlock(&lp_lock);
    char *slash=strrchr(dir,'/');if(!dir[0]||!slash){pthread_mutex_unlock(&lp_sess_lock);return 0;}
    *slash=0;snprintf(lp_logdir,sizeof lp_logdir,"%.250s/logs",dir);
    mkdir(lp_logdir,0755);
    unsigned top=0;DIR *d=opendir(lp_logdir);
    if(!d){pthread_mutex_unlock(&lp_sess_lock);return 0;}
    struct dirent *e;while((e=readdir(d))){unsigned n;if(sscanf(e->d_name,"session-%u.txt",&n)==1&&n>top)top=n;}
    rewinddir(d);
    /* keep the last LP_KEEP sessions */
    while((e=readdir(d))){unsigned n;char what[16];
        if(sscanf(e->d_name,"%15[a-z]-%u",what,&n)==2&&n+LP_KEEP<=top+1&&(!strcmp(what,"session")||!strcmp(what,"watch")||!strcmp(what,"crash"))){
            char p[600];snprintf(p,sizeof p,"%s/%s",lp_logdir,e->d_name);unlink(p);}}
    closedir(d);
    unsigned no=top+1;
    snprintf(lp_log_path,sizeof lp_log_path,"%s/session-%03u.txt",lp_logdir,no);
    snprintf(cl_session_path,sizeof cl_session_path,"%s",lp_log_path);
    snprintf(cl_crash_path,sizeof cl_crash_path,"%s/crash-%03u.txt",lp_logdir,no);
    cl_session_no=no;
    {char rp[320];snprintf(rp,sizeof rp,"%s/README.txt",lp_logdir);struct stat st;
     if(stat(rp,&st)){FILE *r=fopen(rp,"w");if(r){fputs(lp_readme,r);fclose(r);}}}
    __atomic_store_n(&lp_session,no,__ATOMIC_RELEASE);   /* before the header (it names the session) */
    FILE *f=fopen(lp_log_path,"a");
    if(f){lp_header(f);fflush(f);fsync(fileno(f));fclose(f);}
    pthread_mutex_unlock(&lp_sess_lock);
    return 1;
}
/* 1.96: lines are collected in memory and written by a thread of their own once a second.
 * Hardware 2026-11-15 (1.95 watch): EP136's main thread sat in fat_file_release ->
 * congestion_wait closing link-probe.txt (one open/append/close per line, ~10 lines/s while
 * a LINK deck starts): that stalls the UI. Now no EP136 thread touches the stick for the log;
 * the writer appends, syncs and closes once a second (so lines survive pulling the plug).
 * 1.99: the crash handler writes what is still in lp_buf itself. */
#define LP_BUF (192u*1024u)
static char lp_buf[LP_BUF];static size_t lp_len;static unsigned lp_dropped,lp_writer_on;
static size_t lp_written;static unsigned lp_full_noted;
static void *lp_writer(void *arg) {
    (void)arg;static char out[LP_BUF];
    for(;;) {
        usleep(1000000);
        if(!lp_session&&!lp_session_open())continue;   /* no stick yet: lines wait (or are dropped) */
        pthread_mutex_lock(&lp_lock);
        size_t n=lp_len;if(n)memcpy(out,lp_buf,n);lp_len=0;unsigned dropped=lp_dropped;lp_dropped=0;
        pthread_mutex_unlock(&lp_lock);
        if(!n&&!dropped)continue;
        if(lp_written>=LP_MAX_BYTES){
            if(!lp_full_noted){lp_full_noted=1;FILE *f=fopen(lp_log_path,"a");if(f){fprintf(f,"[%llu] log: session log full (%u MB), further lines only in a crash report\n",(unsigned long long)lk_now_ms(),LP_MAX_BYTES>>20);fclose(f);}}
            continue;}
        FILE *f=fopen(lp_log_path,"a");
        if(!f)continue;   /* stick pulled: these lines are lost */
        if(n)fwrite(out,1,n,f);
        if(dropped)fprintf(f,"[%llu] log: %u lines dropped (buffer full)\n",(unsigned long long)lk_now_ms(),dropped);
        fflush(f);fsync(fileno(f));fclose(f);lp_written+=n;
    }
    return 0;
}
static void lp_log(const char *fmt,...) {
    char line[2048];
    int k=snprintf(line,sizeof line,"[%llu] ",(unsigned long long)lk_now_ms());
    if(k<0)return;
    va_list a;va_start(a,fmt);int m=vsnprintf(line+k,sizeof line-(size_t)k-1,fmt,a);va_end(a);
    if(m<0)return;
    size_t len=(size_t)k+((size_t)m<sizeof line-(size_t)k-1?(size_t)m:sizeof line-(size_t)k-2);
    line[len++]='\n';
    pthread_mutex_lock(&lp_lock);
    if(!lp_writer_on) {
        lp_writer_on=1;pthread_t t;pthread_attr_t at;pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at,PTHREAD_CREATE_DETACHED);pthread_attr_setstacksize(&at,256*1024);
        if(pthread_create(&t,&at,lp_writer,0)!=0)lp_writer_on=0;
        pthread_attr_destroy(&at);
        cl_pending=lp_buf;cl_pending_len=&lp_len;
    }
    cl_ring_put(line,len);lp_lines++;
    if(lp_len+len<=LP_BUF){memcpy(lp_buf+lp_len,line,len);lp_len+=len;}
    else lp_dropped++;
    pthread_mutex_unlock(&lp_lock);
}
static void lp_hex(char *out,size_t cap,const unsigned char *p,size_t n) {
    size_t k=0;for(size_t i=0;i<n&&k+3<cap;i++)k+=(size_t)snprintf(out+k,cap-k,"%02x",p[i]);
    if(cap)out[k<cap?k:cap-1]=0;
}

/* ---- census ---------------------------------------------------------------------- */
/* Logged at the first call and then only when a line changes (the log is capped). */
static void lp_census_line(unsigned slot,const char *fmt,const char *a,const char *b) {
    static char last[16][1100];char line[1100];snprintf(line,sizeof line,fmt,a,b);
    if(slot<16&&!strcmp(last[slot],line))return;
    if(slot<16)snprintf(last[slot],sizeof last[slot],"%s",line);
    lp_log("%s",line);
}
/* ---- network census: why unicast to the deck may fail (2026-11-11: the OPUS stopped
 * answering ARP/ping/TCP from the Mac while its broadcasts kept coming) ------------ */
static void lp_file_lines(unsigned slot,const char *label,const char *path,int skip_header,int cols) {
    FILE *f=fopen(path,"r");char out[1000]="";size_t k=0;
    if(f) {
        char line[256];if(skip_header&&!fgets(line,sizeof line,f)){fclose(f);return;}
        while(fgets(line,sizeof line,f)&&k+80<sizeof out) {
            char w[8][64];int n=sscanf(line,"%63s %63s %63s %63s %63s %63s %63s %63s",w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7]);
            for(int i=0;i<n&&i<cols;i++)k+=(size_t)snprintf(out+k,sizeof out-k,"%s%s",i?" ":"",w[i]);
            k+=(size_t)snprintf(out+k,sizeof out-k," | ");
        }
        fclose(f);
    } else snprintf(out,sizeof out,"(no %s)",path);
    lp_census_line(slot,"net %s: %s",label,out);
}
/* one timed read of a link-media file, on its own thread (never blocks the link thread) */
static __attribute__((unused)) void *lp_timed_read(void *arg) {
    const char *path=arg;
    uint64_t t0=lk_now_ms();FILE *f=fopen(path,"rb");size_t total=0;static char buf[16384];
    if(f){size_t r;while((r=fread(buf,1,sizeof buf,f))>0){total+=r;if(lk_now_ms()-t0>60000)break;}fclose(f);}
    lp_log("link media read %s: %s %zu bytes in %llu ms",path,f?"opened,":"cannot open,",total,(unsigned long long)(lk_now_ms()-t0));
    return 0;
}
static void lp_net_census(void) {
    struct ifaddrs *list=0;char out[1000]="";size_t k=0;
    if(!getifaddrs(&list)) {
        for(struct ifaddrs *a=list;a&&k+60<sizeof out;a=a->ifa_next) {
            if(!a->ifa_addr||a->ifa_addr->sa_family!=AF_INET)continue;
            char ip[16],mask[16]="";inet_ntop(AF_INET,&((struct sockaddr_in *)a->ifa_addr)->sin_addr,ip,sizeof ip);
            if(a->ifa_netmask)inet_ntop(AF_INET,&((struct sockaddr_in *)a->ifa_netmask)->sin_addr,mask,sizeof mask);
            k+=(size_t)snprintf(out+k,sizeof out-k,"%s=%s/%s%s ",a->ifa_name,ip,mask,(a->ifa_flags&IFF_UP)?"":"(down)");
        }
        freeifaddrs(list);
    }
    lp_census_line(5,"net %s: %s","addresses",out);
    lp_file_lines(6,"route (iface dest gw flags)","/proc/net/route",1,4);
    lp_file_lines(7,"arp (ip hw flags mac mask dev)","/proc/net/arp",1,6);
    lp_file_lines(8,"iptables","/proc/net/ip_tables_names",0,1);
    /* TCP connections that are not listening: local, remote, state */
    FILE *f=fopen("/proc/net/tcp","r");char conns[1000]="";size_t c=0;
    if(f) {
        char line[256];if(fgets(line,sizeof line,f))
        while(fgets(line,sizeof line,f)&&c+60<sizeof conns) {
            unsigned lip,lport,rip,rport,st;
            if(sscanf(line,"%*s %x:%x %x:%x %x",&lip,&lport,&rip,&rport,&st)!=5||st==0x0a)continue;
            c+=(size_t)snprintf(conns+c,sizeof conns-c,"%u->%u.%u.%u.%u:%u st%x ",lport,rip&255,(rip>>8)&255,(rip>>16)&255,rip>>24,rport,st);
        }
        fclose(f);
    }
    lp_census_line(9,"net %s: %s","tcp connections",conns[0]?conns:"(none)");
    /* link media over FUSE (EP136 -> msg queue -> FuseMountDaemon -> FuseFilsine): processes,
     * mounts under /media and the /media folder itself */
    {char procs[600]="";size_t q=0;DIR *pd=opendir("/proc");
     if(pd){struct dirent *e;while((e=readdir(pd))&&q+80<sizeof procs){
        if(e->d_name[0]<'0'||e->d_name[0]>'9')continue;
        char cp[64],cmd[300]="";snprintf(cp,sizeof cp,"/proc/%.20s/cmdline",e->d_name);
        FILE *cf=fopen(cp,"r");if(!cf)continue;size_t r=fread(cmd,1,sizeof cmd-1,cf);fclose(cf);
        for(size_t i=0;i<r;i++){if(!cmd[i])cmd[i]=' ';}
        cmd[r]=0;
        if(strstr(cmd,"Fuse"))q+=(size_t)snprintf(procs+q,sizeof procs-q,"[%s] %.120s | ",e->d_name,cmd);}
      closedir(pd);}
     lp_census_line(10,"fuse %s: %s","processes",procs[0]?procs:"(none)");}
    {char mounts[800]="";size_t q=0;FILE *mf=fopen("/proc/mounts","r");
     if(mf){char l[300];
      while(fgets(l,sizeof l,mf)&&q+120<sizeof mounts)
        if(strstr(l,"/media/")||strstr(l,"fuse")){l[strcspn(l,"\n")]=0;q+=(size_t)snprintf(mounts+q,sizeof mounts-q,"%.110s | ",l);}
     }
     if(mf)fclose(mf);
     DIR *md=opendir("/media");if(md){struct dirent *e;q+=(size_t)snprintf(mounts+q,sizeof mounts-q,"/media:");
        while((e=readdir(md))&&q+30<sizeof mounts){if(e->d_name[0]!='.')q+=(size_t)snprintf(mounts+q,sizeof mounts-q," %.20s",e->d_name);}
        closedir(md);}
     lp_census_line(11,"fuse %s: %s","mounts",mounts[0]?mounts:"(none)");}
    /* link media: what the FUSE mount shows, and one timed read of export.pdb */
    {DIR *pd=opendir("/media");struct dirent *e;
     while(pd&&(e=readdir(pd))) {
        if(strncmp(e->d_name,"player",6))continue;
        for(int s=0;s<2;s++) {
            char dir[200],list[700]="";size_t q=0;
            snprintf(dir,sizeof dir,"/media/%.20s/%s",e->d_name,s?"usb/PIONEER/rekordbox":"usb");
            DIR *ud=opendir(dir);
            if(ud){struct dirent *f;while((f=readdir(ud))&&q+40<sizeof list){if(f->d_name[0]!='.')q+=(size_t)snprintf(list+q,sizeof list-q,"%.30s ",f->d_name);}closedir(ud);}
            else snprintf(list,sizeof list,"(cannot open: %s)",strerror(errno));
            lp_census_line(s?13:12,"link media %s: %s",dir,list[0]?list:"(empty)");
        }
        /* 1.79: the timed read of the player's export.pdb is gone. On hardware (2026-11-13) it
         * broke off at 360 KB while EP136 was reading the same file over the same link mount,
         * and the LINK browse that followed failed. */
     }
     if(pd)closedir(pd);}
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
/* The NXS2-style 0x11c-byte status, laid out as the XDJ-1000MK2's own (captured
 * 2026-11-11, hwtest-link-20261111/opus3). The older 0xd4-byte CDJ-2000 layout made the
 * XDJ say "linked to a player with older firmware"; the firmware text is copied from the
 * other players' status (lk_peer_fw), so ours is never older than theirs. */
#define LP_STATUS_LEN 0x11c
static void lp_status(unsigned char p[LP_STATUS_LEN],unsigned counter) {
    memset(p,0,LP_STATUS_LEN);memcpy(p,lk_magic,10);p[0x0a]=0x0a;
    memcpy(p+0x0b,LK_NAME,strlen(LK_NAME));
    p[0x1f]=0x01;p[0x20]=0x04;p[0x21]=(unsigned char)LK_PLAYER;p[0x22]=0x00;p[0x23]=LP_STATUS_LEN-0x24;
    p[0x24]=(unsigned char)LK_PLAYER;
    p[0x68]=0x01;
    p[0x6f]=0x00;            /* USB: loaded */
    p[0x73]=0x04;            /* SD: empty */
    p[0x75]=0x01;            /* link media available */
    p[0x77]=0x01;
    memcpy(p+0x7c,lk_peer_fw[0]?lk_peer_fw:"1.13",4);
    p[0x87]=0x01;            /* sync counter (0x84, BE) = 1 */
    p[0x89]=0x84;p[0x8a]=0x00;p[0x8b]=0xfe;
    p[0x8d]=0x10;            /* pitch 1 = 0x00100000: 0 % */
    memset(p+0x90,0xff,8);p[0x90]=0x7f;p[0x94]=0x7f;   /* no track: BPM unknown */
    p[0x99]=0x10;            /* pitch 2 */
    p[0x9f]=0xff;memset(p+0xa0,0xff,4);p[0xa4]=0x01;p[0xa5]=0xff;
    p[0xb6]=0x01;
    p[0xc1]=0x10;p[0xc5]=0x10;   /* pitch 3, 4 */
    p[0xc8]=(unsigned char)(counter>>24);p[0xc9]=(unsigned char)(counter>>16);p[0xca]=(unsigned char)(counter>>8);p[0xcb]=(unsigned char)counter;
    p[0xcc]=0x0f;p[0xcd]=0x01;
    p[0xd0]=0x12;p[0xd1]=0x34;p[0xd2]=0x56;p[0xd3]=0x78;
    p[0xd7]=0x01;memset(p+0xd8,0x01,5);p[0xdd]=0x02;p[0xde]=0x01;p[0xdf]=0x01;p[0xe0]=0x01;
    p[0x113]=0x05;
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

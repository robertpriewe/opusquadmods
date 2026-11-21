/* 1.99: crash reports for people testing Pro DJ Link (Experimental).
 *
 * Every start of the app is a session with its own files in MODS/prodjlink/logs/ (see
 * prodjlink_probe.h): session-NNN.txt (the mod's log), watch-NNN.txt (once-a-second health,
 * shows freezes) and, if the app dies on a fatal signal, crash-NNN.txt from this handler:
 *   - signal, code, fault address, the thread (id + name) and the mod step it was in;
 *   - pc, lr and all registers; each code address is shown as <file>+<offset> (EP136,
 *     a library or a mod's mod.so), so it can be matched to source without the device;
 *   - a frame-pointer backtrace and a scan of the stack for code addresses;
 *   - the mod's last status line and the last ~48 KB of log lines;
 * and the log lines not yet written to the stick are appended to session-NNN.txt.
 * Memory is read with process_vm_readv, so a bad pointer can't fault inside the handler.
 * Afterwards the previous handler runs (the firmware's OPUS-CRASH.txt note, then EP136's
 * own), exactly as without the mod. The handler is installed on the first tick and checked
 * every 10 s by the watch thread. */
#ifndef PRODJLINK_CRASHLOG_H
#define PRODJLINK_CRASHLOG_H
#include <signal.h>
#include <ucontext.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define PDL_VERSION "2.0"
#define PDL_RELEASE 1   /* 2.0: research captures (cue dumps) off */

/* ---- recent log lines (written under lp_lock by lp_log) ---- */
#define CL_RING (48u*1024u)
static char cl_ring[CL_RING];static uint64_t cl_ring_total;
static void cl_ring_put(const char *s,size_t n) {
    if(n>CL_RING){s+=n-CL_RING;n=CL_RING;}
    size_t pos=(size_t)(cl_ring_total%CL_RING),a=CL_RING-pos;if(a>n)a=n;
    memcpy(cl_ring+pos,s,a);if(n>a)memcpy(cl_ring,s+a,n-a);
    cl_ring_total+=n;
}

/* ---- the mod's last status and current step ---- */
static char cl_status[2][600];static unsigned cl_status_cur;
static void cl_set_status(const char *s) {
    unsigned next=!__atomic_load_n(&cl_status_cur,__ATOMIC_ACQUIRE);
    snprintf(cl_status[next],sizeof cl_status[next],"%s",s);
    __atomic_store_n(&cl_status_cur,next,__ATOMIC_RELEASE);
}
static __thread const char *cl_step;   /* set in tick() before each part */
#define CL_STEP(s) (cl_step=(s))

/* ---- executable mappings, so code addresses can be named ---- */
#define CL_MAPS 192
struct cl_map{uintptr_t lo,hi,off;char name[64];};
static struct cl_map cl_maps[2][CL_MAPS];static unsigned cl_nmaps[2],cl_maps_cur;
static void cl_maps_refresh(void) {
    unsigned next=!__atomic_load_n(&cl_maps_cur,__ATOMIC_ACQUIRE),n=0;
    FILE *f=fopen("/proc/self/maps","r");if(!f)return;
    char line[512];
    while(n<CL_MAPS&&fgets(line,sizeof line,f)) {
        unsigned long lo,hi,off;char perms[8];
        if(sscanf(line,"%lx-%lx %7s %lx",&lo,&hi,perms,&off)!=4||perms[2]!='x')continue;
        char *path=strchr(line,'/');if(!path)path=strchr(line,'[');
        struct cl_map *m=&cl_maps[next][n++];m->lo=lo;m->hi=hi;m->off=off;
        if(!path){snprintf(m->name,sizeof m->name,"anon");continue;}
        path[strcspn(path,"\n")]=0;
        /* last two parts: "prodjlink/mod.so", "lib/libc-2.29.so", "bin/EP136" */
        char *s=strrchr(path,'/'),*t=s;
        if(s&&s>path){t=s-1;while(t>path&&*t!='/')t--;if(*t=='/')t++;}
        snprintf(m->name,sizeof m->name,"%s",t?t:path);
    }
    fclose(f);
    cl_nmaps[next]=n;__atomic_store_n(&cl_maps_cur,next,__ATOMIC_RELEASE);
}
static const struct cl_map *cl_find(uintptr_t a) {
    unsigned c=__atomic_load_n(&cl_maps_cur,__ATOMIC_ACQUIRE);
    for(unsigned i=0;i<cl_nmaps[c];i++)if(a>=cl_maps[c][i].lo&&a<cl_maps[c][i].hi)return &cl_maps[c][i];
    return 0;
}

/* ---- paths, set when the session's files are known ---- */
static char cl_crash_path[320],cl_session_path[320];static unsigned cl_session_no;

/* ---- async-signal-safe writer ---- */
static char cw_buf[8192];static unsigned cw_n;static int cw_fd=-1;
static void cw_flush(void){if(cw_fd>=0&&cw_n){if(write(cw_fd,cw_buf,cw_n)<0){}}cw_n=0;}
static void cw_c(char c){if(cw_n>=sizeof cw_buf)cw_flush();cw_buf[cw_n++]=c;}
static void cw_s(const char *s){for(;s&&*s;s++)cw_c(*s);}
static void cw_x(uint64_t v){static const char h[]="0123456789abcdef";char t[16];int k=0;
    do{t[k++]=h[v&15];v>>=4;}while(v&&k<16);cw_s("0x");while(k)cw_c(t[--k]);}
static void cw_u(uint64_t v){char t[24];int k=0;do{t[k++]=(char)('0'+v%10);v/=10;}while(v&&k<24);while(k)cw_c(t[--k]);}
static void cw_loc(uintptr_t a) {
    const struct cl_map *m=cl_find(a);
    if(m){cw_s(" = ");cw_s(m->name);cw_s("+");cw_x((uint64_t)(a-m->lo+m->off));}
}
static int cl_peek(uintptr_t a,void *out,size_t n) {
    struct iovec l={out,n},r={(void *)a,n};
    return syscall(SYS_process_vm_readv,(long)getpid(),&l,1L,&r,1L,0L)==(long)n;
}
static const char *cl_signame(int s) {
    switch(s){case SIGSEGV:return "SIGSEGV (bad memory access)";case SIGBUS:return "SIGBUS (bus error)";
    case SIGILL:return "SIGILL (illegal instruction)";case SIGFPE:return "SIGFPE (arithmetic)";
    case SIGABRT:return "SIGABRT (abort)";default:return "signal";}
}

static const int cl_signals[5]={SIGSEGV,SIGBUS,SIGILL,SIGFPE,SIGABRT};
static struct sigaction cl_old[5];
static int cl_crash_tid;   /* first thread in the handler */
static uint64_t cl_mono_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;}
/* the session log: lines still in memory (defined in prodjlink_probe.h) */
static char *cl_pending;static size_t *cl_pending_len;

static void cl_report(int sig,siginfo_t *si,void *ucv,int tid) {
    if(!cl_crash_path[0])return;
    cw_fd=open(cl_crash_path,O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0644);
    if(cw_fd<0)return;
    cw_n=0;
    cw_s("==== Pro DJ Link (Experimental) " PDL_VERSION " crash report, session ");cw_u(cl_session_no);cw_s(" ====\n");
    cw_s("time ");cw_u(cl_mono_ms());cw_s(" ms (same clock as the session log)\n");
    cw_s("signal ");cw_u((uint64_t)sig);cw_s(" ");cw_s(cl_signame(sig));
    cw_s(" code ");if(si&&si->si_code<0){cw_s("-");cw_u((uint64_t)-si->si_code);}else cw_u(si?(uint64_t)si->si_code:0);
    cw_s(" fault address ");cw_x((uint64_t)(uintptr_t)(si?si->si_addr:0));cw_loc((uintptr_t)(si?si->si_addr:0));cw_c('\n');
    cw_s("thread ");cw_u((uint64_t)tid);cw_s(tid==getpid()?" (main) '":" '");
    {char path[48]="/proc/self/task/",name[32];unsigned k=16;char t[12];int d=0;unsigned v=(unsigned)tid;
     do{t[d++]=(char)('0'+v%10);v/=10;}while(v);while(d)path[k++]=t[--d];memcpy(path+k,"/comm",6);
     int fd=open(path,O_RDONLY|O_CLOEXEC);ssize_t r=fd>=0?read(fd,name,sizeof name-1):-1;if(fd>=0)close(fd);
     if(r>0){name[r]=0;name[strcspn(name,"\n")]=0;cw_s(name);}}
    cw_s("' mod step ");cw_s(cl_step?cl_step:"-");cw_c('\n');
#ifdef __aarch64__
    ucontext_t *uc=ucv;
    if(uc) {
        const mcontext_t *m=&uc->uc_mcontext;
        cw_s("pc ");cw_x(m->pc);cw_loc((uintptr_t)m->pc);cw_c('\n');
        cw_s("lr ");cw_x(m->regs[30]);cw_loc((uintptr_t)m->regs[30]);cw_c('\n');
        cw_s("sp ");cw_x(m->sp);cw_s(" fp ");cw_x(m->regs[29]);cw_s(" pstate ");cw_x(m->pstate);cw_c('\n');
        for(int i=0;i<29;i++){cw_s("x");cw_u((uint64_t)i);cw_s(i<10?"  ":" ");cw_x(m->regs[i]);cw_loc((uintptr_t)m->regs[i]);cw_c('\n');}
        cw_s("backtrace (frame pointers):\n");
        cw_s("  #0 ");cw_x(m->pc);cw_loc((uintptr_t)m->pc);cw_c('\n');
        cw_s("  #1 ");cw_x(m->regs[30]);cw_loc((uintptr_t)m->regs[30]);cw_c('\n');
        uintptr_t fp=(uintptr_t)m->regs[29];
        for(unsigned i=2;i<40&&fp&&!(fp&7);i++) {
            uint64_t rec[2];if(!cl_peek(fp,rec,sizeof rec))break;
            if(!rec[1])break;
            cw_s("  #");cw_u(i);cw_s(" ");cw_x(rec[1]);cw_loc((uintptr_t)rec[1]);cw_c('\n');
            if(rec[0]<=fp||rec[0]-fp>(8u<<20))break;
            fp=(uintptr_t)rec[0];
        }
        cw_s("stack words that point into code (sp+offset):\n");
        unsigned shown=0;
        for(unsigned off=0;off<8192&&shown<64;off+=512) {
            uint64_t w[64];if(!cl_peek((uintptr_t)m->sp+off,w,sizeof w))break;
            for(unsigned i=0;i<64&&shown<64;i++)if(cl_find((uintptr_t)w[i])) {
                shown++;cw_s("  sp+");cw_x(off+i*8);cw_s(" ");cw_x(w[i]);cw_loc((uintptr_t)w[i]);cw_c('\n');}
        }
        cw_s("code near pc:");
        {uint32_t ins[8];if(cl_peek((uintptr_t)m->pc-16,ins,sizeof ins))for(int i=0;i<8;i++){cw_c(' ');cw_x(ins[i]);}else cw_s(" (unreadable)");}
        cw_c('\n');
    }
#else
    (void)ucv;
#endif
    cw_s("code mappings:\n");
    {unsigned c=__atomic_load_n(&cl_maps_cur,__ATOMIC_ACQUIRE);
     for(unsigned i=0;i<cl_nmaps[c];i++){cw_s("  ");cw_x(cl_maps[c][i].lo);cw_s("-");cw_x(cl_maps[c][i].hi);cw_s(" off ");cw_x(cl_maps[c][i].off);cw_c(' ');cw_s(cl_maps[c][i].name);cw_c('\n');}}
    cw_s("last status: ");cw_s(cl_status[__atomic_load_n(&cl_status_cur,__ATOMIC_ACQUIRE)]);cw_c('\n');
    cw_s("---- last log lines ----\n");
    {uint64_t total=cl_ring_total;size_t n=total<CL_RING?(size_t)total:CL_RING,pos=(size_t)(total%CL_RING);
     size_t start=total<CL_RING?0:pos;
     /* skip to the first full line */
     size_t i=0;if(total>=CL_RING)while(i<n&&cl_ring[(start+i)%CL_RING]!='\n')i++;
     for(i=i<n?i+(total>=CL_RING):0;i<n;i++)cw_c(cl_ring[(start+i)%CL_RING]);}
    cw_s("==== end of crash report ====\n\n");
    cw_flush();fsync(cw_fd);close(cw_fd);cw_fd=-1;
    /* the session log's last second, then a pointer to this report */
    if(cl_session_path[0]) {
        cw_fd=open(cl_session_path,O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC,0644);
        if(cw_fd>=0) {
            if(cl_pending&&cl_pending_len){size_t n=*cl_pending_len;if(n&&write(cw_fd,cl_pending,n)<0){}}
            cw_n=0;cw_s("[");cw_u(cl_mono_ms());cw_s("] CRASH: ");cw_s(cl_signame(sig));cw_s(" in thread ");cw_u((uint64_t)tid);
            cw_s(", report in crash-");{char t[8];int k=0;unsigned v=cl_session_no;do{t[k++]=(char)('0'+v%10);v/=10;}while(v);while(k<3)t[k++]='0';while(k)cw_c(t[--k]);}
            cw_s(".txt\n");cw_flush();fsync(cw_fd);close(cw_fd);cw_fd=-1;
        }
    }
}
static void cl_handler(int sig,siginfo_t *si,void *ucv) {
    int tid=(int)syscall(SYS_gettid);
    int first=0,expected=0;
    if(__atomic_compare_exchange_n(&cl_crash_tid,&expected,tid,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))first=1;
    else if(expected==tid) {   /* again in the same thread: fault in the handler or a loop of handlers */
        signal(sig,SIG_DFL);return;
    } else {                   /* another thread crashed at the same time: let the first one finish */
        struct timespec d={0,50*1000*1000};for(int i=0;i<60;i++)nanosleep(&d,0);
    }
    if(first)cl_report(sig,si,ucv,tid);
    for(int i=0;i<5;i++)if(cl_signals[i]==sig){
        struct sigaction o=cl_old[i];
        if(!(o.sa_flags&SA_SIGINFO)&&(o.sa_handler==SIG_IGN))o.sa_handler=SIG_DFL;
        sigaction(sig,&o,NULL);break;}
    /* raised or sent (not a fault) or abort: returning would not repeat it */
    if(sig==SIGABRT||(si&&si->si_code<=0))raise(sig);
}
static unsigned cl_installed;
static int cl_is_ours(int sig) {
    struct sigaction cur;if(sigaction(sig,NULL,&cur))return 1;
    return (cur.sa_flags&SA_SIGINFO)&&cur.sa_sigaction==cl_handler;
}
/* first tick, then every 10 s from the watch: (re)install where something else replaced it */
static unsigned cl_install(void) {
    unsigned changed=0;
    for(int i=0;i<5;i++) {
        if(cl_installed&&cl_is_ours(cl_signals[i]))continue;
        struct sigaction sa;memset(&sa,0,sizeof sa);
        sa.sa_sigaction=cl_handler;sa.sa_flags=SA_SIGINFO|SA_ONSTACK;sigemptyset(&sa.sa_mask);
        if(!sigaction(cl_signals[i],&sa,&cl_old[i]))changed++;
    }
    cl_installed=1;
    return changed;
}
#endif

/* 1.95: freeze watch. Hardware 2026-11-15: with PRO DJ LINK on, the OPUS froze completely
 * (no reboot, no Reload mods, power button ignored) about a minute after a normal play-along;
 * link-probe.txt simply stopped (the last ~30 s were never written to the stick).
 * A thread of its own, independent of EP136's threads, checks every second:
 *  - the mod's tick (EP136 message thread) still runs; if not, logs the main thread's state,
 *    kernel wait channel and kernel stack;
 *  - threads in D state (waiting in the kernel, e.g. on the XDJ's USB over NFS/FUSE): name,
 *    wait channel, first stack lines;
 *  - every 10 s a health line: free memory, load, threads, EP136 CPU.
 * lp_log syncs the log to the stick at least once a second (prodjlink_probe.h), so the lines
 * before a freeze survive pulling the plug. Read-only: /proc only.
 *
 * 1.98: hardware 2026-11-16 (1.97): the OPUS froze again ~92 s into a session, right after a
 * deck 2 PAUSE while a heavy EP136 job (3 threads in mlock, EP136 CPU 400 %) ran. The log
 * just stopped: the watch's lines go through the same in-memory log as everything else, so
 * nothing of the freeze reached the stick. Now the watch also writes freeze-watch.txt next to
 * link-probe.txt itself (write + fdatasync, no buffer), every second:
 *    T <ms> tick <age> mem <MB> cpu <EP136 %> D <n> [rt]
 *  plus, while EP136 CPU >= 300 %, the tick is late or threads wait in D: the 6 busiest
 *  threads of the last second (tid, name, state, CPU %, scheduling policy / RT priority);
 *  plus, when the tick stalls, the main thread's wait channel and kernel stack.
 * The scan stays at normal priority (300 /proc reads a second must not delay EP136's audio).
 * A second, tiny thread asks for SCHED_FIFO 90: it only checks that the watch reported; when
 * the watch is 3 s late (starved, e.g. by an RT thread spinning on every core) it writes the
 * runnable threads with their scheduling policy and the main thread's kernel stack itself.
 * The file is renamed to freeze-watch-old.txt past 4 MB.
 * 1.99: logs/watch-NNN.txt per session (watch-NNN-old.txt past 4 MB); every 10 s the watch also
 * refreshes the code mappings for crash reports and checks the crash handler is still installed. */
#ifndef PRODJLINK_WATCH_H
#define PRODJLINK_WATCH_H
#include <pthread.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sched.h>
#include <stdarg.h>
#include <sys/stat.h>

static uint64_t wt_tick_ms;          /* last tick() */
static unsigned wt_started;
static uint64_t wt_mono_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;}
static void wt_read(const char *path,char *out,size_t n) {
    out[0]=0;FILE *f=fopen(path,"r");if(!f)return;
    size_t k=fread(out,1,n-1,f);fclose(f);out[k]=0;
    for(size_t i=0;i<k;i++)if(out[i]=='\n')out[i]=i+1<k?'|':0;
}
/* state letter and name of a task */
static char wt_task(const char *tid,char *name,size_t n) {
    char path[64],buf[512];snprintf(path,sizeof path,"/proc/self/task/%s/stat",tid);
    wt_read(path,buf,sizeof buf);
    char *a=strchr(buf,'('),*b=strrchr(buf,')');
    if(!a||!b||b<a){name[0]=0;return '?';}
    size_t len=(size_t)(b-a-1);if(len>=n)len=n-1;memcpy(name,a+1,len);name[len]=0;
    return b[1]==' '?b[2]:'?';
}
static void wt_task_detail(const char *tid,const char *why) {
    char name[32],path[64],wchan[64],stack[400];
    char st=wt_task(tid,name,sizeof name);
    snprintf(path,sizeof path,"/proc/self/task/%s/wchan",tid);wt_read(path,wchan,sizeof wchan);
    snprintf(path,sizeof path,"/proc/self/task/%s/stack",tid);wt_read(path,stack,sizeof stack);
    lp_log("watch: %s thread %s '%s' state %c wchan '%s' stack %s",why,tid,name,st,wchan,stack[0]?stack:"(none)");
}
/* ---- 1.98: freeze-watch.txt, written directly by the watch thread ---- */
static int fw_fd=-1;static unsigned fw_rt;static char fw_path[300];
static pthread_mutex_t fw_lock=PTHREAD_MUTEX_INITIALIZER;static uint64_t fw_watch_ms;
static void fw_open(void){
    if(fw_fd>=0)return;
    static unsigned busy;if(__atomic_exchange_n(&busy,1,__ATOMIC_ACQ_REL))return;
    /* 1.99: logs/watch-NNN.txt of this session */
    if(!lp_session_open()){__atomic_store_n(&busy,0,__ATOMIC_RELEASE);return;}
    snprintf(fw_path,sizeof fw_path,"%.270s/watch-%03u.txt",lp_logdir,lp_session);
    fw_fd=open(fw_path,O_WRONLY|O_APPEND|O_CREAT,0644);
    __atomic_store_n(&busy,0,__ATOMIC_RELEASE);
}
static void fw_write(const char *fmt,...){
    fw_open();if(fw_fd<0)return;
    char line[1200];int k=snprintf(line,sizeof line,"[%llu] ",(unsigned long long)lk_now_ms());
    va_list a;va_start(a,fmt);int m=vsnprintf(line+k,sizeof line-(size_t)k-1,fmt,a);va_end(a);
    if(k<0||m<0)return;
    size_t len=(size_t)k+((size_t)m<sizeof line-(size_t)k-1?(size_t)m:sizeof line-(size_t)k-2);line[len++]='\n';
    pthread_mutex_lock(&fw_lock);
    if(fw_fd>=0&&write(fw_fd,line,len)<0){close(fw_fd);fw_fd=-1;}
    pthread_mutex_unlock(&fw_lock);
}
static void fw_sync(void){
    pthread_mutex_lock(&fw_lock);
    if(fw_fd<0){pthread_mutex_unlock(&fw_lock);return;}
    fdatasync(fw_fd);
    struct stat st;
    if(!fstat(fw_fd,&st)&&st.st_size>4*1024*1024){
        char old[320];snprintf(old,sizeof old,"%.290s",fw_path);char *dot=strrchr(old,'.');
        if(dot){snprintf(dot,sizeof old-(size_t)(dot-old),"-old.txt");close(fw_fd);fw_fd=-1;rename(fw_path,old);}
    }
    pthread_mutex_unlock(&fw_lock);
}
/* per-thread CPU of the last second */
#define FW_T 640
struct fw_task{int tid;unsigned long long cpu;unsigned seen;};
static struct fw_task fw_tasks[FW_T];
static struct fw_task *fw_slot(int tid){
    unsigned h=(unsigned)tid*2654435761u%FW_T;
    for(unsigned i=0;i<FW_T;i++){struct fw_task *t=&fw_tasks[(h+i)%FW_T];if(t->tid==tid||t->tid==0){t->tid=tid;return t;}}
    return 0;
}
struct fw_busy{int tid;unsigned pct;char st;unsigned pol,rtp;char name[20];};

static void *wt_thread(void *arg) {
    (void)arg;
    uint64_t last_health=0,last_stall_log=0;unsigned long long last_cpu=0;uint64_t last_cpu_ms=0;
    char main_tid[16];snprintf(main_tid,sizeof main_tid,"%d",(int)getpid());
    fw_write("watch start (" PDL_VERSION ", session %03u)",lp_session);fw_sync();
    unsigned pass=0;long hz=sysconf(_SC_CLK_TCK);if(hz<=0)hz=100;
    uint64_t prev_ms=0;unsigned long long prev_total=0;
    for(;;) {
        sleep(1);
        uint64_t now=wt_mono_ms();
        uint64_t tick=__atomic_load_n(&wt_tick_ms,__ATOMIC_ACQUIRE);
        if(tick&&now>tick+1500&&now-last_stall_log>=3000) {
            last_stall_log=now;
            lp_log("watch: the mod's tick has not run for %llu ms",(unsigned long long)(now-tick));
            wt_task_detail(main_tid,"main");
            {char path[64],wchan[64],stack[400];
             snprintf(path,sizeof path,"/proc/self/task/%s/wchan",main_tid);wt_read(path,wchan,sizeof wchan);
             snprintf(path,sizeof path,"/proc/self/task/%s/stack",main_tid);wt_read(path,stack,sizeof stack);
             fw_write("STALL tick %llu ms late; main wchan '%s' stack %s",(unsigned long long)(now-tick),wchan,stack[0]?stack:"(none)");}
        }
        /* D-state threads */
        static unsigned last_d;
        int health_due=now-last_health>=10000;
        DIR *d=opendir("/proc/self/task");unsigned threads=0,dstate=0,logged=0;
        struct fw_busy busy[6];unsigned nbusy=0;unsigned long long total=0;pass++;
        uint64_t span=prev_ms&&now>prev_ms?now-prev_ms:1000;
        if(d){struct dirent *e;while((e=readdir(d))){
            if(e->d_name[0]<'0'||e->d_name[0]>'9')continue;
            threads++;
            char path[64],buf[512];snprintf(path,sizeof path,"/proc/self/task/%.20s/stat",e->d_name);
            wt_read(path,buf,sizeof buf);
            char *a=strchr(buf,'('),*b=strrchr(buf,')');if(!a||!b||b<a||!b[1])continue;
            char st=b[2];
            /* fields after ')': 3 state ... 14 utime 15 stime ... 40 rt_priority 41 policy */
            unsigned long long ut=0,stt=0;unsigned rtp=0,pol=0;int field=2;
            for(char *q=b+1;*q&&field<41;q++)if(*q==' '){field++;
                if(field==14)ut=strtoull(q+1,0,10);else if(field==15)stt=strtoull(q+1,0,10);
                else if(field==40)rtp=(unsigned)strtoul(q+1,0,10);else if(field==41)pol=(unsigned)strtoul(q+1,0,10);}
            unsigned long long cpu=ut+stt;total+=cpu;
            int tid=atoi(e->d_name);struct fw_task *t=fw_slot(tid);unsigned pct=0;
            if(t){if(t->seen&&t->seen+1==pass&&cpu>=t->cpu)pct=(unsigned)((cpu-t->cpu)*100000ull/(unsigned long long)hz/span);
                  t->cpu=cpu;t->seen=pass;}
            if(pct>=5){
                struct fw_busy x={tid,pct,st,pol,rtp,{0}};size_t nl=(size_t)(b-a-1);if(nl>=sizeof x.name)nl=sizeof x.name-1;memcpy(x.name,a+1,nl);
                unsigned at=nbusy<6?nbusy++:6;
                if(at==6){unsigned lo=0;for(unsigned i=1;i<6;i++)if(busy[i].pct<busy[lo].pct)lo=i;if(busy[lo].pct<pct)busy[lo]=x;}
                else busy[at]=x;
            }
            if(st=='D'){dstate++;
                /* details when the count changes or with the health line (log is capped) */
                if(logged<6&&(health_due||last_d==0)){logged++;wt_task_detail(e->d_name,"waiting (D)");}}
        }closedir(d);}
        /* forget threads that ended */
        if(pass%30==0)for(unsigned i=0;i<FW_T;i++)if(fw_tasks[i].tid&&fw_tasks[i].seen!=pass)fw_tasks[i]=(struct fw_task){0,0,0};
        unsigned allpct=prev_ms&&total>=prev_total?(unsigned)((total-prev_total)*100000ull/(unsigned long long)hz/span):0;
        prev_total=total;prev_ms=now;
        {char mem[2048];wt_read("/proc/meminfo",mem,sizeof mem);char *av=strstr(mem,"MemAvailable:");
         unsigned long mb=av?strtoul(av+13,0,10)/1024:0;
         uint64_t age=tick&&now>tick?now-tick:0;
         fw_write("T tick %llu mem %lu cpu %u D %u thr %u%s",(unsigned long long)age,mb,allpct,dstate,threads,__atomic_load_n(&fw_rt,__ATOMIC_RELAXED)?" rt":"");
         if(allpct>=300||age>1500||dstate){
            for(unsigned i=0;i<nbusy;i++)for(unsigned j=i+1;j<nbusy;j++)if(busy[j].pct>busy[i].pct){struct fw_busy x=busy[i];busy[i]=busy[j];busy[j]=x;}
            char top[700];size_t k=0;top[0]=0;
            for(unsigned i=0;i<nbusy&&k<sizeof top-80;i++)
                k+=(size_t)snprintf(top+k,sizeof top-k," %d '%s' %c %u%% pol %u/%u;",busy[i].tid,busy[i].name,busy[i].st,busy[i].pct,busy[i].pol,busy[i].rtp);
            fw_write("  busy:%s",nbusy?top:" (none over 5 %)");
         }
         fw_sync();}
        __atomic_store_n(&fw_watch_ms,wt_mono_ms(),__ATOMIC_RELEASE);
        int changed=dstate!=last_d;last_d=dstate;
        if(health_due||changed) {
            last_health=now;
            cl_maps_refresh();   /* 1.99: code mappings for crash reports */
            {unsigned re=cl_install();if(re)lp_log("crash log: handler for %u signal(s) was replaced by another one, installed again",re);}
            char mem[2048],load[128],stat[512];
            wt_read("/proc/meminfo",mem,sizeof mem);wt_read("/proc/loadavg",load,sizeof load);
            wt_read("/proc/self/stat",stat,sizeof stat);
            unsigned long long ut=0,stt=0;char *b=strrchr(stat,')');
            if(b){int field=2;char *p=b+1;
                while(*p&&field<15){if(*p==' '){field++;if(field==14)ut=strtoull(p+1,0,10);if(field==15)stt=strtoull(p+1,0,10);}p++;}}
            unsigned long long cpu=ut+stt;long hz=sysconf(_SC_CLK_TCK);if(hz<=0)hz=100;
            double pct=last_cpu_ms&&now>last_cpu_ms?(double)(cpu-last_cpu)*1000.0/(double)hz/(double)(now-last_cpu_ms)*100.0:0;
            last_cpu=cpu;last_cpu_ms=now;
            char *avail=strstr(mem,"MemAvailable:");unsigned long availkb=avail?strtoul(avail+13,0,10):0;
            lp_log("watch: health mem available %lu MB, load %s, EP136 threads %u (D %u), EP136 CPU %.0f %%, tick %llu ms ago",
                availkb/1024,load,threads,dstate,pct,tick?(unsigned long long)(now-tick):0ull);
        }
    }
    return 0;
}
/* 1.98: RT guard: runs even when normal threads are starved */
static void *fw_guard(void *arg){
    (void)arg;
    struct sched_param sp;memset(&sp,0,sizeof sp);sp.sched_priority=90;
    __atomic_store_n(&fw_rt,pthread_setschedparam(pthread_self(),SCHED_FIFO,&sp)==0,__ATOMIC_RELEASE);
    char main_tid[16];snprintf(main_tid,sizeof main_tid,"%d",(int)getpid());
    uint64_t last_report=0;
    for(;;){
        sleep(1);
        uint64_t now=wt_mono_ms(),w=__atomic_load_n(&fw_watch_ms,__ATOMIC_ACQUIRE);
        if(!w||now<w+3000||now<last_report+3000)continue;
        last_report=now;
        uint64_t tick=__atomic_load_n(&wt_tick_ms,__ATOMIC_ACQUIRE);
        char load[128];wt_read("/proc/loadavg",load,sizeof load);
        fw_write("GUARD watch %llu ms late, tick %llu ms ago, load %s",(unsigned long long)(now-w),
                 tick&&now>tick?(unsigned long long)(now-tick):0ull,load);
        /* runnable threads and their scheduling (policy 1 FIFO / 2 RR = real time) */
        DIR *d=opendir("/proc/self/task");char list[900];size_t k=0;list[0]=0;unsigned nr=0;
        if(d){struct dirent *e;while((e=readdir(d))){
            if(e->d_name[0]<'0'||e->d_name[0]>'9')continue;
            char path[64],buf[512];snprintf(path,sizeof path,"/proc/self/task/%.20s/stat",e->d_name);wt_read(path,buf,sizeof buf);
            char *a=strchr(buf,'('),*b=strrchr(buf,')');if(!a||!b||b<a||!b[1]||(b[2]!='R'&&b[2]!='D'))continue;
            unsigned rtp=0,pol=0;int field=2;
            for(char *q=b+1;*q&&field<41;q++)if(*q==' '){field++;if(field==40)rtp=(unsigned)strtoul(q+1,0,10);else if(field==41)pol=(unsigned)strtoul(q+1,0,10);}
            nr++;if(k<sizeof list-60){*b=0;k+=(size_t)snprintf(list+k,sizeof list-k," %s '%.16s' %c pol %u/%u;",e->d_name,a+1,b[2]?b[2]:'?',pol,rtp);}
        }closedir(d);}
        fw_write("  running/waiting %u:%s",nr,list);
        {char path[64],wchan[64],stack[400];
         snprintf(path,sizeof path,"/proc/self/task/%s/wchan",main_tid);wt_read(path,wchan,sizeof wchan);
         snprintf(path,sizeof path,"/proc/self/task/%s/stack",main_tid);wt_read(path,stack,sizeof stack);
         fw_write("  main wchan '%s' stack %s",wchan,stack[0]?stack:"(none)");}
        fw_sync();
    }
    return 0;
}
static void wt_start(void) {
    if(wt_started)return;
    wt_started=1;pthread_t t;pthread_attr_t at;pthread_attr_init(&at);pthread_attr_setdetachstate(&at,PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at,256*1024);
    if(pthread_create(&t,&at,wt_thread,0)!=0)wt_started=0;
    else {pthread_t g;if(pthread_create(&g,&at,fw_guard,0)!=0)lp_log("watch: RT guard thread not started");}
    pthread_attr_destroy(&at);
}
static void wt_tick(void){__atomic_store_n(&wt_tick_ms,wt_mono_ms(),__ATOMIC_RELEASE);}
#endif

#ifndef OQ_PRODJLINK_FSFIX_H
#define OQ_PRODJLINK_FSFIX_H
/* Link media seen through EP136's FUSE mount (/media/playerNN/usb, FuseFilsine over the
 * player's NFS). An XDJ-1000MK2 answers NFS READDIR with broken names (1-char/garbled), so
 * folder listings there are garbage while opening a known path works (export.pdb read in
 * 1.2 s, 2026-11-11). EP136's file calls under /media/player are logged, and (option
 * "fixlist") its folder listings there are rebuilt from the standard rekordbox names that
 * exist. GOT (EP136 1.32): opendir 0x349a348, readdir 0x349a638, readdir64 0x349a7c8,
 * closedir 0x349a250, open 0x349a510, open64 0x349a1b0, fopen 0x349ae88, fopen64 0x349a4d8,
 * __xstat 0x349b998, __xstat64 0x349b670, __lxstat 0x349a940, access 0x349b908. */
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
extern int __xstat(int,const char *,struct stat *);
extern int __xstat64(int,const char *,struct stat *);
extern int __lxstat(int,const char *,struct stat *);
static unsigned fs_installed,fs_fixlist,fs_lines,fs_fixed;
static void *fs_orig[13];
static int fs_link(const char *p){return p&&!strncmp(p,"/media/player",13);}
static void fs_log(const char *what,const char *path,long r,int err) {
    if(fs_lines>=200)return;
    fs_lines++;
    if(!LP_QUIET||r<0)lp_log("ep136 file %s %.150s -> %ld%s%s",what,path,r,r<0?" ":"",r<0?strerror(err):"");
}
/* rebuilt listings: one per DIR handle */
#define FS_DIRS 8
#define FS_NAMES 24
static struct {DIR *d;unsigned n,at;struct dirent ent[FS_NAMES];} fs_dirs[FS_DIRS];
static pthread_mutex_t fs_lock=PTHREAD_MUTEX_INITIALIZER;
static const char *const fs_known[]={"PIONEER","Contents","rekordbox","USBANLZ","export.pdb","exportExt.pdb",
    "exportLibrary.db","DEVSETTING.DAT","MYSETTING.DAT","MYSETTING2.DAT","DJMMYSETTING.DAT","djprofile.nxs",
    "MYTAG","Artwork","share","PIONEER.DAT","exportLibrary.db-wal"};
static DIR *fs_opendir(const char *path) {
    DIR *d=opendir(path);int err=errno;
    if(!fs_link(path))return d;
    fs_log("opendir",path,d?0:-1,err);
    if(!d||!fs_fixlist)return d;
    pthread_mutex_lock(&fs_lock);
    for(unsigned i=0;i<FS_DIRS;i++)if(!fs_dirs[i].d) {
        fs_dirs[i].d=d;fs_dirs[i].n=fs_dirs[i].at=0;
        static const char *const dots[]={".",".."};
        for(unsigned k=0;k<2;k++){struct dirent *e=&fs_dirs[i].ent[fs_dirs[i].n++];memset(e,0,sizeof *e);e->d_type=DT_DIR;snprintf(e->d_name,sizeof e->d_name,"%s",dots[k]);e->d_ino=1+k;e->d_reclen=sizeof *e;}
        for(unsigned k=0;k<sizeof fs_known/sizeof *fs_known&&fs_dirs[i].n<FS_NAMES;k++) {
            char p[400];struct stat st;snprintf(p,sizeof p,"%s/%s",path,fs_known[k]);
            if(stat(p,&st))continue;
            struct dirent *e=&fs_dirs[i].ent[fs_dirs[i].n++];memset(e,0,sizeof *e);
            e->d_type=S_ISDIR(st.st_mode)?DT_DIR:DT_REG;e->d_ino=st.st_ino?st.st_ino:100+k;e->d_reclen=sizeof *e;
            snprintf(e->d_name,sizeof e->d_name,"%s",fs_known[k]);
        }
        fs_fixed++;
        if(fs_lines<200){char names[300]="";size_t q=0;for(unsigned k=2;k<fs_dirs[i].n&&q+40<sizeof names;k++)q+=(size_t)snprintf(names+q,sizeof names-q,"%s ",fs_dirs[i].ent[k].d_name);
            fs_lines++;lp_log("ep136 file listing of %.120s rebuilt: %s",path,names[0]?names:"(nothing known)");}
        break;
    }
    pthread_mutex_unlock(&fs_lock);
    return d;
}
static struct dirent *fs_readdir(DIR *d) {
    pthread_mutex_lock(&fs_lock);
    for(unsigned i=0;i<FS_DIRS;i++)if(fs_dirs[i].d==d&&d) {
        struct dirent *e=fs_dirs[i].at<fs_dirs[i].n?&fs_dirs[i].ent[fs_dirs[i].at++]:0;
        pthread_mutex_unlock(&fs_lock);return e;
    }
    pthread_mutex_unlock(&fs_lock);
    return readdir(d);
}
static int fs_closedir(DIR *d) {
    pthread_mutex_lock(&fs_lock);
    for(unsigned i=0;i<FS_DIRS;i++)if(fs_dirs[i].d==d)fs_dirs[i].d=0;
    pthread_mutex_unlock(&fs_lock);
    return closedir(d);
}
static int fs_open(const char *p,int fl,...) {
    va_list a;va_start(a,fl);int mode=(fl&O_CREAT)?va_arg(a,int):0;va_end(a);
    int r=open(p,fl,mode),err=errno;if(fs_link(p))fs_log("open",p,r,err);errno=err;return r;
}
static FILE *fs_fopen(const char *p,const char *m) {
    FILE *f=fopen(p,m);int err=errno;if(fs_link(p))fs_log("fopen",p,f?0:-1,err);errno=err;return f;
}
static int fs_xstat(int v,const char *p,struct stat *s){int r=__xstat(v,p,s),err=errno;if(fs_link(p))fs_log("stat",p,r,err);errno=err;return r;}
static int fs_xstat64(int v,const char *p,struct stat *s){int r=__xstat64(v,p,s),err=errno;if(fs_link(p))fs_log("stat64",p,r,err);errno=err;return r;}
static int fs_lxstat(int v,const char *p,struct stat *s){int r=__lxstat(v,p,s),err=errno;if(fs_link(p))fs_log("lstat",p,r,err);errno=err;return r;}
static int fs_access(const char *p,int m){int r=access(p,m),err=errno;if(fs_link(p))fs_log("access",p,r,err);errno=err;return r;}
/* Browsing a mounted player's USB: EP136 asks the dbserver on 127.0.0.1 (12523 port query,
 * then the dbserver) - it takes player numbers 1-4 for its own decks. Option "linkdb":
 * while another player's USB is mounted, those two connections go to that player. */
static unsigned fs_linkdb,fs_redirects,fs_conn_lines;
static int fs_connect(int fd,const struct sockaddr *a,socklen_t l) {
    struct sockaddr_in to;
    if(a&&a->sa_family==AF_INET&&l>=sizeof to) {
        memcpy(&to,a,sizeof to);
        uint32_t ip=ntohl(to.sin_addr.s_addr);unsigned port=ntohs(to.sin_port);
        int lo=ip==0x7f000001u,ll=(ip>>16)==0xa9feu;
        if(fs_linkdb&&lo&&(port==12523||port==1051)&&nt_link_ip[0]==0xa9) {
            memcpy(&to.sin_addr.s_addr,nt_link_ip,4);
            fs_redirects++;
            if(fs_conn_lines<60){fs_conn_lines++;lp_log("ep136 connect 127.0.0.1:%u -> %u.%u.%u.%u:%u (redirected, %u)",port,nt_link_ip[0],nt_link_ip[1],nt_link_ip[2],nt_link_ip[3],port,fs_redirects);}
            if(w3_proxy_connect(fd,&to)==1)return 0;   /* 1.69 */
            int r=connect(fd,(struct sockaddr *)&to,sizeof to),err=errno;
            if(fs_conn_lines<60){fs_conn_lines++;lp_log("ep136 connect result %d%s%s",r,r<0?" ":"",r<0?strerror(err):"");}
            errno=err;return r;
        }
        if((lo||ll)&&fs_conn_lines<60){fs_conn_lines++;lp_log("ep136 connect %u.%u.%u.%u:%u",ip>>24,(ip>>16)&255,(ip>>8)&255,ip&255,port);}
        if(ll&&w3_proxy_connect(fd,&to)==1)return 0;   /* 1.69: a player's dbserver, through the mod */
    }
    return connect(fd,a,l);
}
static void fs_install(void) {
    if(fs_installed)return;
    fs_installed=1;
    static const struct {uintptr_t slot;void *fn;} t[]={
        {0x349a348,(void *)fs_opendir},{0x349a638,(void *)fs_readdir},{0x349a7c8,(void *)fs_readdir},{0x349a250,(void *)fs_closedir},
        {0x349a510,(void *)fs_open},{0x349a1b0,(void *)fs_open},{0x349ae88,(void *)fs_fopen},{0x349a4d8,(void *)fs_fopen},
        {0x349b998,(void *)fs_xstat},{0x349b670,(void *)fs_xstat64},{0x349a940,(void *)fs_lxstat},{0x349b908,(void *)fs_access},
        {0x349aef8,(void *)fs_connect}};
    unsigned ok=0;
    for(unsigned i=0;i<sizeof t/sizeof *t;i++)ok+=db_store(t[i].slot,t[i].fn,&fs_orig[i]);
    lp_log("file hooks: %u of %u, rebuilt listings %s",ok,(unsigned)(sizeof t/sizeof *t),fs_fixlist?"on":"off");
}
#endif

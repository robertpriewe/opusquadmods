#ifndef OQ_PRODJLINK_NFS_H
#define OQ_PRODJLINK_NFS_H
/* Read-only NFS for the Pro DJ Link mod: lets another player load tracks from the OPUS's
 * USB, the way CDJs share theirs. The OPUS's own deck software already answers the
 * browse side (dbserver, seen on the link 2026-11-03); its NFS threads never start, so
 * this serves the files: portmap v2 (UDP 111), MOUNT v1 and NFS v2 (RFC 1833 / 1094),
 * UDP only, AUTH accepted as sent.
 *
 *   portmap  NULL, GETPORT (MOUNT v1 and NFS v2 over UDP), DUMP
 *   MOUNT    NULL, MNT ("/C/" = the USB: the first mount that holds
 *            PIONEER/rekordbox/export.pdb; "/" too), UMNT, UMNTALL, EXPORT
 *   NFS      NULL, GETATTR, LOOKUP, READ (up to 8192 bytes), READDIR, STATFS;
 *            anything that writes gets NFSERR_ROFS.
 *
 * File names arrive as UTF-16LE (CDJs) or UTF-8; both are accepted, matched without case
 * when the exact name is not there. Handles are 32 bytes: "OQNF", a table index and a
 * per-start generation; ".." and names with '/' are refused, so nothing outside the USB
 * root is reachable. Everything runs on the link thread (ln_fdset / ln_serve). */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>

#ifndef LN_PORTMAP_PORT
#define LN_PORTMAP_PORT 111
#endif
#ifndef LN_MOUNT_PORT
#define LN_MOUNT_PORT 30635
#endif
#ifndef LN_NFS_PORT
#define LN_NFS_PORT 2049
#endif
enum {LN_PORTMAP=100000,LN_MOUNT=100005,LN_NFS=100003};
enum {LN_OK=0,LN_NOENT=2,LN_IO=5,LN_ACCES=13,LN_NOTDIR=20,LN_ISDIR=21,LN_ROFS=30,LN_NAMETOOLONG=63,LN_STALE=70};
#define LN_HANDLES 4096
#define LN_PATH 512
#define LN_MAX_READ 8192

static char ln_root[256];
static char ln_paths[LN_HANDLES][LN_PATH];   /* relative to ln_root; [0] = "" (the root) */
static unsigned ln_used=1,ln_gen;
static int ln_sock[3]={-1,-1,-1};            /* portmap, mount, nfs */
static unsigned ln_calls[3],ln_errors;
static void (*ln_log)(const char *fmt,...);

/* ---- XDR helpers ---------------------------------------------------------------- */
struct ln_buf {unsigned char *p;size_t n,cap;int bad;};
static uint32_t ln_get(struct ln_buf *b) {
    if(b->n+4>b->cap){b->bad=1;return 0;}
    const unsigned char *q=b->p+b->n;b->n+=4;
    return (uint32_t)q[0]<<24|(uint32_t)q[1]<<16|(uint32_t)q[2]<<8|q[3];
}
static const unsigned char *ln_get_opaque(struct ln_buf *b,uint32_t *len,uint32_t max) {
    *len=ln_get(b);
    if(b->bad||*len>max||b->n+*len>b->cap){b->bad=1;return 0;}
    const unsigned char *q=b->p+b->n;b->n+=(*len+3)&~3u;if(b->n>b->cap)b->n=b->cap;
    return q;
}
static void ln_put(struct ln_buf *b,uint32_t v) {
    if(b->n+4>b->cap){b->bad=1;return;}
    unsigned char *q=b->p+b->n;q[0]=(unsigned char)(v>>24);q[1]=(unsigned char)(v>>16);q[2]=(unsigned char)(v>>8);q[3]=(unsigned char)v;b->n+=4;
}
static void ln_put_bytes(struct ln_buf *b,const void *d,size_t len,int with_len) {
    if(with_len)ln_put(b,(uint32_t)len);
    size_t pad=(4-len%4)%4;
    if(b->n+len+pad>b->cap){b->bad=1;return;}
    memcpy(b->p+b->n,d,len);memset(b->p+b->n+len,0,pad);b->n+=len+pad;
}

/* ---- the USB root ------------------------------------------------------------------ */
static int ln_find_root(void) {
    FILE *f=fopen("/proc/mounts","r");
    if(!f)return ln_root[0]!=0;           /* a test may have set it */
    char dev[128],dir[256],type[32];
    while(fscanf(f,"%127s %255s %31s %*[^\n]",dev,dir,type)==3) {
        if(strncmp(dir,"/media",6)&&strncmp(dir,"/mnt",4))continue;
        char probe[400];snprintf(probe,sizeof probe,"%s/PIONEER/rekordbox/export.pdb",dir);
        struct stat st;
        if(!stat(probe,&st)){snprintf(ln_root,sizeof ln_root,"%s",dir);break;}
    }
    fclose(f);
    return ln_root[0]!=0;
}

/* ---- handles ------------------------------------------------------------------------ */
static void ln_handle(unsigned idx,unsigned char fh[32]) {
    memset(fh,0,32);memcpy(fh,"OQNF",4);
    fh[4]=(unsigned char)(idx>>24);fh[5]=(unsigned char)(idx>>16);fh[6]=(unsigned char)(idx>>8);fh[7]=(unsigned char)idx;
    fh[8]=(unsigned char)(ln_gen>>24);fh[9]=(unsigned char)(ln_gen>>16);fh[10]=(unsigned char)(ln_gen>>8);fh[11]=(unsigned char)ln_gen;
}
static int ln_index(const unsigned char *fh) {
    if(memcmp(fh,"OQNF",4))return -1;
    unsigned idx=(unsigned)fh[4]<<24|(unsigned)fh[5]<<16|(unsigned)fh[6]<<8|fh[7];
    unsigned gen=(unsigned)fh[8]<<24|(unsigned)fh[9]<<16|(unsigned)fh[10]<<8|fh[11];
    return gen==ln_gen&&idx<ln_used?(int)idx:-1;
}
static unsigned ln_intern(const char *rel) {
    for(unsigned i=0;i<ln_used;i++)if(!strcmp(ln_paths[i],rel))return i;
    unsigned i=ln_used<LN_HANDLES?ln_used++:1+(unsigned)(rand()%(LN_HANDLES-1));   /* full: reuse a slot */
    snprintf(ln_paths[i],LN_PATH,"%s",rel);
    return i;
}
static void ln_full(unsigned idx,char *out,size_t cap) {
    snprintf(out,cap,"%s%s%s",ln_root,ln_paths[idx][0]?"/":"",ln_paths[idx]);
}

/* UTF-16LE (as CDJs send names) or UTF-8 -> UTF-8. */
static int ln_decode(const unsigned char *in,uint32_t len,int utf16,char *out,size_t cap);
static int ln_name(const unsigned char *in,uint32_t len,char *out,size_t cap) {
    /* UTF-16LE when the length is even and some ASCII-range unit has a zero high byte;
     * an all-non-ASCII UTF-16 name is retried that way by the caller (ln_name16). */
    int utf16=0;
    if(len>=2&&!(len&1))for(uint32_t i=1;i<len;i+=2)if(!in[i]&&in[i-1]){utf16=1;break;}
    return ln_decode(in,len,utf16,out,cap);
}
static int ln_decode(const unsigned char *in,uint32_t len,int utf16,char *out,size_t cap) {
    size_t k=0;
    if(!utf16) {
        if(len+1>cap)return 0;
        memcpy(out,in,len);out[len]=0;k=len;
        while(k&&!out[k-1])k--;
        out[k]=0;
    } else {
        for(uint32_t i=0;i+1<len;i+=2) {
            uint32_t c=(uint32_t)in[i]|(uint32_t)in[i+1]<<8;
            if(c>=0xd800&&c<0xdc00&&i+3<len) {
                uint32_t lo=(uint32_t)in[i+2]|(uint32_t)in[i+3]<<8;
                if(lo>=0xdc00&&lo<0xe000){c=0x10000+((c-0xd800)<<10)+(lo-0xdc00);i+=2;}
            }
            if(!c)break;
            unsigned char u[4];size_t n;
            if(c<0x80){u[0]=(unsigned char)c;n=1;}
            else if(c<0x800){u[0]=(unsigned char)(0xc0|c>>6);u[1]=(unsigned char)(0x80|(c&63));n=2;}
            else if(c<0x10000){u[0]=(unsigned char)(0xe0|c>>12);u[1]=(unsigned char)(0x80|((c>>6)&63));u[2]=(unsigned char)(0x80|(c&63));n=3;}
            else {u[0]=(unsigned char)(0xf0|c>>18);u[1]=(unsigned char)(0x80|((c>>12)&63));u[2]=(unsigned char)(0x80|((c>>6)&63));u[3]=(unsigned char)(0x80|(c&63));n=4;}
            if(k+n+1>cap)return 0;
            memcpy(out+k,u,n);k+=n;
        }
        out[k]=0;
    }
    if(!k||!strcmp(out,".")||!strcmp(out,"..")||strchr(out,'/'))return 0;
    return 1;
}

/* ---- attributes ------------------------------------------------------------------- */
static void ln_fattr(struct ln_buf *b,const struct stat *st) {
    int dir=S_ISDIR(st->st_mode);
    ln_put(b,dir?2:1);
    ln_put(b,dir?(040000|0555):(0100000|0444));
    ln_put(b,dir?2:1);ln_put(b,0);ln_put(b,0);
    ln_put(b,(uint32_t)(st->st_size>0xffffffffLL?0xffffffffu:(uint32_t)st->st_size));
    ln_put(b,4096);ln_put(b,0);ln_put(b,(uint32_t)((st->st_size+4095)/4096));
    ln_put(b,1);ln_put(b,(uint32_t)st->st_ino);
    for(int i=0;i<3;i++){ln_put(b,(uint32_t)st->st_mtime);ln_put(b,0);}
}
static int ln_errno(int e) {
    return e==ENOENT?LN_NOENT:e==EACCES||e==EPERM?LN_ACCES:e==ENOTDIR?LN_NOTDIR:e==EISDIR?LN_ISDIR:e==ENAMETOOLONG?LN_NAMETOOLONG:LN_IO;
}

/* ---- the three programs --------------------------------------------------------------- */
static int ln_portmap(uint32_t proc,struct ln_buf *in,struct ln_buf *out) {
    if(proc==0)return 0;
    if(proc==3) {
        uint32_t prog=ln_get(in),vers=ln_get(in),prot=ln_get(in);ln_get(in);
        if(in->bad)return 4;
        uint32_t port=prot!=17?0:prog==LN_MOUNT&&vers==1?LN_MOUNT_PORT:prog==LN_NFS&&vers==2?LN_NFS_PORT:prog==LN_PORTMAP&&vers==2?LN_PORTMAP_PORT:0;
        ln_put(out,port);return 0;
    }
    if(proc==4) {   /* DUMP */
        static const uint32_t maps[3][3]={{LN_PORTMAP,2,LN_PORTMAP_PORT},{LN_MOUNT,1,LN_MOUNT_PORT},{LN_NFS,2,LN_NFS_PORT}};
        for(int i=0;i<3;i++){ln_put(out,1);ln_put(out,maps[i][0]);ln_put(out,maps[i][1]);ln_put(out,17);ln_put(out,maps[i][2]);}
        ln_put(out,0);return 0;
    }
    return 3;
}
static int ln_mount(uint32_t proc,struct ln_buf *in,struct ln_buf *out) {
    if(proc==0||proc==3||proc==4) {   /* NULL, UMNT, UMNTALL */
        if(proc==3){uint32_t n;ln_get_opaque(in,&n,1024);}
        return 0;
    }
    if(proc==1) {
        uint32_t n;const unsigned char *path=ln_get_opaque(in,&n,1024);if(in->bad)return 4;
        char p[64];snprintf(p,sizeof p,"%.*s",(int)(n<63?n:63),(const char *)path);
        int usb=!strcmp(p,"/C/")||!strcmp(p,"/C")||!strcmp(p,"/");
        if(!usb||(!ln_root[0]&&!ln_find_root())) {
            ln_put(out,usb?LN_NOENT:LN_ACCES);
            if(ln_log)ln_log("nfs: MOUNT %s refused%s",p,usb?" (no USB with a rekordbox export)":"");
            return 0;
        }
        unsigned char fh[32];ln_handle(0,fh);ln_put(out,0);ln_put_bytes(out,fh,32,0);
        if(ln_log)ln_log("nfs: MOUNT %s -> %s",p,ln_root);
        return 0;
    }
    if(proc==5){ln_put(out,1);ln_put_bytes(out,"/C/",3,1);ln_put(out,0);ln_put(out,0);return 0;}   /* EXPORT: /C/, no groups */
    return 3;
}
static int ln_stat_handle(const unsigned char *fh,char *full,size_t cap,struct stat *st) {
    int idx=ln_index(fh);if(idx<0)return LN_STALE;
    ln_full((unsigned)idx,full,cap);
    return stat(full,st)?ln_errno(errno):LN_OK;
}
static int ln_nfs(uint32_t proc,struct ln_buf *in,struct ln_buf *out) {
    unsigned char fh[32];char full[LN_PATH+300];struct stat st;
    if(proc==0)return 0;
    if(proc==2||proc==5||(proc>=8&&proc<=15)){ln_put(out,LN_ROFS);return 0;}   /* SETATTR WRITECACHE WRITE CREATE REMOVE RENAME LINK SYMLINK MKDIR RMDIR */
    if(in->n+32>in->cap)return 4;
    memcpy(fh,in->p+in->n,32);in->n+=32;
    if(proc==1) {   /* GETATTR */
        int e=ln_stat_handle(fh,full,sizeof full,&st);ln_put(out,(uint32_t)e);if(!e)ln_fattr(out,&st);return 0;
    }
    if(proc==4) {   /* LOOKUP */
        uint32_t n;const unsigned char *raw=ln_get_opaque(in,&n,1024);if(in->bad)return 4;
        int idx=ln_index(fh);char name[400];
        if(idx<0){ln_put(out,LN_STALE);return 0;}
        if(!ln_name(raw,n,name,sizeof name)){ln_put(out,LN_NOENT);return 0;}
        char rel[LN_PATH];
        if(snprintf(rel,sizeof rel,"%s%s%s",ln_paths[idx],ln_paths[idx][0]?"/":"",name)>=(int)sizeof rel){ln_put(out,LN_NAMETOOLONG);return 0;}
        ln_full((unsigned)idx,full,sizeof full);
        char want[LN_PATH+300+410];snprintf(want,sizeof want,"%s/%s",full,name);
        if(stat(want,&st)) {   /* not there as sent: match the name without case */
            DIR *d=opendir(full);struct dirent *e;int found=0;
            while(d&&(e=readdir(d)))if(!strcasecmp(e->d_name,name)){
                snprintf(rel,sizeof rel,"%s%s%s",ln_paths[idx],ln_paths[idx][0]?"/":"",e->d_name);
                snprintf(want,sizeof want,"%s/%s",full,e->d_name);found=!stat(want,&st);break;}
            if(d)closedir(d);
            char alt[400];
            if(!found&&!(n&1)&&ln_decode(raw,n,1,alt,sizeof alt)&&strcmp(alt,name)) {   /* UTF-16 without ASCII */
                snprintf(want,sizeof want,"%s/%s",full,alt);
                if(!stat(want,&st)){snprintf(rel,sizeof rel,"%s%s%s",ln_paths[idx],ln_paths[idx][0]?"/":"",alt);found=1;}
            }
            if(!found){ln_put(out,LN_NOENT);if(ln_log)ln_log("nfs: LOOKUP %s: not found",name);return 0;}
        }
        unsigned child=ln_intern(rel);unsigned char cfh[32];ln_handle(child,cfh);
        ln_put(out,0);ln_put_bytes(out,cfh,32,0);ln_fattr(out,&st);return 0;
    }
    if(proc==6) {   /* READ */
        uint32_t off=ln_get(in),count=ln_get(in);ln_get(in);if(in->bad)return 4;
        int e=ln_stat_handle(fh,full,sizeof full,&st);
        if(!e&&S_ISDIR(st.st_mode))e=LN_ISDIR;
        if(e){ln_put(out,(uint32_t)e);return 0;}
        if(count>LN_MAX_READ)count=LN_MAX_READ;
        static unsigned char data[LN_MAX_READ];ssize_t got=0;
        int fd=open(full,O_RDONLY|O_CLOEXEC);
        if(fd<0){ln_put(out,(uint32_t)ln_errno(errno));return 0;}
        got=pread(fd,data,count,(off_t)off);close(fd);
        if(got<0){ln_put(out,LN_IO);return 0;}
        ln_put(out,0);ln_fattr(out,&st);ln_put_bytes(out,data,(size_t)got,1);return 0;
    }
    if(proc==16) {  /* READDIR: cookie = position in the directory */
        uint32_t cookie=ln_get(in),count=ln_get(in);if(in->bad)return 4;
        int idx=ln_index(fh);if(idx<0){ln_put(out,LN_STALE);return 0;}
        ln_full((unsigned)idx,full,sizeof full);
        DIR *d=opendir(full);if(!d){ln_put(out,(uint32_t)ln_errno(errno));return 0;}
        ln_put(out,0);
        if(count>LN_MAX_READ)count=LN_MAX_READ;
        size_t limit=out->n+count>out->cap?out->cap:out->n+count;
        struct dirent *e;uint32_t pos=0;int eof=1;
        while((e=readdir(d))) {
            pos++;if(pos<=cookie)continue;
            size_t len=strlen(e->d_name),need=4+4+4+((len+3)&~3u)+4+8;
            if(out->n+need>limit){eof=0;break;}
            ln_put(out,1);ln_put(out,(uint32_t)e->d_ino);ln_put_bytes(out,e->d_name,len,1);ln_put(out,pos);
        }
        closedir(d);ln_put(out,0);ln_put(out,(uint32_t)eof);return 0;
    }
    if(proc==17){ln_put(out,0);ln_put(out,LN_MAX_READ);ln_put(out,4096);ln_put(out,1u<<20);ln_put(out,0);ln_put(out,0);return 0;}   /* STATFS */
    return 3;
}

/* One RPC call on socket k (0 portmap, 1 mount, 2 nfs): parse, answer, reply. */
static void ln_one(int k) {
    static unsigned char req[2048],rep[LN_MAX_READ+512];
    struct sockaddr_in from;socklen_t fl=sizeof from;
    ssize_t n=recvfrom(ln_sock[k],req,sizeof req,0,(struct sockaddr *)&from,&fl);
    if(n<24)return;
    struct ln_buf in={req,0,(size_t)n,0},out={rep,0,sizeof rep,0};
    uint32_t xid=ln_get(&in),type=ln_get(&in);
    if(type!=0)return;
    uint32_t rpcvers=ln_get(&in),prog=ln_get(&in),vers=ln_get(&in),proc=ln_get(&in);
    uint32_t len;ln_get(&in);ln_get_opaque(&in,&len,400);ln_get(&in);ln_get_opaque(&in,&len,400);   /* cred, verf */
    ln_put(&out,xid);ln_put(&out,1);
    if(in.bad||rpcvers!=2){ln_put(&out,1);ln_put(&out,0);ln_put(&out,2);ln_put(&out,2);goto send;}   /* MSG_DENIED RPC_MISMATCH 2..2 */
    ln_put(&out,0);ln_put(&out,0);ln_put(&out,0);   /* accepted, AUTH_NULL verifier */
    static const uint32_t progs[3]={LN_PORTMAP,LN_MOUNT,LN_NFS},versions[3]={2,1,2};
    if(prog!=progs[k]){ln_put(&out,1);goto send;}   /* PROG_UNAVAIL */
    if(vers!=versions[k]){ln_put(&out,2);ln_put(&out,versions[k]);ln_put(&out,versions[k]);goto send;}
    size_t status_at=out.n;ln_put(&out,0);
    int r=k==0?ln_portmap(proc,&in,&out):k==1?ln_mount(proc,&in,&out):ln_nfs(proc,&in,&out);
    if(r){out.n=status_at;ln_put(&out,(uint32_t)r);}   /* PROC_UNAVAIL / GARBAGE_ARGS */
    ln_calls[k]++;
send:
    if(out.bad){ln_errors++;return;}
    sendto(ln_sock[k],rep,out.n,0,(struct sockaddr *)&from,fl);
}

static int ln_udp(unsigned port) {
    int s=socket(AF_INET,SOCK_DGRAM,0),one=1;if(s<0)return -1;
    fcntl(s,F_SETFD,FD_CLOEXEC);
    setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);
    struct sockaddr_in a;memset(&a,0,sizeof a);a.sin_family=AF_INET;a.sin_port=htons((uint16_t)port);
    if(bind(s,(struct sockaddr *)&a,sizeof a)){
        if(ln_log)ln_log("nfs: UDP %u not available (%s)",port,strerror(errno));
        close(s);return -1;
    }
    return s;
}
static void ln_open(void) {
    static const unsigned ports[3]={LN_PORTMAP_PORT,LN_MOUNT_PORT,LN_NFS_PORT};
    if(!ln_gen){struct timespec t;clock_gettime(CLOCK_REALTIME,&t);ln_gen=(unsigned)(t.tv_sec^t.tv_nsec)|1u;}
    for(int k=0;k<3;k++)if(ln_sock[k]<0) {
        ln_sock[k]=ln_udp(ports[k]);
        if(ln_sock[k]>=0&&ln_log)ln_log("nfs: listening on UDP %u (%s)",ports[k],k==0?"portmap":k==1?"MOUNT":"NFS");
    }
    if(!ln_root[0]&&ln_find_root()&&ln_log)ln_log("nfs: serving %s as /C/",ln_root);
}
static void ln_close(void) {
    for(int k=0;k<3;k++)if(ln_sock[k]>=0){close(ln_sock[k]);ln_sock[k]=-1;}
    ln_root[0]=0;ln_used=1;
}
static void ln_fdset(fd_set *set,int *top) {
    for(int k=0;k<3;k++)if(ln_sock[k]>=0){FD_SET(ln_sock[k],set);if(ln_sock[k]>*top)*top=ln_sock[k];}
}
static void ln_serve(fd_set *set) {
    for(int k=0;k<3;k++)if(ln_sock[k]>=0&&FD_ISSET(ln_sock[k],set))ln_one(k);
}
#endif

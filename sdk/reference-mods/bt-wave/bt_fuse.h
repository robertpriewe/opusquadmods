#ifndef OQ_BT_FUSE_H
#define OQ_BT_FUSE_H
/* Live Bluetooth track over FUSE (R105). R104 on hardware: EP136 plays a growing WAV only
 * up to the size it had at load, and caches that length. So the deck gets a file that is
 * long from the start and whose reads wait for the audio, as a stream still arriving:
 *
 *   <USB>/OQ_BTLIVE/          a FUSE mount of ours (the kernel's /dev/fuse protocol,
 *                             no libfuse), read-only, one file:
 *     LIVE-<n>.WAV            44-byte header claiming BF_CLAIM_SECONDS, then the audio
 *                             from the backing file; n is new for every recording, so
 *                             EP136's track cache never sees an old length
 *   <USB>/OQ_BTLIVE_DATA/live.raw   the backing PCM, written by bt_live.h
 *
 * A read past what has been recorded waits (up to BF_WAIT_MS) for the audio to arrive,
 * (only EP136's player, BF_WAITER) then returns silence for what is still missing, so a reader is paced by the incoming
 * audio and nothing ever hangs. One thread serves everything with poll(): waiting reads
 * are parked and answered as audio comes in. Each reading thread's name is noted
 * (bf_readers) to learn which of EP136's threads play and which analyse.
 * Protocol: FUSE 7.12 (fuse_kernel.h), direct I/O so every read reaches us. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BF_CLAIM_SECONDS 3600u   /* the default; bf_claim_seconds is what is used */
#ifndef BF_WAIT_MS
#define BF_WAIT_MS 10000u
#endif
#define BF_PENDING 16
#define BF_READERS 8
enum {BF_LOOKUP=1,BF_FORGET=2,BF_GETATTR=3,BF_OPEN=14,BF_READ=15,BF_STATFS=17,BF_RELEASE=18,BF_FLUSH=25,
      BF_INIT=26,BF_OPENDIR=27,BF_READDIR=28,BF_RELEASEDIR=29,BF_ACCESS=34,BF_INTERRUPT=36,BF_DESTROY=38,BF_BATCH_FORGET=42};
struct bf_in {uint32_t len,opcode;uint64_t unique,nodeid;uint32_t uid,gid,pid,padding;};
struct bf_out {uint32_t len;int32_t error;uint64_t unique;};
struct bf_attr {uint64_t ino,size,blocks,atime,mtime,ctime;uint32_t atimensec,mtimensec,ctimensec,mode,nlink,uid,gid,rdev,blksize,padding;};
struct bf_entry_out {uint64_t nodeid,generation,entry_valid,attr_valid;uint32_t entry_valid_nsec,attr_valid_nsec;struct bf_attr attr;};
struct bf_attr_out {uint64_t attr_valid;uint32_t attr_valid_nsec,dummy;struct bf_attr attr;};
struct bf_open_out {uint64_t fh;uint32_t open_flags,padding;};
struct bf_read_in {uint64_t fh,offset;uint32_t size,read_flags;};
struct bf_init_out {uint32_t major,minor,max_readahead,flags;uint16_t max_background,congestion_threshold;uint32_t max_write;};
struct bf_pending {uint64_t unique,offset,since_ms;uint32_t size,pid;int used;};

/* How long the live track says it is. Round 3 of the analysis trace: EP136 publishes the
 * waveform of a track in steps of about a minute when the track claims an hour, so the
 * claim is an option (bt-wave choice "length") to see whether a shorter one gives finer steps. */
static unsigned bf_claim_seconds=BF_CLAIM_SECONDS;
static int bf_fd=-1;
static char bf_mount[300],bf_backing[330],bf_name[32];
static unsigned bf_number,bf_rate=44100,bf_mounted,bf_errors,bf_reads,bf_waited,bf_silence_bytes;
static uint64_t bf_recorded;                 /* bytes of PCM in the backing file, from bt_live.h */
static struct bf_pending bf_wait[BF_PENDING];
static struct {uint32_t pid;char comm[20];unsigned reads;uint64_t furthest;} bf_readers[BF_READERS];

static uint64_t bf_ms(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;}
static uint64_t bf_file_size(void) {return 44u+(uint64_t)bf_claim_seconds*bf_rate*4u;}
static void bf_header(unsigned char h[44]) {
    uint32_t data=bf_claim_seconds*bf_rate*4u,riff=data+36,rate=bf_rate,byte_rate=bf_rate*4,fmt=16;uint16_t pcm=1,ch=2,align=4,bits=16;
    memcpy(h,"RIFF",4);memcpy(h+4,&riff,4);memcpy(h+8,"WAVEfmt ",8);memcpy(h+16,&fmt,4);memcpy(h+20,&pcm,2);
    memcpy(h+22,&ch,2);memcpy(h+24,&rate,4);memcpy(h+28,&byte_rate,4);memcpy(h+32,&align,2);memcpy(h+34,&bits,2);
    memcpy(h+36,"data",4);memcpy(h+40,&data,4);
}
static void bf_attr(struct bf_attr *a,int dir) {
    memset(a,0,sizeof *a);
    a->ino=dir?1:2;a->mode=dir?(S_IFDIR|0555):(S_IFREG|0444);a->nlink=dir?2:1;a->blksize=4096;
    a->size=dir?0:bf_file_size();a->blocks=(a->size+511)/512;
    a->atime=a->mtime=a->ctime=(uint64_t)time(0);
}
static void bf_reply(uint64_t unique,int error,const void *body,size_t n) {
    unsigned char buf[sizeof(struct bf_out)+256];
    struct bf_out o={(uint32_t)(sizeof o+(error?0:n)),error?-error:0,unique};
    if(error||!body)n=0;
    if(n>sizeof buf-sizeof o)return;
    memcpy(buf,&o,sizeof o);if(n)memcpy(buf+sizeof o,body,n);
    if(write(bf_fd,buf,sizeof o+n)<0)bf_errors++;
}
static int bf_note_reader(uint32_t pid,uint64_t end) {
    int slot=-1;
    for(int i=0;i<BF_READERS;i++){if(bf_readers[i].pid==pid){slot=i;break;}if(slot<0&&!bf_readers[i].pid)slot=i;}
    if(slot<0)return -1;
    if(bf_readers[slot].pid!=pid) {
        bf_readers[slot].pid=pid;char p[64];snprintf(p,sizeof p,"/proc/self/task/%u/comm",pid);
        FILE *f=fopen(p,"r");if(!f){snprintf(p,sizeof p,"/proc/%u/comm",pid);f=fopen(p,"r");}
        if(f){if(fgets(bf_readers[slot].comm,sizeof bf_readers[slot].comm,f))bf_readers[slot].comm[strcspn(bf_readers[slot].comm,"\n")]=0;fclose(f);}
    }
    bf_readers[slot].reads++;if(end>bf_readers[slot].furthest)bf_readers[slot].furthest=end;
    return slot;
}
/* R105 on hardware: EP136's player reads the track in its FileDataCache thread and follows the
 * live edge; the USB library service (MscLibSrvUsb1) reads up to the end at load, and making it
 * wait made loading take minutes. Only the player waits for audio; others get an answer at once. */
#ifndef BF_WAITER
#define BF_WAITER "FileDataCache"
#endif

/* Answers a read if the audio is there, or once it has waited long enough; 0: keep it parked. */
/* Round 8: quick reads at the live edge. A read that runs past the recording is answered at
 * once with the audio that is there (a short read, whole frames) instead of waiting for all
 * of it. Round 8 on hardware: every read of the track's analysis is a request queued to the
 * one FileDataCache worker (RawFileDataCache 0xd2c060 waits on its future), and that worker
 * sits parked at the live edge, so the analysis crawls (0.56x real time, idle CPU). On by
 * default (round 8's tick on the option did not register). */
static unsigned bf_short_reads,bf_shorted;
static int bf_try_read(uint64_t unique,uint64_t offset,uint32_t size,uint64_t since,int force) {
    uint64_t total=bf_file_size(),recorded=__atomic_load_n(&bf_recorded,__ATOMIC_ACQUIRE);
    if(offset>=total){bf_reply(unique,0,"",0);return 1;}
    if(size>131072)size=131072;
    if(offset+size>total)size=(uint32_t)(total-offset);
    uint64_t have=44+recorded;   /* bytes of the file that exist */
    int waiting=offset+size>have&&!force&&bf_ms()-since<BF_WAIT_MS;
    if(waiting&&__atomic_load_n(&bf_short_reads,__ATOMIC_RELAXED)&&have>offset&&have-offset>=4) {
        size=(uint32_t)((have-offset)&~(uint64_t)3);waiting=0;bf_shorted++;   /* whole stereo frames */
    }
    if(waiting)return 0;
    static unsigned char buf[sizeof(struct bf_out)+131072];
    unsigned char *data=buf+sizeof(struct bf_out);memset(data,0,size);
    uint32_t done=0;
    if(offset<44){unsigned char h[44];bf_header(h);done=(uint32_t)(44-offset<size?44-offset:size);memcpy(data,h+offset,done);}
    if(done<size&&offset+done<have) {
        uint64_t from=offset+done-44,n=have-(offset+done);if(n>size-done)n=size-done;
        int fd=open(bf_backing,O_RDONLY|O_CLOEXEC);
        if(fd>=0){ssize_t got=pread(fd,data+done,(size_t)n,(off_t)from);close(fd);if(got>0)done+=(uint32_t)got;}
    }
    if(done<size)bf_silence_bytes+=size-done;   /* not recorded yet after the wait: silence */
    struct bf_out o={(uint32_t)(sizeof o+size),0,unique};memcpy(buf,&o,sizeof o);
    if(write(bf_fd,buf,sizeof o+size)<0)bf_errors++;
    return 1;
}
static void bf_request(const unsigned char *req,size_t n) {
    if(n<sizeof(struct bf_in))return;
    struct bf_in in;memcpy(&in,req,sizeof in);
    const unsigned char *arg=req+sizeof in;size_t an=n-sizeof in;
    switch(in.opcode) {
    case BF_INIT: {struct bf_init_out o={7,12,131072,0,4,3,131072};bf_reply(in.unique,0,&o,sizeof o);break;}
    case BF_LOOKUP: {
        if(in.nodeid!=1||an!=strlen(bf_name)+1||memcmp(arg,bf_name,an)){bf_reply(in.unique,ENOENT,0,0);break;}
        struct bf_entry_out e;memset(&e,0,sizeof e);e.nodeid=2;e.entry_valid=1;e.attr_valid=1;bf_attr(&e.attr,0);
        bf_reply(in.unique,0,&e,sizeof e);break;
    }
    case BF_GETATTR: {
        if(in.nodeid!=1&&in.nodeid!=2){bf_reply(in.unique,ENOENT,0,0);break;}
        struct bf_attr_out o;memset(&o,0,sizeof o);o.attr_valid=1;bf_attr(&o.attr,in.nodeid==1);bf_reply(in.unique,0,&o,sizeof o);break;
    }
    case BF_OPEN: {
        if(in.nodeid!=2){bf_reply(in.unique,EISDIR,0,0);break;}
        uint32_t flags=0;if(an>=4)memcpy(&flags,arg,4);
        if(flags&3){bf_reply(in.unique,EROFS,0,0);break;}
        struct bf_open_out o={2,1,0};bf_reply(in.unique,0,&o,sizeof o);break;   /* FOPEN_DIRECT_IO */
    }
    case BF_OPENDIR: {struct bf_open_out o={1,0,0};bf_reply(in.unique,0,&o,sizeof o);break;}
    case BF_READ: {
        if(an<sizeof(struct bf_read_in)||in.nodeid!=2){bf_reply(in.unique,EIO,0,0);break;}
        struct bf_read_in r;memcpy(&r,arg,sizeof r);bf_reads++;
        int who=bf_note_reader(in.pid,r.offset+r.size);
        uint64_t now=bf_ms();
        int waits=who>=0&&!strcmp(bf_readers[who].comm,BF_WAITER);
        if(bf_try_read(in.unique,r.offset,r.size,now,!waits))break;
        int parked=0;
        for(int i=0;i<BF_PENDING;i++)if(!bf_wait[i].used){bf_wait[i]=(struct bf_pending){in.unique,r.offset,now,r.size,in.pid,1};parked=1;bf_waited++;break;}
        if(!parked)bf_try_read(in.unique,r.offset,r.size,now,1);   /* too many waiting: answer now */
        break;
    }
    case BF_READDIR: {
        uint64_t off=0;if(an>=16)memcpy(&off,arg+8,8);
        unsigned char out[256];size_t k=0;
        const char *names[3]={".","..",bf_name};uint64_t inos[3]={1,1,2};uint32_t types[3]={4,4,8};
        for(unsigned i=(unsigned)off;i<3;i++) {
            size_t len=strlen(names[i]),rec=(24+len+7)&~(size_t)7;
            if(k+rec>sizeof out)break;
            memset(out+k,0,rec);uint64_t next=i+1;uint32_t nl=(uint32_t)len;
            memcpy(out+k,&inos[i],8);memcpy(out+k+8,&next,8);memcpy(out+k+16,&nl,4);memcpy(out+k+20,&types[i],4);memcpy(out+k+24,names[i],len);
            k+=rec;
        }
        bf_reply(in.unique,0,out,k);break;
    }
    case BF_STATFS: {unsigned char o[80];memset(o,0,sizeof o);uint64_t blocks=1;memcpy(o,&blocks,8);uint32_t bsize=4096,nl=255;memcpy(o+40,&bsize,4);memcpy(o+44,&nl,4);bf_reply(in.unique,0,o,sizeof o);break;}
    case BF_RELEASE:case BF_RELEASEDIR:case BF_FLUSH: bf_reply(in.unique,0,0,0);break;
    case BF_ACCESS: bf_reply(in.unique,0,0,0);break;
    case BF_FORGET:case BF_BATCH_FORGET:case BF_INTERRUPT:case BF_DESTROY: break;   /* no reply */
    default: bf_reply(in.unique,ENOSYS,0,0);break;
    }
}
/* Mount <mount> (an existing directory). Returns 1 when mounted. */
static int bf_start(const char *mount_dir,const char *backing,unsigned number,unsigned rate) {
    snprintf(bf_mount,sizeof bf_mount,"%s",mount_dir);snprintf(bf_backing,sizeof bf_backing,"%s",backing);
    bf_number=number;bf_rate=rate>=8000&&rate<=96000?rate:44100;snprintf(bf_name,sizeof bf_name,"LIVE-%u.WAV",number);
    __atomic_store_n(&bf_recorded,0,__ATOMIC_RELEASE);memset(bf_wait,0,sizeof bf_wait);memset(bf_readers,0,sizeof bf_readers);
    bf_fd=open("/dev/fuse",O_RDWR|O_CLOEXEC);
    if(bf_fd<0){bf_errors++;return 0;}
    char opts[160];snprintf(opts,sizeof opts,"fd=%d,rootmode=40000,user_id=0,group_id=0,allow_other,max_read=131072",bf_fd);
    if(mount("oqlive",bf_mount,"fuse",MS_NOSUID|MS_NODEV|MS_RDONLY,opts)){close(bf_fd);bf_fd=-1;bf_errors++;return 0;}
    bf_mounted=1;return 1;
}
/* One pass of the server: requests for up to wait_ms, then parked reads that can go now. */
static void bf_serve(int wait_ms) {
    if(bf_fd<0)return;
    static unsigned char req[135168];
    struct pollfd p={bf_fd,POLLIN,0};
    if(poll(&p,1,wait_ms)>0&&(p.revents&POLLIN)) {
        ssize_t n=read(bf_fd,req,sizeof req);
        if(n>0)bf_request(req,(size_t)n);
        else if(n<0&&errno==ENODEV){bf_mounted=0;}
    }
    for(int i=0;i<BF_PENDING;i++)if(bf_wait[i].used&&bf_try_read(bf_wait[i].unique,bf_wait[i].offset,bf_wait[i].size,bf_wait[i].since_ms,0))bf_wait[i].used=0;
}
static void bf_stop(void) {
    if(bf_fd<0)return;
    for(int i=0;i<BF_PENDING;i++)if(bf_wait[i].used){bf_try_read(bf_wait[i].unique,bf_wait[i].offset,bf_wait[i].size,0,1);bf_wait[i].used=0;}
    umount2(bf_mount,MNT_DETACH);close(bf_fd);bf_fd=-1;bf_mounted=0;
}
#endif

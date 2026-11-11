#ifndef OQ_BT_DIRECT_H
#define OQ_BT_DIRECT_H
/* Direct reads of the live track (round 10). EP136 reads a track's bytes through
 * input_stream::CacheFacade::read(const juce::String &path, int64 pos,
 * const meow::RawArrayRef<uint8_t,int> &buf) -> bytes read (vtable 0x24428c8 slot 7,
 * fn 0xd1ef60; called at 0xd3c778, which wants exactly buf.size bytes back). Each read waits
 * on the cache worker (futures, 5 s / 10 s timeouts), and on the live track that holds the
 * analysis at 0.56x real time with an idle CPU (rounds 7-9 on hardware).
 * The hook answers a read of the current OQ_BTLIVE/LIVE-<n>.WAV that lies wholly inside what
 * has been recorded straight from live.raw (the WAV header is made here), the same bytes the
 * file gives. Anything else, or any doubt, goes to the original. */
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BD_SLOT (0x24428c8u+8u*7u)
#define BD_FN 0xd1ef60u
struct bd_buf {uint8_t *data;int32_t size;};
static int (*bd_orig)(void *,const void *,int64_t,const struct bd_buf *);
static unsigned bd_installed,bd_direct,bd_passed;
static pthread_mutex_t bd_lock=PTHREAD_MUTEX_INITIALIZER;
static int bd_fd=-1;static unsigned bd_fd_number;

/* "<...>/OQ_BTLIVE/LIVE-<n>.WAV" -> n, else 0. path is a juce::String: its first word is the
 * UTF-8 text. */
static unsigned bd_live_number(const void *jstr) {
    if(!jstr)return 0;
    const char *text=*(const char *const *)jstr;
    if(!text)return 0;
    const char *p=strstr(text,"OQ_BTLIVE/LIVE-");if(!p)return 0;
    p+=15;char *end;unsigned long n=strtoul(p,&end,10);
    return end!=p&&(!strcmp(end,".WAV")||!strcmp(end,".wav"))&&n<100000000ul?(unsigned)n:0;
}
/* bytes [pos, pos+n) of the WAV into out; 1 if all of it was there */
static int bd_fill(unsigned number,int64_t pos,uint8_t *out,uint32_t n) {
    uint64_t recorded=__atomic_load_n(&bf_recorded,__ATOMIC_ACQUIRE);
    if(pos<0||(uint64_t)pos+n>44u+recorded||(uint64_t)pos+n>bf_file_size())return 0;
    uint32_t done=0;
    if(pos<44){unsigned char h[44];bf_header(h);done=(uint32_t)(44-pos<n?44-pos:n);memcpy(out,h+pos,done);}
    if(done==n)return 1;
    int ok=0;
    pthread_mutex_lock(&bd_lock);
    if(bd_fd<0||bd_fd_number!=number) {
        if(bd_fd>=0)close(bd_fd);
        bd_fd=open(bf_backing,O_RDONLY|O_CLOEXEC);bd_fd_number=number;
    }
    int fd=bd_fd;
    pthread_mutex_unlock(&bd_lock);
    if(fd>=0) {
        uint64_t from=(uint64_t)pos+done-44;
        while(done<n) {
            ssize_t got=pread(fd,out+done,n-done,(off_t)from);
            if(got<=0)break;
            done+=(uint32_t)got;from+=(uint64_t)got;
        }
        ok=done==n;
    }
    return ok;
}
static int bd_read(void *self,const void *path,int64_t pos,const struct bd_buf *buf) {
    if(__atomic_load_n(&bf_mounted,__ATOMIC_ACQUIRE)&&buf&&buf->data&&buf->size>0) {
        unsigned n=bd_live_number(path);
        if(n&&n==__atomic_load_n(&bf_number,__ATOMIC_ACQUIRE)) {
            if(bd_fill(n,pos,buf->data,(uint32_t)buf->size)){__atomic_fetch_add(&bd_direct,1,__ATOMIC_RELAXED);return buf->size;}
            __atomic_fetch_add(&bd_passed,1,__ATOMIC_RELAXED);   /* not recorded yet: the normal way */
        }
    }
    return bd_orig(self,path,pos,buf);
}
#endif

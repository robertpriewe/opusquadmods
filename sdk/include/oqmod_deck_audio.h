#ifndef OQMOD_DECK_AUDIO_H
#define OQMOD_DECK_AUDIO_H
/* Deck audio insert (SDK 1.11): change the four decks' audio before the mixer.
 *
 * WHAT IT IS. EP136 sends the decks' audio to the mixer hardware as one interleaved ALSA
 * stream (10 channels S32_LE at 44.1 kHz on R120-R125; one stereo pair per deck). This
 * header hooks that stream (snd_pcm_writei) and hands your callback each deck as float
 * left/right arrays (-1.0 .. 1.0) that you may change in place. What you write is what
 * the mixer receives.
 *
 * WHAT IT IS NOT. Not the master. The stream is the mixer's DECK inputs: everything the
 * mixer does comes AFTER it and is not in it: channel EQ / trim / fader, crossfader, Beat FX,
 * LINE / PHONO / MIC, master, booth and zone. You cannot reach those from a mod. Processing
 * all four decks alike sounds like a "master" treatment for deck music only, and master and
 * zone always get the same deck audio.
 *
 * DECKS. Pair 0 = deck 1 (mixer channel 1) is confirmed on hardware (Mobius 0.3). Pairs 1-3 =
 * decks 2-4 is the natural order but not yet confirmed. Channels past the 8th are passed on
 * untouched. A deck whose mixer channel is set to another input is not heard, whatever you do.
 *
 * SHARING. Every mod that uses this header (and Stems, Mobius, Mono Master) chains its own
 * hook onto the same slot: each receives the previous one's output. The firmware never
 * unloads mods, so hooks are never removed: switch processing with oq_deck_audio_enable().
 *
 * RULES for the callback (EP136's audio thread, about 700 calls a second, usually under
 * 100 frames each): no allocation, locks, file / network I/O, logging or SDK calls; keep it
 * well under 1 ms. Check oq_deck_audio_status() for the cost (max_ns).
 *
 * Usage:
 *   #include "oqmod_deck_audio.h"
 *   static void process(struct oq_deck_audio_block *b, void *user) {
 *       for (unsigned d = 0; d < 4; d++) for (unsigned i = 0; i < b->frames; i++) {
 *           b->left[d][i] *= 0.5f; b->right[d][i] *= 0.5f; }
 *   }
 *   oqmod_init:  oq_deck_audio_install(process, 0);   (once)
 *   enabled(on): oq_deck_audio_enable(on);
 *
 * EP136 1.32 (firmware R120-R125): the call is at 0x1e6a544 through GOT slot 0x349a130.
 * install() refuses (OQ_DECK_AUDIO_UNSUPPORTED) when that slot does not hold libasound's
 * snd_pcm_writei or another mod's hook. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1   /* dladdr; include this header first, or define _GNU_SOURCE yourself */
#endif
#include <sys/uio.h>
#include <sys/syscall.h>
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#ifndef OQ_DECK_AUDIO_SLOT
#define OQ_DECK_AUDIO_SLOT 0x349a130ull     /* EP136 1.32 GOT: snd_pcm_writei */
#endif
#define OQ_DECK_AUDIO_MAX_FRAMES 1024u
#define OQ_DECK_AUDIO_DECKS 4u

enum {
    OQ_DECK_AUDIO_OK = 0,
    OQ_DECK_AUDIO_UNSUPPORTED = -1,   /* not EP136 1.32 / slot holds unknown code */
    OQ_DECK_AUDIO_INVALID = -2,
    OQ_DECK_AUDIO_BUSY = -3,          /* already installed by this mod */
    OQ_DECK_AUDIO_FAILED = -4         /* GOT page not writable */
};
/* Valid only during the callback. left/right[d] hold `frames` floats each, -1.0 .. 1.0;
 * change them in place. Results are clipped to the stream's range. All 8 deck channels go
 * through float (24-bit precision, about -144 dB: inaudible) whether you change them or not;
 * channels past the 8th stay bit-exact. */
struct oq_deck_audio_block {
    uint32_t frames, sample_rate, channels;   /* channels of the whole stream (10) */
    float *left[OQ_DECK_AUDIO_DECKS], *right[OQ_DECK_AUDIO_DECKS];
};
typedef void (*oq_deck_audio_fn)(struct oq_deck_audio_block *, void *user);
struct oq_deck_audio_status {
    uint32_t installed, enabled;
    uint32_t calls;          /* writes seen on all streams */
    uint32_t processed;      /* deck stream writes given to the callback */
    uint32_t skipped;        /* deck stream writes passed on untouched (too long) */
    uint32_t max_ns;         /* slowest callback incl. conversion */
    uint32_t channels, format, sample_rate;   /* deck stream; format: ALSA number (10 = S32_LE) */
    char previous[64];       /* what the slot held: libasound or another mod */
};

/* ---- implementation (one private copy per mod) ---- */
struct oq__da_fmt {void *pcm;int channels,format;unsigned rate,ok;};
static struct {
    oq_deck_audio_fn fn;void *user;
    long (*orig)(void *,const void *,unsigned long);
    volatile int enabled;unsigned installed;
    struct oq__da_fmt fmts[4];
    size_t (*hw_sizeof)(void);int (*hw_current)(void *,void *);
    int (*hw_channels)(const void *,unsigned *);int (*hw_format)(const void *,int *);
    int (*hw_rate)(const void *,unsigned *,int *);
    unsigned calls,processed,skipped,max_ns;
    const struct oq__da_fmt *deck;
    char previous[64];
} oq__da;
static int32_t oq__da_out[16u*OQ_DECK_AUDIO_MAX_FRAMES] __attribute__((aligned(16)));
static float oq__da_f[2u*OQ_DECK_AUDIO_DECKS][OQ_DECK_AUDIO_MAX_FRAMES] __attribute__((aligned(16)));

static const struct oq__da_fmt *oq__da_format(void *pcm) {
    for(unsigned i=0;i<4;i++)if(oq__da.fmts[i].pcm==pcm)return oq__da.fmts[i].ok?&oq__da.fmts[i]:0;
    for(unsigned i=0;i<4;i++)if(!oq__da.fmts[i].pcm) {
        struct oq__da_fmt *f=&oq__da.fmts[i];f->pcm=pcm;
        if(!oq__da.hw_sizeof||!oq__da.hw_current||!oq__da.hw_channels||!oq__da.hw_format||!oq__da.hw_rate)return 0;
        size_t n=oq__da.hw_sizeof();if(!n||n>4096)return 0;
        unsigned char params[4096] __attribute__((aligned(16)));memset(params,0,n);
        unsigned ch=0,rate=0;int fmt=-1,dir=0;
        if(oq__da.hw_current(pcm,params)<0||oq__da.hw_channels(params,&ch)<0||oq__da.hw_format(params,&fmt)<0||
           oq__da.hw_rate(params,&rate,&dir)<0)return 0;
        f->channels=(int)ch;f->format=fmt;f->rate=rate;f->ok=1;return f;
    }
    return 0;
}
static int oq__da_is_deck(const struct oq__da_fmt *f) {
    return f&&f->ok&&f->channels>=8&&f->channels<=16&&(f->format==10||f->format==6||f->format==14);
}
static long oq__da_writei(void *pcm,const void *buf,unsigned long frames) {
    __atomic_fetch_add(&oq__da.calls,1,__ATOMIC_RELAXED);
    if(!__atomic_load_n(&oq__da.enabled,__ATOMIC_ACQUIRE)||!buf||!frames)return oq__da.orig(pcm,buf,frames);
    const struct oq__da_fmt *f=oq__da_format(pcm);
    if(!oq__da_is_deck(f))return oq__da.orig(pcm,buf,frames);
    if(!oq__da.deck)oq__da.deck=f;               /* the first deck-shaped stream */
    if(f!=oq__da.deck)return oq__da.orig(pcm,buf,frames);
    if(frames>OQ_DECK_AUDIO_MAX_FRAMES){__atomic_fetch_add(&oq__da.skipped,1,__ATOMIC_RELAXED);return oq__da.orig(pcm,buf,frames);}
    struct timespec t0;clock_gettime(CLOCK_MONOTONIC,&t0);
    unsigned ch=(unsigned)f->channels,n=(unsigned)frames;
    memcpy(oq__da_out,buf,(size_t)n*ch*4u);
    struct oq_deck_audio_block b;b.frames=n;b.sample_rate=f->rate?f->rate:44100u;b.channels=ch;
    for(unsigned d=0;d<OQ_DECK_AUDIO_DECKS;d++){b.left[d]=oq__da_f[2*d];b.right[d]=oq__da_f[2*d+1];}
    if(f->format==14) {
        const float *x=(const float *)oq__da_out;
        for(unsigned i=0;i<n;i++)for(unsigned k=0;k<8;k++)oq__da_f[k][i]=x[i*ch+k];
    } else {
        const float s=f->format==6?1.0f/8388608.0f:1.0f/2147483648.0f;
        for(unsigned i=0;i<n;i++)for(unsigned k=0;k<8;k++)oq__da_f[k][i]=(float)oq__da_out[i*ch+k]*s;
    }
    oq__da.fn(&b,oq__da.user);
    if(f->format==14) {
        float *x=(float *)oq__da_out;
        for(unsigned i=0;i<n;i++)for(unsigned k=0;k<8;k++){float v=oq__da_f[k][i];
            if(!(v>-1.0f))v=v==v?-1.0f:0.0f;else if(v>1.0f)v=1.0f;   /* NaN -> silence */
            x[i*ch+k]=v;}
    } else {
        const double full=f->format==6?8388607.0:2147483647.0,lo=f->format==6?-8388608.0:-2147483648.0;
        for(unsigned i=0;i<n;i++)for(unsigned k=0;k<8;k++){float v=oq__da_f[k][i];
            double y=v==v?(double)v*(full+1.0):0.0;if(y>full)y=full;else if(y<lo)y=lo;
            oq__da_out[i*ch+k]=(int32_t)y;}
    }
    struct timespec t1;clock_gettime(CLOCK_MONOTONIC,&t1);
    unsigned ns=(unsigned)((t1.tv_sec-t0.tv_sec)*1000000000L+(t1.tv_nsec-t0.tv_nsec));
    if(ns>__atomic_load_n(&oq__da.max_ns,__ATOMIC_RELAXED))__atomic_store_n(&oq__da.max_ns,ns,__ATOMIC_RELAXED);
    __atomic_fetch_add(&oq__da.processed,1,__ATOMIC_RELAXED);
    return oq__da.orig(pcm,oq__da_out,frames);
}
/* Once, from oqmod_init or tick. Processing starts off: call oq_deck_audio_enable(1). */
__attribute__((unused)) static int oq_deck_audio_install(oq_deck_audio_fn fn,void *user) {
    if(!fn)return OQ_DECK_AUDIO_INVALID;
    if(oq__da.installed)return OQ_DECK_AUDIO_BUSY;
    void **slot=(void **)(uintptr_t)OQ_DECK_AUDIO_SLOT;
    Dl_info info;memset(&info,0,sizeof info);
    /* the slot must be readable (other firmware: maybe not even mapped) ... */
    void *cur=0;
    {struct iovec l={&cur,sizeof cur},r={(void *)slot,sizeof cur};
     if(syscall(SYS_process_vm_readv,(long)getpid(),&l,1L,&r,1L,0L)!=(long)sizeof cur)return OQ_DECK_AUDIO_UNSUPPORTED;}
    /* ... and hold libasound's snd_pcm_writei or another mod's hook (a shared library) */
    void *real=dlsym(RTLD_DEFAULT,"snd_pcm_writei");
    if(!cur||!dladdr(cur,&info)||!info.dli_fname)return OQ_DECK_AUDIO_UNSUPPORTED;
    if(cur!=real&&!strstr(info.dli_fname,".so"))return OQ_DECK_AUDIO_UNSUPPORTED;
    oq__da.hw_sizeof=(size_t (*)(void))dlsym(RTLD_DEFAULT,"snd_pcm_hw_params_sizeof");
    oq__da.hw_current=(int (*)(void *,void *))dlsym(RTLD_DEFAULT,"snd_pcm_hw_params_current");
    oq__da.hw_channels=(int (*)(const void *,unsigned *))dlsym(RTLD_DEFAULT,"snd_pcm_hw_params_get_channels");
    oq__da.hw_format=(int (*)(const void *,int *))dlsym(RTLD_DEFAULT,"snd_pcm_hw_params_get_format");
    oq__da.hw_rate=(int (*)(const void *,unsigned *,int *))dlsym(RTLD_DEFAULT,"snd_pcm_hw_params_get_rate");
    long pg=sysconf(_SC_PAGESIZE);uintptr_t page=(uintptr_t)slot&~(uintptr_t)(pg-1);
    if(mprotect((void *)page,(size_t)pg,PROT_READ|PROT_WRITE))return OQ_DECK_AUDIO_FAILED;
    const char *base=strrchr(info.dli_fname,'/');
    strncpy(oq__da.previous,cur==real?"libasound":base?base+1:info.dli_fname,sizeof oq__da.previous-1);
    oq__da.fn=fn;oq__da.user=user;oq__da.orig=(long (*)(void *,const void *,unsigned long))cur;
    oq__da.installed=1;
    __atomic_store_n(slot,(void *)oq__da_writei,__ATOMIC_RELEASE);
    return OQ_DECK_AUDIO_OK;
}
/* From enabled() / tick(): 0 passes the audio on untouched. */
__attribute__((unused)) static void oq_deck_audio_enable(int on){__atomic_store_n(&oq__da.enabled,on?1:0,__ATOMIC_RELEASE);}
__attribute__((unused)) static void oq_deck_audio_status(struct oq_deck_audio_status *s) {
    memset(s,0,sizeof *s);
    s->installed=oq__da.installed;s->enabled=(uint32_t)__atomic_load_n(&oq__da.enabled,__ATOMIC_ACQUIRE);
    s->calls=__atomic_load_n(&oq__da.calls,__ATOMIC_RELAXED);s->processed=__atomic_load_n(&oq__da.processed,__ATOMIC_RELAXED);
    s->skipped=__atomic_load_n(&oq__da.skipped,__ATOMIC_RELAXED);s->max_ns=__atomic_load_n(&oq__da.max_ns,__ATOMIC_RELAXED);
    const struct oq__da_fmt *f=oq__da.deck;
    if(f){s->channels=(uint32_t)f->channels;s->format=(uint32_t)f->format;s->sample_rate=f->rate;}
    memcpy(s->previous,oq__da.previous,sizeof s->previous);
}
#endif

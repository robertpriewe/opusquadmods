/* Mobius FX 0.1 (test build): a beat-synced, endlessly rising or falling tone (Shepard
 * style octave stack, original DSP in mobius_dsp.h) added to one mixer channel.
 *
 * Why this way (EP136 1.32 / MIXER controller, static RE 2026-11-12):
 *  - Beat FX run inside the D810 mixer. EP136 tells it the effect number (TX frame byte
 *    0x0c & 15); the mixer uses it as an index into 14 parameter records of 0x1ec bytes
 *    (controller 0x8e38 -> +0x78, read 0x8f7c), so a 15th number must never be sent.
 *  - EP136's own audio leaves through JUCE's ALSA writer (0x1e6a544): interleaved S32,
 *    8 channels = 4 stereo mixer inputs, via snd_pcm_writei. Its GOT slot 0x349a130 is
 *    writable (prodjlink patches GOT slots the same way). The hook copies the period,
 *    adds the tone to one stereo pair and passes the copy on: after Stems and every
 *    other deck processing, before the mixer's channel EQ/fader.
 *  - The native Beat FX list keeps 14 item widgets at controller +0xb0 and the index at
 *    +0x50; select routine 0x190b030 refuses index > 13. Logged here (stage 2 adds the
 *    MOBIUS entry).
 *
 * This build: Off/Rise/Fall in the mod's menu, channel, beats per octave, level. It
 * logs what stage 2 needs: list picks, mixer frame bytes that change (LEVEL/DEPTH,
 * BEAT FX SELECT, ON), the FX target and the audio format.
 *
 * 0.3 (hardware 2026-11-16, R125: hook ran 64000 times, mixed 0, no sound): the stream
 * was not 8 ch S32 (the XDJ-AZ's deck stream is 10 ch S32, ~780 writes/s here suggests
 * more than one stream). Now: every stream's format and write rate on the mod's page;
 * mixes into an 8..16 channel stream in S32_LE, S24_LE or FLOAT_LE, the first such
 * stream (Auto) or the one picked under "Audio stream".
 * Hardware 0.3 (2026-11-16): audible; stream 1 = 10 ch S32 44100 Hz, ~710 writes/s.
 *
 * 0.4: MOBIUS entry in the Beat FX list (mobius_list.h). Selected + BEAT FX ON (mixer frame
 * byte 88 bit 0x20, 0.2 log: 03 -> 23 -> 03 on two presses) plays the oscillator on the
 * "Channel" set here; sound/direction from the menu. The stock effect still runs too.
 * 0.7: MOBIUS also in the effect box on the right (mobius_list.h).
 * 0.8: no touch, box hidden under native pop-ups, native name images saved to MODS/mobius/native.
 * 0.9: MOBIUS drawn in the native font (mobius_name_px.h); native highlight switched, not covered.
 * 1.0: release build (Experimental): no test tone, audio stream on Auto, no image dumps, plain status. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "oqmod.h"
#include "oqmod_firmware.h"
#include "mobius_dsp.h"

static const struct oqmod_api *api;
static int on;
static unsigned lines;
#define LOG(...) do{if(lines<600){char l_[200];snprintf(l_,sizeof l_,__VA_ARGS__);api->log(l_);lines++;}}while(0)

#include <stdlib.h>
#include "mobius_list.h"
static unsigned char ml_ring[12];static unsigned ml_ring_n;
/* ---- 0.6: BEAT FX SELECT knob ---- */
struct mk_input {int delta;unsigned char push;};
static void (*mk_original)(void *,const void *,const struct mk_input *);
static volatile uint64_t mk_last_id;static volatile uint64_t mk_last_ns;
static volatile uint64_t mk_knob;              /* learned identity, 0 = not yet */
static volatile unsigned mk_steps,mk_held;
static uint64_t mk_ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec;}
static void mk_input(void *self,const void *source,const struct mk_input *value){
    if(self&&value&&*(const uintptr_t *)self==0x29bbbc0ul&&!value->push) {
        uint64_t id=*(const uint64_t *)((const char *)self+0xd0);
        int d=(int)(int16_t)(value->delta&0xffff);
        if(d){__atomic_store_n(&mk_last_id,id,__ATOMIC_RELAXED);__atomic_store_n(&mk_last_ns,mk_ns(),__ATOMIC_RELEASE);}
        uint64_t knob=__atomic_load_n(&mk_knob,__ATOMIC_ACQUIRE);
        uint64_t ctrl=__atomic_load_n(&ml_ctrl,__ATOMIC_ACQUIRE);
        if(knob&&id==knob&&(d==1||d==-1)&&ctrl&&__atomic_load_n(&on,__ATOMIC_RELAXED)&&api->setting_get) {
            unsigned cur=*(const unsigned *)(uintptr_t)(ctrl+0x50);
            int sel=__atomic_load_n(&ml_selected,__ATOMIC_ACQUIRE),hold=0;
            __atomic_fetch_add(&mk_steps,1,__ATOMIC_RELAXED);
            if(!sel){
                if((cur==13&&d>0)||(cur==0&&d<0)){hold=1;__atomic_store_n(&ml_selected,1,__ATOMIC_RELEASE);}
            }else{
                __atomic_store_n(&ml_selected,0,__ATOMIC_RELEASE);
                if((cur==13&&d<0)||(cur==0&&d>0))hold=1;   /* back to the stock entry next to MOBIUS */
            }
            if(hold){
                __atomic_fetch_add(&mk_held,1,__ATOMIC_RELAXED);
                __atomic_fetch_add((unsigned *)((char *)self+0xd8),(unsigned)value->delta,__ATOMIC_RELEASE);
                __atomic_store_n((unsigned char *)((char *)self+0xdc),value->push,__ATOMIC_RELEASE);
                return;
            }
        }
    }
    mk_original(self,source,value);
}
static unsigned mk_hooked;
/* learning: a byte-20 step with one encoder event in the 200 ms before it */
static uint64_t mk_cand;static unsigned mk_votes;
static void mk_learn(unsigned char counter){
    static int have;static unsigned char prev;
    if(!have){have=1;prev=counter;return;}
    if(counter==prev)return;
    prev=counter;
    if(__atomic_load_n(&mk_knob,__ATOMIC_ACQUIRE))return;
    uint64_t t=__atomic_load_n(&mk_last_ns,__ATOMIC_ACQUIRE),id=__atomic_load_n(&mk_last_id,__ATOMIC_RELAXED);
    if(!t||mk_ns()-t>300000000ull)return;
    if(id==mk_cand)mk_votes++;else{mk_cand=id;mk_votes=1;}
    if(mk_votes>=3){__atomic_store_n(&mk_knob,id,__ATOMIC_RELEASE);LOG("mobius: BEAT FX SELECT knob is %016llx",(unsigned long long)id);}
}

/* ---- settings shared with the audio thread ------------------------------------------- */
static int mb_mode;            /* 0 off, 1 rise, 2 fall */
static int mb_wave;            /* 0 sine, 1 triangle, 2 saw (X-PAD on the XDJ-AZ) */
static int mb_channel;         /* 0..3 */
static int64_t mb_cycle_us;    /* seconds per octave x 1e6 */
static int mb_gain_milli;      /* 0..200 (mb_render caps at 0.2) */

/* ---- ALSA format of each PCM EP136 writes to ----------------------------------------- */
struct mb_fmt {void *pcm;int channels,format;unsigned rate,ok;unsigned calls,shown,per_s;};
static struct mb_fmt mb_fmts[4];
static size_t (*al_sizeof)(void);
static int (*al_current)(void *,void *);
static int (*al_channels)(const void *,unsigned *);
static int (*al_format)(const void *,int *);
static int (*al_rate)(const void *,unsigned *,int *);
static const struct mb_fmt *mb_format(void *pcm) {
    for(unsigned i=0;i<4;i++)if(mb_fmts[i].pcm==pcm)return mb_fmts[i].ok?&mb_fmts[i]:0;
    for(unsigned i=0;i<4;i++)if(!mb_fmts[i].pcm) {
        struct mb_fmt *f=&mb_fmts[i];f->pcm=pcm;
        if(!al_sizeof||!al_current||!al_channels||!al_format||!al_rate)return 0;
        size_t n=al_sizeof();if(!n||n>4096)return 0;
        unsigned char params[4096] __attribute__((aligned(16)));memset(params,0,n);
        unsigned ch=0,rate=0;int fmt=-1,dir=0;
        if(al_current(pcm,params)<0||al_channels(params,&ch)<0||al_format(params,&fmt)<0||al_rate(params,&rate,&dir)<0)return 0;
        f->channels=(int)ch;f->format=fmt;f->rate=rate;f->ok=1;return f;
    }
    return 0;
}

/* ---- the audio hook ------------------------------------------------------------------ */
typedef long (*writei_t)(void *,const void *,unsigned long);
static writei_t mb_orig_writei;
static struct mb_tables mb_tab;
static struct mb_osc mb_osc;
static int32_t mb_scratch[8*1024] __attribute__((aligned(16)));
static float mb_tone[1024];
static unsigned mb_calls,mb_mixed,mb_last_frames,mb_max_ns;
static int mb_usable(const struct mb_fmt *f) {
    return f&&f->ok&&f->channels>=8&&f->channels<=16&&(f->format==10||f->format==6||f->format==14);
}
static int mb_stream_pick;     /* 0 auto (first usable), 1..4 that stream */
static int mb_target_index=-1; /* stream being mixed, for the status */
static long mb_writei(void *pcm,const void *buf,unsigned long frames) {
    __atomic_fetch_add(&mb_calls,1,__ATOMIC_RELAXED);
    __atomic_store_n(&mb_last_frames,(unsigned)frames,__ATOMIC_RELAXED);
    const struct mb_fmt *f=mb_format(pcm);
    if(f)__atomic_fetch_add((unsigned *)&f->calls,1,__ATOMIC_RELAXED);
    if(!mb_usable(f)||!buf||!frames||frames>1024)return mb_orig_writei(pcm,buf,frames);
    int index=(int)(f-mb_fmts),pick=__atomic_load_n(&mb_stream_pick,__ATOMIC_RELAXED);
    if(pick){if(index!=pick-1)return mb_orig_writei(pcm,buf,frames);}
    else for(int i=0;i<index;i++)if(mb_usable(&mb_fmts[i]))return mb_orig_writei(pcm,buf,frames);
    __atomic_store_n(&mb_target_index,index,__ATOMIC_RELAXED);
    unsigned chans=(unsigned)f->channels;
    if((unsigned long)chans*frames>8u*1024u)return mb_orig_writei(pcm,buf,frames);
    int mode=__atomic_load_n(&on,__ATOMIC_RELAXED)?__atomic_load_n(&mb_mode,__ATOMIC_RELAXED):0;
    if(!mode&&mb_osc.envelope<=0.0f)return mb_orig_writei(pcm,buf,frames);
    struct timespec t0;clock_gettime(CLOCK_MONOTONIC,&t0);
    double cycle=(double)__atomic_load_n(&mb_cycle_us,__ATOMIC_RELAXED)/1e6;
    if(mode==2)cycle=-cycle;
    float gain=(float)__atomic_load_n(&mb_gain_milli,__ATOMIC_RELAXED)/1000.0f;
    mb_render_wave(&mb_tab,&mb_osc,mb_tone,(unsigned)frames,(double)(f->rate?f->rate:44100u),cycle?cycle:1.0,gain,mode!=0,
                   (unsigned)__atomic_load_n(&mb_wave,__ATOMIC_RELAXED)%MB_WAVES);
    if(!mode&&mb_osc.envelope<=0.0f)mb_osc_reset(&mb_osc);
    unsigned ch=(unsigned)__atomic_load_n(&mb_channel,__ATOMIC_RELAXED)&3u;
    memcpy(mb_scratch,buf,frames*chans*4u);
    if(f->format==14){                       /* FLOAT_LE */
        float *x=(float *)mb_scratch;
        for(unsigned long i=0;i<frames;i++)for(unsigned k=0;k<2;k++){
            float v=x[i*chans+ch*2+k]+mb_tone[i];if(v>1.0f)v=1.0f;if(v<-1.0f)v=-1.0f;x[i*chans+ch*2+k]=v;}
    }else{                                   /* S32_LE full scale, S24_LE in 32-bit words */
        int64_t full=f->format==6?8388607:2147483647,lo=f->format==6?-8388608:INT32_MIN;
        for(unsigned long i=0;i<frames;i++){
            int64_t add=(int64_t)(mb_tone[i]*(float)full);
            for(unsigned k=0;k<2;k++){
                int64_t v=(int64_t)mb_scratch[i*chans+ch*2+k]+add;
                if(v>full)v=full;
                if(v<lo)v=lo;
                mb_scratch[i*chans+ch*2+k]=(int32_t)v;
            }
        }
    }
    __atomic_fetch_add(&mb_mixed,1,__ATOMIC_RELAXED);
    struct timespec t1;clock_gettime(CLOCK_MONOTONIC,&t1);
    unsigned ns=(unsigned)((t1.tv_sec-t0.tv_sec)*1000000000L+(t1.tv_nsec-t0.tv_nsec));
    if(ns>__atomic_load_n(&mb_max_ns,__ATOMIC_RELAXED))__atomic_store_n(&mb_max_ns,ns,__ATOMIC_RELAXED);
    return mb_orig_writei(pcm,mb_scratch,frames);
}
static int mb_got_install(void) {
    void **slot=(void **)0x349a130;void *cur=__atomic_load_n(slot,__ATOMIC_ACQUIRE);
    Dl_info info;
    memset(&info,0,sizeof info);
    /* chain-friendly: libasound, or another mod's hook (Mono Master) */
    if(!cur||!dladdr(cur,&info)||!info.dli_fname) {
        LOG("mobius: snd_pcm_writei GOT slot holds %p, unknown code: not hooked",cur);
        return 0;
    }
    long pg=sysconf(_SC_PAGESIZE);uintptr_t page=(uintptr_t)slot&~(uintptr_t)(pg-1);
    if(mprotect((void *)page,(size_t)pg,PROT_READ|PROT_WRITE)){LOG("mobius: GOT page not writable");return 0;}
    mb_orig_writei=(writei_t)cur;
    __atomic_store_n(slot,(void *)mb_writei,__ATOMIC_RELEASE);
    LOG("mobius: snd_pcm_writei hooked (was %s)",info.dli_fname);
    return 1;
}

/* ---- research: the native Beat FX list ----------------------------------------------- */
static void *mb_select_orig;
static unsigned mb_pick_count,mb_pick_last,mb_pick_prev_index;
static void mb_select_hook(uint64_t self,unsigned index) {
    __atomic_store_n(&ml_ctrl,self,__ATOMIC_RELEASE);
    __atomic_store_n(&ml_selected,0,__ATOMIC_RELEASE);   /* a stock effect picked: MOBIUS off */
    {unsigned i=__atomic_fetch_add(&ml_ring_n,1,__ATOMIC_RELAXED)%12u;
     unsigned from=self?*(unsigned *)(uintptr_t)(self+0x50):15u;
     ml_ring[i]=(unsigned char)(((from>15?15:from)<<4)|(index>15?15:index));}
    __atomic_store_n(&mb_pick_prev_index,self?*(unsigned *)(uintptr_t)(self+0x50):99u,__ATOMIC_RELAXED);
    __atomic_store_n(&mb_pick_last,index,__ATOMIC_RELAXED);
    __atomic_fetch_add(&mb_pick_count,1,__ATOMIC_RELEASE);
    ((void (*)(uint64_t,unsigned))mb_select_orig)(self,index);
}

/* ---- tick: settings, logging, status ------------------------------------------------- */
static const double beats_v[]={0.5,1,2,4,8,16,32,64};
static const int gain_v[]={50,100,200};
static void tick(void) {
    static uint64_t last;uint64_t now=api->now_ms();
    if(now-last<100)return;
    last=now;
    int test=0,ch=api->setting_get("channel",0),beats=api->setting_get("beats",3),level=api->setting_get("level",1);
    if(test<0||test>6)test=0;
    int mode=test?(test<=3?1:2):0,wave=test?(test-1)%3:0;
    /* the list entry: MOBIUS selected and BEAT FX ON */
    static int list_entry=-1;if(list_entry<0||now%1000<100)list_entry=api->setting_get("entry",1);
    int fx_on=0;{struct oqmod_mixer mm;memset(&mm,0,sizeof mm);mm.size=sizeof mm;
        if(api->mixer&&api->mixer(&mm)&&mm.valid){fx_on=(mm.raw[88]&0x20)!=0;mk_learn(mm.raw[20]);}}
    __atomic_store_n(&ml_fx_on,fx_on,__ATOMIC_RELAXED);
    int sel=__atomic_load_n(&ml_selected,__ATOMIC_ACQUIRE)&&list_entry;
    if(!test&&sel&&fx_on){int sound=api->setting_get("sound",0),dir=api->setting_get("dir",0);
        if(sound<0||sound>2)sound=0;
        mode=dir==1?2:1;wave=sound;}
    ml_tick(list_entry&&on);
    if(ch<0||ch>3)ch=0;
    if(beats<0||beats>7)beats=3;
    if(level<0||level>2)level=1;
    int stream=0;   /* 1.0: Auto */
    __atomic_store_n(&mb_stream_pick,stream,__ATOMIC_RELAXED);
    struct oqmod_deck d;memset(&d,0,sizeof d);
    double bpm=api->deck((unsigned)ch+1,&d)&&d.bpm_x100?d.bpm_x100/100.0:128.0;
    if(bpm<40||bpm>300)bpm=128;
    double seconds=beats_v[beats]*60.0/bpm;if(seconds>32)seconds=32;   /* XDJ-AZ TIME: 10 ms .. 32 s */
    __atomic_store_n(&mb_cycle_us,(int64_t)(seconds*1e6),__ATOMIC_RELAXED);
    __atomic_store_n(&mb_wave,wave,__ATOMIC_RELAXED);
    __atomic_store_n(&mb_gain_milli,gain_v[level],__ATOMIC_RELAXED);
    __atomic_store_n(&mb_channel,ch,__ATOMIC_RELAXED);
    __atomic_store_n(&mb_mode,on?mode:0,__ATOMIC_RELAXED);

    /* list picks */
    static unsigned seen_picks;unsigned picks=__atomic_load_n(&mb_pick_count,__ATOMIC_ACQUIRE);
    if(picks!=seen_picks){seen_picks=picks;LOG("mobius: FX list select index %u (was %u), %u calls",__atomic_load_n(&mb_pick_last,__ATOMIC_RELAXED),__atomic_load_n(&mb_pick_prev_index,__ATOMIC_RELAXED),picks);}
    /* FX target */
    struct oqmod_fx_target t;memset(&t,0,sizeof t);t.size=sizeof t;
    static uint32_t last_target=99,last_mic=99;
    if(api->fx_target_read&&api->fx_target_read(&t)&&t.ready&&(t.value!=last_target||t.mic!=last_mic)){
        last_target=t.value;last_mic=t.mic;LOG("mobius: FX target %u mic %u",t.value,t.mic);
    }
    /* audio status */
    static uint64_t status_ms;
    if(now-status_ms>=1000) {
        status_ms=now;
        static unsigned last_calls;unsigned calls=__atomic_load_n(&mb_calls,__ATOMIC_RELAXED);
        static int fmt_logged[4];
        for(unsigned i=0;i<4;i++)if(mb_fmts[i].pcm&&!fmt_logged[i]){fmt_logged[i]=1;
            LOG("mobius: pcm %p: %d ch, format %d, %u Hz%s",mb_fmts[i].pcm,mb_fmts[i].channels,mb_fmts[i].format,mb_fmts[i].rate,mb_fmts[i].ok?"":" (unknown)");}
        /* 1.0: only tell the user when the audio output could not be attached */
        api->set_status(mb_orig_writei?"":"Could not attach to the audio output; Mobius is silent.");
        (void)last_calls;
        last_calls=calls;
        static uint64_t stat_log_ms;
        if(now-stat_log_ms>=10000){stat_log_ms=now;LOG("mobius: stats calls %u mixed %u frames %u max %u ns mode %d wave %d ch %d",calls,
            __atomic_load_n(&mb_mixed,__ATOMIC_RELAXED),__atomic_load_n(&mb_last_frames,__ATOMIC_RELAXED),__atomic_load_n(&mb_max_ns,__ATOMIC_RELAXED),mode,wave,ch+1);}
    }
}
static void enabled(int yes) {
    __atomic_store_n(&on,yes?1:0,__ATOMIC_RELEASE);
    if(!yes)__atomic_store_n(&mb_mode,0,__ATOMIC_RELEASE);   /* fades out within 10 ms */
}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info) {
    if(!a||a->version<OQMOD_API_VERSION)return 1;
    if(!OQMOD_HAS_R122(a))return oqmod_needs_firmware(a,info,"Mobius FX (Experimental)","1.0","R124");
    api=a;
    static const char *const chans[]={"1","2","3","4"},*const beats[]={"1/2","1","2","4","8","16","32","64"},*const levels[]={"Low","Medium","High"};
    static const char *const sounds[]={"Sine","Triangle","Saw"},*const dirs[]={"Rise","Fall"};
    api->option("entry","MOBIUS in the Beat FX list",1);
    api->choice("sound","Mobius sound",sounds,3,0);
    api->choice("dir","Mobius direction",dirs,2,0);
    api->choice("channel","Channel",chans,4,0);
    api->choice("beats","Beats per octave",beats,8,3);
    api->choice("level","Level",levels,3,1);
    if(api->panel_renderer&&api->panels_set&&!api->panel_renderer(ml_draw,ml_touch))LOG("mobius: panel renderer refused");
    mb_tables_init(&mb_tab);mb_osc_reset(&mb_osc);
    __atomic_store_n(&mb_cycle_us,1875000,__ATOMIC_RELAXED);
    __atomic_store_n(&mb_gain_milli,100,__ATOMIC_RELAXED);
    al_sizeof=(size_t (*)(void))dlsym(RTLD_DEFAULT,"snd_pcm_hw_params_sizeof");
    al_current=(int (*)(void *,void *))dlsym(RTLD_DEFAULT,"snd_pcm_hw_params_current");
    al_channels=(int (*)(const void *,unsigned *))dlsym(RTLD_DEFAULT,"snd_pcm_hw_params_get_channels");
    al_format=(int (*)(const void *,int *))dlsym(RTLD_DEFAULT,"snd_pcm_hw_params_get_format");
    al_rate=(int (*)(const void *,unsigned *,int *))dlsym(RTLD_DEFAULT,"snd_pcm_hw_params_get_rate");
    if(api->ep136_ok()) {
        mb_got_install();
        if(!api->hook(0x190b030,0xa9bb7bfd,(void *)mb_select_hook,&mb_select_orig))LOG("mobius: FX list select hook refused");
        if(api->patch){uintptr_t cur=((const uintptr_t *)0x29bbbc0)[4];
            if(cur&&api->patch(0x29bbbc0+4*8,cur,(void *)mk_input,(void **)&mk_original)==1)mk_hooked=1;else LOG("mobius: knob hook refused");}
    } else LOG("mobius: not EP136 1.32, nothing installed");
    LOG("mobius: alsa helpers %s",al_sizeof&&al_current&&al_channels&&al_format&&al_rate?"found":"MISSING");
    info->api_version=OQMOD_API_VERSION;
    info->name="Mobius FX (Experimental)";
    info->description="EXPERIMENTAL: MOBIUS in the Beat FX list. Select it and press\nBEAT FX ON for a rising or falling Mobius tone.";
    info->version="1.0";
    info->tick=tick;
    info->enabled=enabled;
    return 0;
}

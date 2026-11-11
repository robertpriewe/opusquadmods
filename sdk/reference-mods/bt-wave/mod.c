/* Bluetooth Waveform, an add-on mod: MODS/bt-wave/mod.so.
 * Draws the Bluetooth audio as a scrolling waveform while it plays, newest on the right,
 * in a strip near the bottom of the WAVEFORM screen. First step towards a live Bluetooth
 * deck: it shows how fast audio can be turned into a waveform on the deck, and its status
 * lines show how the packets arrive (codec, rate, longest gap).
 * EP136 decodes Bluetooth itself; the mod hooks both decoders the first time it is
 * switched on and copies only what the Bluetooth AudioDecoder decodes (the AAC library is
 * also used for tracks, which are never taken). Option "live" (bt_live.h) also writes the
 * audio to a growing WAV on the USB, to load on a deck while it records:
 *   SBC  wrapper 0x1d48668(this, frames, len, out, *out_bytes): one caller (bl at 0x1d9d744)
 *   AAC  TMC decode 0x21571a0(dec, in, len, out, info): 0 = 1024 stereo frames at out
 *        (= AudioDecoder+0x340, stream rate at +728); Bluetooth caller bl at 0x1d9ce9c. */
#include <stdio.h>
#include "bt_wave.h"
#include "bt_live.h"
#include "bt_direct.h"
#include "bt_publish.h"
#include "bt_route.h"
#include "oqmod.h"

#define BW_SBC_WRAPPER 0x1d48668ul
#define BW_SBC_RETURN 0x1d9d748ul
#define BW_AAC_DECODE 0x21571a0ul
#define BW_AAC_RETURN 0x1d9cea0ul
#define BW_AAC_RATE_OFF (728-0x340)

static const struct oqmod_api *api;
static unsigned on,installed,hooks,ticks,draws,draws_per_second;
static uint64_t shown_head,second_ms;

static int (*o_sbc)(void *,const unsigned char *,unsigned,int16_t *,size_t *);
static int (*o_aac)(void *,const void *,int,int16_t *,void *);
static int sbc_hook(void *self,const unsigned char *frames,unsigned len,int16_t *out,size_t *out_bytes) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    int r=o_sbc(self,frames,len,out,out_bytes);
    if(__atomic_load_n(&on,__ATOMIC_RELAXED)&&caller==BW_SBC_RETURN&&out&&out_bytes&&*out_bytes&&*out_bytes<=(1u<<20))
    {
        unsigned rate=bw_sbc_rate(frames,len);
        bw_feed(BW_SBC,rate,out,*out_bytes/4);bl_push(out,*out_bytes/4,rate);
    }
    return r;
}
static int aac_hook(void *decoder,const void *in,int len,int16_t *out,void *info) {
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    int r=o_aac(decoder,in,len,out,info);
    if(__atomic_load_n(&on,__ATOMIC_RELAXED)&&caller==BW_AAC_RETURN&&r==0&&out) {
        uint64_t rate=*(const uint64_t *)((const char *)out+BW_AAC_RATE_OFF);
        bw_feed(BW_AAC,(unsigned)(rate<=96000?rate:0),out,1024);bl_push(out,1024,(unsigned)(rate<=96000?rate:0));
    }
    return r;
}
static void install(void) {
    installed=1;
    if(*(const uint32_t *)(BW_SBC_RETURN-4)==0x97feabc9u&&
       api->hook(BW_SBC_WRAPPER,0xa9b77bfdu,(void *)sbc_hook,(void **)&o_sbc))hooks|=1;
    if(*(const uint32_t *)(BW_AAC_RETURN-4)==0x940ee8c1u&&
       api->hook(BW_AAC_DECODE,0xa9bc7bfdu,(void *)aac_hook,(void **)&o_aac))hooks|=2;
    if(api->patch(BD_SLOT,BD_FN,(void *)bd_read,(void **)&bd_orig))bd_installed=1;   /* direct reads of the live track */
    if(api->patch(BR_SLOT,BR_FN,(void *)br_hook,(void **)&br_orig))br_installed=1;   /* Bluetooth -> the waveform track */
    char line[64];snprintf(line,sizeof line,"hooks sbc=%u aac=%u direct=%u",hooks&1,hooks>>1&1,bd_installed);api->log(line);
}

static void status(void) {
    uint64_t now=bw_now_us(),last=bw_last_us;
    char text[256];
    if(!installed)return;                           /* keeps "Not available ..." */
    if(!hooks) {snprintf(text,sizeof text,"Could not attach to the Bluetooth decoder.");api->set_status(text);return;}
    if(!last) {api->set_status("No Bluetooth audio yet: play music from a paired phone.");return;}
    unsigned secs=(unsigned)((last-bw_first_us)/1000000u);
    unsigned per_second=secs?(unsigned)(bw_packets/secs):0;
    int n=snprintf(text,sizeof text,"%s%s %u.%u kHz, %u packets a second, longest gap %u ms\nScreen updates: %u a second",
             now-last>2000000u?"Paused. Last: ":"",bw_codec==BW_AAC?"AAC":"SBC",bw_rate/1000,bw_rate%1000/100,
             per_second,(unsigned)(bw_gap_max_us/1000),draws_per_second);
    if(__atomic_load_n(&bl_enabled,__ATOMIC_ACQUIRE)&&n>0&&(size_t)n<sizeof text) {
        unsigned sec=(unsigned)(bl_written/(bl_rate*4u));
        if(bl_open_failed)snprintf(text+n,sizeof text-(size_t)n,"\nLive track: could not write OQ_BTLIVE_DATA on the USB");
        else if(!bl_number)snprintf(text+n,sizeof text-(size_t)n,"\nLive track: starts with the Bluetooth audio");
        else snprintf(text+n,sizeof text-(size_t)n,"\nLive LIVE-%u.WAV: %u:%02u recorded%s\nDirect reads %s %u, waveform updates %s%s",bl_number,sec/60,sec%60,
                      bl_fuse_failed?" (FUSE failed: not loadable)":"",bd_installed?"on":"refused",bd_direct,
                      bp_state==1?"every 2 s":bp_state==2?"refused":"stock",bf_short_reads?", quick reads on":"");
    }
    api->set_status(text);
}
/* Which of EP136's threads read the live track, and how far (to the log, when it changes). */
static void log_readers(void) {
    static unsigned shown;
    if(bf_reads==shown||ticks%150)return;
    shown=bf_reads;char line[160];
    snprintf(line,sizeof line,"live track: reads=%u silence=%u fuse_errors=%u direct=%u passed=%u route=%u swapped=%u passed=%u",bf_reads,bf_silence_bytes,bf_errors,bd_direct,bd_passed,br_installed,br_swapped,br_passed);api->log(line);
    for(int i=0;i<BF_READERS;i++)if(bf_readers[i].pid) {
        snprintf(line,sizeof line,"live reader %s (%u): %u reads, up to %.1f s",bf_readers[i].comm,bf_readers[i].pid,bf_readers[i].reads,
                 bf_readers[i].furthest>44?(double)(bf_readers[i].furthest-44)/(bl_rate*4.0):0.0);
        api->log(line);
    }
}
static void tick(void) {
    log_readers();
    if(ticks%150==75&&__atomic_load_n(&on,__ATOMIC_RELAXED)) {   /* round 14: does Bluetooth audio keep coming when no deck is set to it? */
        static uint64_t last_packets;uint64_t p=bw_packets;
        if(p!=last_packets||ticks%1500==75) {
            char line[120];snprintf(line,sizeof line,"bt audio: packets=%llu last=%llu ms ago recorded=%u s",(unsigned long long)p,
                     bw_last_us?(unsigned long long)((bw_now_us()-bw_last_us)/1000u):0ull,(unsigned)(bl_written/(bl_rate*4u)));
            api->log(line);last_packets=p;
        }
    }
    {   /* takes effect with the next recording (a new LIVE-n.WAV) */
        static const unsigned seconds[4]={3600,1800,600,300};
        int l=api->setting_get("length",0);if(!bl_running)bf_claim_seconds=seconds[l>=0&&l<4?l:0];
    }
    bl_set(__atomic_load_n(&on,__ATOMIC_ACQUIRE)&&api->setting_get("live",0));
    if(installed&&bd_installed)bp_set(__atomic_load_n(&bl_enabled,__ATOMIC_ACQUIRE)!=0);   /* waveform hand-over every chunk */
    __atomic_store_n(&br_want,__atomic_load_n(&bl_enabled,__ATOMIC_ACQUIRE)&&api->setting_get("bttrack",1)?1:0,__ATOMIC_RELEASE);
    {
        unsigned q=api->setting_get("quick",0)?1u:0u;
        if(q!=__atomic_load_n(&bf_short_reads,__ATOMIC_RELAXED)) {
            __atomic_store_n(&bf_short_reads,q,__ATOMIC_RELAXED);api->log(q?"quick edge reads on":"quick edge reads off");
        }
    }
    uint64_t head=__atomic_load_n(&bw_head,__ATOMIC_ACQUIRE),ms=api->now_ms();
    if(ms-second_ms>=1000){second_ms=ms;draws_per_second=draws;draws=0;}
    if(head!=shown_head&&api->waveform_screen())api->redraw();
    if(ticks++%15==0)status();
}
static uint8_t bw_sqrt[256];                        /* 255*sqrt(i/255), filled in init */
static void draw(struct oqmod_canvas *c,int w,int h) {
    uint64_t head=__atomic_load_n(&bw_head,__ATOMIC_ACQUIRE);
    shown_head=head;draws++;
    api->fill(c,0,0,w,h,0x0c0c0c);
    int mid=h/2,half=h/2-2;
    api->fill(c,0,mid,w,1,0x303030);
    if(!head) {api->text(c,0,0,w,h,"BLUETOOTH: WAITING FOR AUDIO",14,0x808080);return;}
    int paused=bw_now_us()-bw_last_us>2000000u;
    for(int x=w-1;x>=0;x--) {                       /* one column per pixel, newest right */
        uint64_t back=(uint64_t)(w-1-x);
        if(back>=head||back>=BW_RING-64)break;
        struct bw_col col=bw_ring[(head-1-back)%BW_RING];
        int bar=bw_sqrt[col.peak]*half/255;if(!bar)continue;   /* loudness-like scale */
        uint32_t rgb=(uint32_t)col.r<<16|(uint32_t)col.g<<8|col.b;
        if(paused)rgb=rgb>>1&0x7f7f7f;
        api->fill(c,x,mid-bar,1,2*bar,rgb);
    }
    api->outline(c,0,0,w,h,1,paused?0x404040:0x2060ff);
    api->text(c,6,2,120,16,"BT",12,0xffffff);
}
static void enabled(int yes) {
    if(yes&&!installed) {
        if(!api->ep136_ok()) {api->set_status("Not available on this deck software.");return;}
        install();
    }
    __atomic_store_n(&on,yes&&hooks?1u:0u,__ATOMIC_RELEASE);
    if(yes)status();
    api->log(yes?"switched on":"switched off");
}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info) {
    if(!a||a->version<OQMOD_API_VERSION||a->size<sizeof *a)return 1;
    api=a;
    api->option("live","Live Bluetooth track for a deck: OQ_BTLIVE (test)",0);
    static const char *const lengths[]={"60 min","30 min","10 min","5 min"};
    api->choice("length","Live track length",lengths,4,0);
    api->option("bttrack","Bluetooth on deck 3/4 plays the waveform track",1);
    for(unsigned i=0,r=0;i<256;i++){while((r+1)*(r+1)<=i*255u)r++;bw_sqrt[i]=(uint8_t)r;}
    info->api_version=OQMOD_API_VERSION;
    info->name="Bluetooth Waveform (Experimental)";
    info->description="Draws Bluetooth audio as a live waveform while it plays.";
    info->version="1.6";
    info->x=156;info->y=690;info->w=924;info->h=52;
    info->tick=tick;info->draw=draw;info->enabled=enabled;
    return 0;
}

#ifndef OQ_BT_WAVE_H
#define OQ_BT_WAVE_H
/* Bluetooth Waveform: turns decoded Bluetooth PCM into waveform columns as it arrives.
 * Each column covers rate/BW_PER_SECOND frames (about 6.7 ms): its peak and its low /
 * mid / high energy (two one-pole filters at about 200 Hz and 2.5 kHz), drawn like the
 * RGB waveform (red low, green mid, blue high).
 * One producer (the decode thread, bw_feed) writes the ring and publishes bw_head; the
 * drawing side only reads columns behind bw_head. Packet timing is counted too: it sets
 * the buffer a live Bluetooth deck needs. */
#include <stdint.h>
#include <string.h>
#include <time.h>

#define BW_PER_SECOND 150u
#define BW_RING 2048u                    /* columns kept, about 13 s */
enum {BW_SBC=1,BW_AAC=2};
struct bw_col {uint8_t peak,r,g,b;};     /* peak 0..255 of full scale */

static struct bw_col bw_ring[BW_RING];
static uint64_t bw_head;                 /* columns written, monotonic */
/* decode-thread state */
static float bw_lp1,bw_lp2,bw_low,bw_mid,bw_high;
static unsigned bw_peak,bw_frames,bw_rate=44100;
/* packet statistics; read by the tick thread (a torn read only skews one status line) */
static uint64_t bw_packets,bw_last_us,bw_gap_max_us,bw_first_us;
static unsigned bw_codec;

static uint64_t bw_now_us(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;}
/* SBC frame header: sync 0x9c, then the rate in the top two bits of the next byte. */
static unsigned bw_sbc_rate(const unsigned char *frame,size_t n) {
    static const unsigned rates[4]={16000,32000,44100,48000};
    return frame&&n>=2&&frame[0]==0x9c?rates[frame[1]>>6]:0;
}
static uint8_t bw_scale(float v,float top) {float x=v*255.0f/top;return x>255.0f?255:(uint8_t)x;}
static void bw_emit(void) {
    float top=bw_low;if(bw_mid>top)top=bw_mid;if(bw_high>top)top=bw_high;
    struct bw_col c={(uint8_t)(bw_peak>=32640u?255u:bw_peak>>7),0,0,0};
    if(top>0){c.r=bw_scale(bw_low,top);c.g=bw_scale(bw_mid,top);c.b=bw_scale(bw_high,top);}
    bw_ring[bw_head%BW_RING]=c;
    __atomic_store_n(&bw_head,bw_head+1,__ATOMIC_RELEASE);
    bw_peak=bw_frames=0;bw_low=bw_mid=bw_high=0;
}
/* Decode thread: interleaved 16-bit stereo. Never blocks. */
static void bw_feed(unsigned codec,unsigned rate,const int16_t *pcm,size_t frames) {
    if(!pcm||!frames)return;
    uint64_t now=bw_now_us();
    if(bw_last_us&&now-bw_last_us>bw_gap_max_us&&now-bw_last_us<2000000u)bw_gap_max_us=now-bw_last_us;
    if(!bw_last_us||now-bw_last_us>=2000000u)bw_first_us=now,bw_packets=0,bw_gap_max_us=0;   /* a new run */
    bw_last_us=now;bw_packets++;bw_codec=codec;
    if(rate>=8000&&rate<=96000&&rate!=bw_rate)bw_rate=rate,bw_frames=0;
    /* one-pole low passes: a = 1-exp(-2*pi*f/rate), for 44.1 kHz; close enough at 48 */
    const float a1=0.0281f,a2=0.300f;
    unsigned per=bw_rate/BW_PER_SECOND;
    for(size_t i=0;i<frames;i++) {
        int m=(pcm[2*i]+pcm[2*i+1])/2;
        unsigned mag=(unsigned)(m<0?-m:m);if(mag>bw_peak)bw_peak=mag;
        float x=(float)m;
        bw_lp1+=a1*(x-bw_lp1);bw_lp2+=a2*(x-bw_lp2);
        float lo=bw_lp1,mi=bw_lp2-bw_lp1,hi=x-bw_lp2;
        bw_low+=lo<0?-lo:lo;bw_mid+=mi<0?-mi:mi;bw_high+=hi<0?-hi:hi;
        if(++bw_frames>=per)bw_emit();
    }
}
#endif

#ifndef OQ_BT_LIVE_H
#define OQ_BT_LIVE_H
/* Live Bluetooth track for a deck (R105). The Bluetooth audio, as it arrives, is written
 * to <USB>/OQ_BTLIVE_DATA/live.raw (16-bit stereo PCM), and bt_fuse.h shows it to the deck
 * as <USB>/OQ_BTLIVE/LIVE-<n>.WAV: an hour long from the start, with reads that wait for
 * the audio. n counts up across recordings (OQ_BTLIVE_DATA/number.txt) so EP136, which
 * caches a track's length (R104 on hardware), always sees a new track.
 * Threads: the decode thread only copies into a ring (bl_push); the writer thread appends
 * to live.raw every 100 ms and publishes how much is there (bf_recorded); the FUSE thread
 * answers the deck's reads. Switching off ends the recording and unmounts. */
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "bt_fuse.h"

#define BL_RING (4u << 20)               /* about 24 s of 44.1 kHz stereo 16-bit */
static unsigned char bl_ring[BL_RING];
static uint64_t bl_head,bl_tail,bl_written,bl_drops;
static unsigned bl_enabled,bl_running,bl_fuse_running,bl_rate=44100,bl_open_failed,bl_fuse_failed,bl_number;
static char bl_root[260];                /* the settings USB; a test may set it */

/* The settings USB, from where this mod was loaded: <USB>/MODS/<folder>/mod.so. */
static int bl_find_root(void) {
    if(bl_root[0])return 1;
    FILE *f=fopen("/proc/self/maps","r");if(!f)return 0;
    char line[512];uintptr_t me=(uintptr_t)&bl_find_root;int ok=0;
    while(fgets(line,sizeof line,f)) {
        unsigned long lo,hi;char *path=strchr(line,'/');
        if(sscanf(line,"%lx-%lx",&lo,&hi)!=2||me<lo||me>=hi||!path)continue;
        path[strcspn(path,"\n")]=0;
        for(int up=0;up<3;up++){char *s=strrchr(path,'/');if(!s)break;*s=0;}   /* mod.so, <folder>, MODS */
        if(path[0]){snprintf(bl_root,sizeof bl_root,"%.250s",path);ok=1;}
        break;
    }
    fclose(f);return ok;
}
static unsigned bl_next_number(const char *data_dir) {
    char p[320];snprintf(p,sizeof p,"%s/number.txt",data_dir);
    unsigned n=0;FILE *f=fopen(p,"r");if(f){if(fscanf(f,"%u",&n)!=1)n=0;fclose(f);}
    n++;f=fopen(p,"w");if(f){fprintf(f,"%u\n",n);fclose(f);}
    return n;
}
/* Decode thread: never blocks. */
static void bl_push(const int16_t *pcm,size_t frames,unsigned rate) {
    if(!__atomic_load_n(&bl_enabled,__ATOMIC_ACQUIRE)||!pcm||!frames)return;
    if(rate>=8000&&rate<=96000)bl_rate=rate;
    size_t bytes=frames*4;uint64_t head=bl_head,tail=__atomic_load_n(&bl_tail,__ATOMIC_ACQUIRE);
    if(bytes>BL_RING/4||head-tail+bytes>BL_RING){__atomic_fetch_add(&bl_drops,1,__ATOMIC_RELAXED);return;}
    size_t off=(size_t)(head%BL_RING),first=bytes<BL_RING-off?bytes:BL_RING-off;
    memcpy(bl_ring+off,pcm,first);memcpy(bl_ring,(const unsigned char *)pcm+first,bytes-first);
    __atomic_store_n(&bl_head,head+bytes,__ATOMIC_RELEASE);
}
static void *bl_fuse_thread(void *unused) {
    (void)unused;
    while(__atomic_load_n(&bl_enabled,__ATOMIC_ACQUIRE)&&bf_mounted)bf_serve(5);   /* parked reads re-checked every 5 ms */
    bf_stop();
    __atomic_store_n(&bl_fuse_running,0,__ATOMIC_RELEASE);
    return 0;
}
static void *bl_thread(void *unused) {
    (void)unused;
    FILE *out=0;
    while(__atomic_load_n(&bl_enabled,__ATOMIC_ACQUIRE)) {
        uint64_t head=__atomic_load_n(&bl_head,__ATOMIC_ACQUIRE),tail=bl_tail;
        if(!out&&head>tail) {   /* the first audio: a new recording, a new track for the deck */
            if(!bl_find_root()){bl_open_failed=1;break;}
            char data_dir[300],backing[320],mount_dir[300];
            snprintf(data_dir,sizeof data_dir,"%s/OQ_BTLIVE_DATA",bl_root);mkdir(data_dir,0777);
            snprintf(backing,sizeof backing,"%s/live.raw",data_dir);
            snprintf(mount_dir,sizeof mount_dir,"%s/OQ_BTLIVE",bl_root);mkdir(mount_dir,0777);
            out=fopen(backing,"wb");
            if(!out){bl_open_failed=1;break;}
            bl_written=0;bl_number=bl_next_number(data_dir);
            if(bf_start(mount_dir,backing,bl_number,bl_rate)) {
                __atomic_store_n(&bl_fuse_running,1,__ATOMIC_RELEASE);
                pthread_t t;if(pthread_create(&t,0,bl_fuse_thread,0)){bf_stop();__atomic_store_n(&bl_fuse_running,0,__ATOMIC_RELEASE);bl_fuse_failed=1;}
                else pthread_detach(t);
            } else bl_fuse_failed=1;
        }
        while(out&&head>tail) {
            size_t off=(size_t)(tail%BL_RING),n=(size_t)(head-tail);if(n>BL_RING-off)n=BL_RING-off;
            if(fwrite(bl_ring+off,1,n,out)!=n){bl_open_failed=1;break;}
            tail+=n;__atomic_store_n(&bl_tail,tail,__ATOMIC_RELEASE);bl_written+=n;
        }
        if(out){fflush(out);__atomic_store_n(&bf_recorded,bl_written,__ATOMIC_RELEASE);}
        struct timespec pause={0,10000000};nanosleep(&pause,0);   /* 10 ms: a parked read of the deck gets new audio soon (round 8) */
    }
    if(out)fclose(out);
    for(int i=0;i<100&&__atomic_load_n(&bl_fuse_running,__ATOMIC_ACQUIRE);i++){struct timespec t={0,20000000};nanosleep(&t,0);}
    __atomic_store_n(&bl_tail,__atomic_load_n(&bl_head,__ATOMIC_ACQUIRE),__ATOMIC_RELEASE);
    __atomic_store_n(&bl_running,0,__ATOMIC_RELEASE);
    return 0;
}
static void bl_set(int on) {
    __atomic_store_n(&bl_enabled,on?1u:0u,__ATOMIC_RELEASE);
    if(on&&!__atomic_load_n(&bl_running,__ATOMIC_ACQUIRE)) {
        bl_open_failed=bl_fuse_failed=0;__atomic_store_n(&bl_running,1,__ATOMIC_RELEASE);
        pthread_t t;if(pthread_create(&t,0,bl_thread,0))__atomic_store_n(&bl_running,0,__ATOMIC_RELEASE);else pthread_detach(t);
    }
}
#endif

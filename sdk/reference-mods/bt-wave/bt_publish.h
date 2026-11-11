#ifndef OQ_BT_PUBLISH_H
#define OQ_BT_PUBLISH_H
/* Frequent waveform updates for the live track (round 12 on hardware). EP136's beat
 * analyser (BeatAnalyzer thread) works through a track in chunks of about 2 s of audio and
 * hands the waveforms it has so far (overviews, detailed) to the screen only when its chunk
 * counter is 4 mod 25:
 *   0x141c924..0x141c944  w0 = w25 % 25; cmp w0, #4; b.ne 0x141c818 (skip the hand-over)
 * i.e. at 10 s and then every 50 s, which a live track that is analysed as it arrives shows
 * as 50 s jumps. (Round 12 caught the writes: pc 0x1418768 via 0x1417e68, called from the
 * hand-over block at 0x141cd80.) While the live track is recording, the b.ne is made a nop,
 * so every chunk is handed over: an update every ~2 s of audio. A normal track analysed
 * meanwhile is updated more often too, which is harmless. Off again when recording stops. */
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef BP_AT
#define BP_AT 0x141c944ul
#endif
#define BP_ORIG 0x54fff6a1u   /* b.ne 0x141c818 */
#define BP_NOP 0xd503201fu
static unsigned bp_state;      /* 0 stock, 1 every chunk, 2 refused (unexpected code) */
static int bp_write(uint32_t insn) {
    long pg=sysconf(_SC_PAGESIZE);if(pg<=0)pg=4096;
    uintptr_t base=BP_AT&~(uintptr_t)(pg-1);
    if(mprotect((void *)base,(size_t)pg,PROT_READ|PROT_WRITE|PROT_EXEC))return 0;
    __atomic_store_n((uint32_t *)BP_AT,insn,__ATOMIC_RELEASE);
    __builtin___clear_cache((char *)BP_AT,(char *)BP_AT+4);
    mprotect((void *)base,(size_t)pg,PROT_READ|PROT_EXEC);
    return 1;
}
/* want: 1 while the live track records. Call from tick(). */
static void bp_set(int want) {
    if(bp_state==2)return;
    uint32_t now=*(const volatile uint32_t *)BP_AT;
    if(want&&bp_state==0) {
        if(now!=BP_ORIG){bp_state=2;return;}
        if(bp_write(BP_NOP))bp_state=1;
    } else if(!want&&bp_state==1) {
        if(now==BP_NOP&&bp_write(BP_ORIG))bp_state=0;
    }
}
#endif

#ifndef OQ_BT_ROUTE_H
#define OQ_BT_ROUTE_H
/* Bluetooth on deck 3/4 plays the waveform track (round 15). Selecting Bluetooth for a deck
 * is one call: usecase::mixer::Channel slot 2 (vtable 0x232c5a0, fn 0x8b1018),
 * setInputSource(const InputSource &), first byte 3 = Bluetooth, 0 = the deck (round 14 on
 * hardware). While the option is on and the live track records, a Bluetooth choice is passed
 * on as "deck": the channel keeps playing the deck's player, which holds LIVE-n.WAV (loaded
 * by hand for now), a few seconds behind, with its waveform, cues and loops. The Bluetooth
 * audio still arrives and is recorded (round 14). Every other choice goes through as it is. */
#include <stdint.h>
#include <string.h>

#define BR_SLOT (0x232c5a0u+8u*2u)
#define BR_FN 0x8b1018u
static uintptr_t (*br_orig)(void *,const uint8_t *);
static unsigned br_installed,br_swapped,br_passed;
static volatile int br_want;      /* option on and recording */
static uintptr_t br_hook(void *self,const uint8_t *src) {
    if(src&&src[0]==3&&__atomic_load_n(&br_want,__ATOMIC_ACQUIRE)) {
        uint8_t copy[16];memcpy(copy,src,sizeof copy);copy[0]=0;   /* Bluetooth -> the deck */
        __atomic_fetch_add(&br_swapped,1,__ATOMIC_RELAXED);
        return br_orig(self,copy);
    }
    __atomic_fetch_add(&br_passed,1,__ATOMIC_RELAXED);
    return br_orig(self,src);
}
#endif

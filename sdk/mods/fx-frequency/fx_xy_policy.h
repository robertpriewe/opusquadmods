/* Experimental Y-only proxy, NOT native per-band DSP isolation. No activation. */
#ifndef OPUS_FX_XY_POLICY_H
#define OPUS_FX_XY_POLICY_H
#include <stdint.h>
#include <stddef.h>
struct fp_state {unsigned enabled,selection,known,on;uint64_t feedback_ms;};
enum {FP_DISABLED,FP_BOTH,FP_UNKNOWN,FP_OFF,FP_STALE,FP_TOUCH,FP_SAME,FP_CHANGED,FP_INVALID,FP_REASONS};
static inline unsigned fp_apply(const struct fp_state *p,uint64_t now,
                                 unsigned char *sub,size_t length) {
    if(!p||!sub||length!=9||p->selection<1||p->selection>3)return FP_INVALID;
    if(!p->enabled)return FP_DISABLED;
    if(p->selection==3)return FP_BOTH;
    if(!p->known)return FP_UNKNOWN;
    if(!p->on)return FP_OFF;
    if(now<p->feedback_ms||now-p->feedback_ms>500)return FP_STALE;
    if((sub[1]>>4)&3)return FP_TOUCH; /* Touch/HOLD/reserved own their XY. */
    /* Native Y increases downwards. Bottom=LPF (LOW); top=HPF (HIGH).
     * These are coordinate endpoints, not calibrated Hz/crossover bands. */
    unsigned y=p->selection==1?255:0;
    if(sub[3]==y)return FP_SAME;
    sub[3]=(unsigned char)y;return FP_CHANGED;
}
#endif

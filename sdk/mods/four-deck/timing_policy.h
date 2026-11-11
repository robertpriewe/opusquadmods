/* SPDX-License-Identifier: GPL-3.0-only
 * OPUS 1.32 port of the timing approach documented in AZ-MODS waveform.c,
 * Kyle-Hosman/xdj-az-mods commit 214a058b94120ec6c15070abddd6d04bbffef230.
 * All addresses below are independently checked against EP136, not AZ addresses.
 * No audio, transport, controller, or unrelated TimerTaskBox is accelerated.
 */
#define RT_START 0x1fb0650ul
#define RT_PAINT 0x210e730ul
#define RT_MAIN_VT 0x2994e70ul
#define RT_REPAINT_VT 0x2cb6740ul
#define RT_PLAYER_VT 0x291c048ul
#define RT_POSTPAINT_LR 0x210ec4cul
#define RT_NEXTSTEP_LR 0x1ce64d8ul
/* meow::TimerTaskBox::Impl (RTTI 0x29bfbf0). One of these, started at 33 ms,
 * drives gui::DisplayRefreshCycleTimer (vtable 0x25cf760), which dirties the
 * waveform rect: without it the frame clock runs at 60 but the waveform
 * content still only redraws 30 times a second (R83 finding; the AZ port's
 * TIMERTASKBOX_VT). */
#define RT_TASKBOX_VT 0x29bfea0ul
enum { RT_MODE_STOCK, RT_MODE_SMOOTH_30, RT_MODE_FAST_60 };
static int rt_mode;
static uintptr_t rt_main;
static uint64_t rt_due_ns,rt_frame_interval_bits;
static double rt_clock_before;
static int rt_period(void *self,int ms,uintptr_t caller,uint64_t now) {
    uintptr_t vt=*(uintptr_t *)self;
    int mode=__atomic_load_n(&rt_mode,__ATOMIC_ACQUIRE);
    int fast=mode==RT_MODE_FAST_60;
    if(vt==RT_MAIN_VT) {
        __atomic_store_n(&rt_main,(uintptr_t)self,__ATOMIC_RELEASE);
        double *fi=(double *)((char *)self+0x50);
        __atomic_store_n(&rt_frame_interval_bits,*(uint64_t *)fi,__ATOMIC_RELAXED);
        if(fast && *fi>12.0 && *fi<25.0) {
            rt_clock_before=*fi;*fi*=0.5;
        } else if(!fast && rt_clock_before>12.0 && *fi==rt_clock_before*0.5) {
            *fi=rt_clock_before;rt_clock_before=0;
        }
        if(fast && ms>=28 && ms<=40)ms=16;
    }
    if(fast && (vt==RT_PLAYER_VT||vt==RT_TASKBOX_VT) && ms>=28 && ms<=40)ms=16;
    if(vt==RT_REPAINT_VT) {
        /* Both repaint-manager re-arm branches use 33 ms, including a tail
         * branch whose return address is the outer caller. Match the exact
         * timer class/period rather than missing that second path by LR. */
        if(fast && ms==33)ms=16;
        if(fast && caller==RT_NEXTSTEP_LR && *(int *)((char *)self+0x10)>0 &&
           rt_due_ns>now-50000000ull && rt_due_ns<now+(uint64_t)ms*1000000ull)
            return (-2147483647-1); /* retain an already-earlier repaint deadline */
        rt_due_ns=now+(uint64_t)(ms>0?ms:1)*1000000ull;
    }
    return ms;
}
static int rt_rebase_paint_clock(int smooth,double paint_start_ms,double before,
                                 double last,double frame_now,double *stamp) {
    if(smooth && last!=before && last==frame_now && paint_start_ms<=last &&
       last-paint_start_ms<100.0) {
        *stamp=paint_start_ms;
        return 1;
    }
    return 0;
}


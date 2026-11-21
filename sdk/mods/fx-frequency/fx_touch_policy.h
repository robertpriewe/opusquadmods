/* FX Frequency 2.9: LOW / HIGH by emulating a finger on the Beat FX XY pad.
 *
 * Hardware facts (2026-11-15, stock XY pad capture, ECHO, report OPUS-RUNTIME-R124-148-1952):
 *  - a real touch changes only sub-frame byte1 bits 4-5 (mode 0 idle -> 1 touched), byte2 X and
 *    byte3 Y. Y grows downwards: bottom ~f8, top ~13. Bottom and top sound different (user).
 *  - the mixer turns X into the beat: step = X/32 (8 steps). Measured: step 0 = 1/16,
 *    step 5 = 1 beat, step 7 = 4 beats. The mixer reports the beat back in RX byte 0x5a
 *    ("format"): 1 = 1/16, 6 = 1 beat, 8 = 4 beats, i.e. step = format - 1.
 *  - RX byte 0x58 bit 0x20 = the mixer's Beat FX ON; a touch keeps it set, so only the physical
 *    BEAT FX ON press (raw switch byte 0x0e bit 0x40) stops the emulation.
 * 2.9 (hardware 2.8, OPUS-RUNTIME-R124-237-1750): the mixer sends its status only about every
 *    0.47 s, so 2.8's 250 ms freshness rule dropped the touch ~2x a second (starts=128).
 *    Now the hold continues while the link is alive (frame within 2 s). While touched, the
 *    mixer reports the held beat, so BEAT button presses (raw switch byte 0x0c bits 0x02/0x08)
 *    briefly release the pad: the mixer applies the press itself and reports the new beat, then
 *    the hold resumes there.
 * 2.10 (hardware 2.9, OPUS-RUNTIME-R124-107-1954): the mixer sends frames only when something
 *    changes (gaps up to 14 s), so the 2 s alive rule stopped the hold after ~2 s (S 2 events).
 *    The engine now treats any valid link as alive.
 * 2.11 (hardware 2.10, OPUS-RUNTIME-R124-78-2203): BEAT < = raw switch byte 0x0e bit 0x20,
 *    BEAT > = 0x0e bit 0x10 (beat 5>4>3>2>1 and 1>..>7 with the effect off). With the pad held the
 *    mixer ignores them (press at 5559 reported 6 before and after), so the screen's beat number
 *    stayed. Now each press moves X one step (1/16 .. 4 beats) and the mixer reports that beat.
 *    The 2.10 release-on-any-button and follow rules are gone (0x0e etc. toggle by themselves).
 * Pure: no I/O, no globals; unit-tested in test_touch.c. */
#ifndef FX_TOUCH_POLICY_H
#define FX_TOUCH_POLICY_H
#include <stdint.h>

enum { FT_LOW=1, FT_HIGH=2, FT_ALL=3 };
enum { FT_PASS_DISABLED, FT_PASS_ALL, FT_PASS_STALE, FT_PASS_USER_TOUCH, FT_PASS_OFF, FT_PASS_BLOCKED,
       FT_PASS_BEAT_UNKNOWN, FT_RELEASED, FT_FORCED, FT_PASS_BEAT_CHANGE, FT_RESULTS };
#define FT_ALIVE_MS 2000u
#define FT_STEPS 8u             /* X steps: 1/16 1/8 1/4 1/2 3/4 1 2 4 */

struct ft_input {
    unsigned enabled, selection, strength;   /* strength 0 light, 1 medium, 2 strong */
    unsigned alive;                          /* a mixer frame arrived within FT_ALIVE_MS */
    unsigned new_frame;                      /* this call sees a mixer frame not seen before */
    unsigned on_edge; int beat_dir;             /* button went down in that new frame */
    unsigned rx_on;                          /* RX 0x58 & 0x20 */
    unsigned rx_beat;                        /* RX 0x5a */
    uint64_t now, frame_ms;                  /* now_ms(), arrival of the latest mixer frame */
};
struct ft_state {
    unsigned forcing, blocked, step;
    unsigned starts, press_stops, moves;
};

static inline int ft_step_for_beat(unsigned format){
    if(format>=1&&format<=8)return (int)format-1;
    if(format==9||format==10)return 7;       /* 8 / 16 beats: the pad's longest step (4 beats) */
    return -1;
}
static inline unsigned ft_x_for_step(unsigned step){return step*32u+16u;}
static inline unsigned ft_y(unsigned selection,unsigned strength){
    static const unsigned char low[3]={0xc0,0xe0,0xf8},high[3]={0x40,0x20,0x08};
    if(strength>2)strength=1;
    return selection==FT_LOW?low[strength]:high[strength];
}

/* Called for every Beat FX sub-frame after the native serializer filled it (sub = 9 bytes).
 * Changes sub only when it returns FT_FORCED. */
static inline unsigned ft_apply(struct ft_state *s,const struct ft_input *in,unsigned char *sub){
    unsigned on_edge=in->new_frame&&in->on_edge;
    int beat_dir=in->new_frame?in->beat_dir:0;
    if(!in->enabled){s->forcing=s->blocked=0;return FT_PASS_DISABLED;}
    if(in->selection!=FT_LOW&&in->selection!=FT_HIGH){s->forcing=s->blocked=0;return FT_PASS_ALL;}
    if(!in->alive){s->forcing=0;return FT_PASS_STALE;}
    if((sub[1]>>4)&3){s->forcing=s->blocked=0;return FT_PASS_USER_TOUCH;}   /* the user's finger wins */
    if(s->forcing){
        if(on_edge){s->forcing=0;s->blocked=1;s->press_stops++;return FT_RELEASED;}
        /* BEAT < / >: the mixer ignores them while the pad is held, so move the finger along X */
        if(beat_dir<0&&s->step>0){s->step--;s->moves++;}
        else if(beat_dir>0&&s->step<FT_STEPS-1){s->step++;s->moves++;}
    } else {
        if(on_edge)s->blocked=0;                               /* a new press after a stop re-arms */
        if(s->blocked){if(!in->rx_on)s->blocked=0;return FT_PASS_BLOCKED;}
        if(!in->rx_on)return FT_PASS_OFF;
        int step=ft_step_for_beat(in->rx_beat);
        if(step<0)return FT_PASS_BEAT_UNKNOWN;
        s->step=(unsigned)step;s->forcing=1;s->starts++;
    }
    sub[1]=(unsigned char)((sub[1]&~0x30u)|0x10u);
    sub[2]=(unsigned char)ft_x_for_step(s->step);
    sub[3]=(unsigned char)ft_y(in->selection,in->strength);
    return FT_FORCED;
}
#endif

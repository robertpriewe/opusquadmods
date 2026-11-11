/* The FX Frequency selection (LOW, HIGH or both = ALL); the XY filter proxy applies it. */
#ifndef OPUS_FREQUENCY_H
#define OPUS_FREQUENCY_H
enum { FX_LOW=1, FX_HIGH=2, FX_BOTH=3 };
/* LOW | ALL | HIGH radio buttons: a tap selects that range; tapping the lit LOW or
 * HIGH again goes back to ALL. Anything unknown reads as ALL. */
static inline unsigned fx_frequency_select(unsigned current,unsigned button) {
    if(current<FX_LOW || current>FX_BOTH)current=FX_BOTH;
    if(button<FX_LOW || button>FX_BOTH)return current;
    return button==current&&button!=FX_BOTH?FX_BOTH:button;
}
/* What the selected range does, for the box heading. */
static inline const char *fx_frequency_caption(unsigned current) {
    return current==FX_LOW?"FX: LOWS ONLY":current==FX_HIGH?"FX: HIGHS ONLY":"FX: FULL RANGE";
}
#endif

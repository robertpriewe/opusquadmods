#ifndef OPUS_FX_ENCODER_POLICY_H
#define OPUS_FX_ENCODER_POLICY_H
#include <stdint.h>
/* Normalize signed 16-bit absolute-position wrap for navigation only. */
static inline int ft_movement(int difference) {
    unsigned d=(unsigned)difference&65535u;
    return d<32768u?(int)d:(int)d-65536;
}
struct ft_divider {int remainder,pending;};
static inline void ft_divide_by(struct ft_divider *s,int counts,unsigned threshold) {
    if(!counts)return;
    if(threshold<1)threshold=1;
    if(threshold>5)threshold=5;
    if((counts>0&&s->remainder<0)||(counts<0&&s->remainder>0))s->remainder=0;
    int total=s->remainder+counts;
    s->pending+=total/(int)threshold;s->remainder=total%(int)threshold;
    if(s->pending>24)s->pending=24;
    if(s->pending< -24)s->pending= -24;
}
static inline void ft_divide(struct ft_divider *s,int counts) {ft_divide_by(s,counts,3);}
static inline unsigned ft_clock_target(unsigned current,unsigned mic,int direction) {
    const unsigned order[6]={mic,3,4,5,6,7};unsigned index=0;
    for(unsigned i=0;i<6;i++)if(current==order[i])index=i;
    return order[((int)index+(direction>0?1:5))%6];
}
#endif

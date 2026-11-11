#ifndef OQ_BROWSER_INPUT_H
#define OQ_BROWSER_INPUT_H
#include <stdint.h>
enum bi_kind { BI_OTHER,BI_SELECTOR,BI_PREVIOUS,BI_NEXT };
struct bi_key {enum bi_kind kind;unsigned side,deck,index;};
/* Only IDs corroborated by stock factories / ordered R25 and R26 captures. */
static inline struct bi_key bi_decode(uint64_t id) {
    for(unsigned side=0;side<2;side++) {
        if(id==0x0d174d4fd081f300ull+side)return (struct bi_key){BI_SELECTOR,side,side+1,side};
        if(id==0x3030e6ac95bc4d00ull+side)return (struct bi_key){BI_SELECTOR,side,side+3,side+2};
        if(id==0x22b9b592e46a7e00ull+side)return (struct bi_key){BI_PREVIOUS,side,0,side+4};
        if(id==0xcdacccaccdd3fe00ull+side)return (struct bi_key){BI_NEXT,side,0,side+6};
    }
    return (struct bi_key){BI_OTHER,0,0,0};
}
struct bi_gesture {unsigned down,owned;};
struct bi_input {struct bi_gesture keys[8];};
/* Latch whether the ORIGINAL firmware callback must be suppressed. An exit or
 * arming transition between down/up cannot split ownership of the gesture.
 * Adapter invokes state actions only on changed edges, preserving stock when
 * unarmed. Original down forwarded => original release MUST be forwarded. */
static inline int bi_claim(struct bi_input *input,struct bi_key key,int down,int armed,int *changed) {
    *changed=0;
    if(key.kind==BI_OTHER||key.index>=8||(down!=0&&down!=1))return 0;
    struct bi_gesture *g=&input->keys[key.index];
    if(g->down==(unsigned)down)return g->owned;
    *changed=1;
    if(down) {g->down=1;g->owned=armed!=0;return g->owned;}
    int owner=g->owned;g->down=0;g->owned=0;return owner;
}
#endif

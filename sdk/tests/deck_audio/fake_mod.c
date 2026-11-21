/* A mod using the deck audio insert; built twice (MOD_A / MOD_B) as two .so files. */
extern void *fake_got;
#define OQ_DECK_AUDIO_SLOT ((uintptr_t)&fake_got)
#include "oqmod_deck_audio.h"
#ifdef MOD_A
static void process(struct oq_deck_audio_block *b,void *u){(void)u;   /* deck 1 at half level, deck 2 x4 (clips) */
    for(unsigned i=0;i<b->frames;i++){b->left[0][i]*=0.5f;b->right[0][i]*=0.5f;b->left[1][i]*=4.0f;b->right[1][i]*=4.0f;}}
#define NAME(x) a_##x
#else
static void process(struct oq_deck_audio_block *b,void *u){(void)u;   /* deck 3 left := NaN, deck 4 += 0.25 */
    for(unsigned i=0;i<b->frames;i++){b->left[2][i]=0.0f/0.0f;b->left[3][i]+=0.25f;b->right[3][i]+=0.25f;}}
#define NAME(x) b_##x
#endif
__attribute__((visibility("default"))) int NAME(install)(void){return oq_deck_audio_install(process,0);}
__attribute__((visibility("default"))) void NAME(enable)(int on){oq_deck_audio_enable(on);}
__attribute__((visibility("default"))) void NAME(status)(struct oq_deck_audio_status *s){oq_deck_audio_status(s);}

/* Deck Tone (SDK example for oqmod_deck_audio.h): a bass and a treble shelf on all four
 * decks' audio, before the mixer.
 *
 * Shows the deck audio insert: install once in oqmod_init, switch with enabled(), keep the
 * audio callback small, hand new settings to it without locks. It changes the decks only:
 * LINE / PHONO / MIC and Beat FX come after this point in the mixer and are not affected. */
#include "oqmod_deck_audio.h"   /* first: it needs _GNU_SOURCE */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include "oqmod.h"

static const struct oqmod_api *api;

/* ---- RBJ shelving biquads ---- */
struct bq {float b0,b1,b2,a1,a2;};
struct dt_coef {struct bq low,high;int flat;};
static struct dt_coef coefs[2];static unsigned coef_cur;     /* written by tick, read by audio */
static float z[4][2][2][2];   /* deck, L/R, filter, state */
static struct bq shelf(double rate,double hz,double db,int high) {
    double A=pow(10.0,db/40.0),w=2.0*M_PI*hz/rate,c=cos(w),s=sin(w),al=s/2.0*sqrt(2.0),sq=2.0*sqrt(A)*al;
    double b0,b1,b2,a0,a1,a2;
    if(!high){b0=A*((A+1)-(A-1)*c+sq);b1=2*A*((A-1)-(A+1)*c);b2=A*((A+1)-(A-1)*c-sq);
              a0=(A+1)+(A-1)*c+sq;a1=-2*((A-1)+(A+1)*c);a2=(A+1)+(A-1)*c-sq;}
    else     {b0=A*((A+1)+(A-1)*c+sq);b1=-2*A*((A-1)+(A+1)*c);b2=A*((A+1)+(A-1)*c-sq);
              a0=(A+1)-(A-1)*c+sq;a1=2*((A-1)-(A+1)*c);a2=(A+1)-(A-1)*c-sq;}
    return (struct bq){(float)(b0/a0),(float)(b1/a0),(float)(b2/a0),(float)(a1/a0),(float)(a2/a0)};
}
static inline float run(const struct bq *q,float *st,float x) {   /* transposed direct form II */
    float y=q->b0*x+st[0];st[0]=q->b1*x-q->a1*y+st[1];st[1]=q->b2*x-q->a2*y;return y;
}
/* ---- audio thread ---- */
static void process(struct oq_deck_audio_block *b,void *user) {
    (void)user;
    const struct dt_coef *k=&coefs[__atomic_load_n(&coef_cur,__ATOMIC_ACQUIRE)];
    if(k->flat)return;
    for(unsigned d=0;d<OQ_DECK_AUDIO_DECKS;d++) {
        float *ch[2]={b->left[d],b->right[d]};
        for(unsigned c=0;c<2;c++)for(unsigned i=0;i<b->frames;i++)
            ch[c][i]=run(&k->high,z[d][c][1],run(&k->low,z[d][c][0],ch[c][i]));
    }
}
/* ---- settings (tick) ---- */
static const char *const steps[]={"-12 dB","-6 dB","-3 dB","0 dB","+3 dB","+6 dB"};
static const double step_db[]={-12,-6,-3,0,3,6};
static void tick(void) {
    static int last_bass=-1,last_treble=-1;
    int bass=api->setting_get("bass",3),treble=api->setting_get("treble",3);
    if(bass<0||bass>5)bass=3;
    if(treble<0||treble>5)treble=3;
    if(bass!=last_bass||treble!=last_treble) {
        unsigned next=!__atomic_load_n(&coef_cur,__ATOMIC_ACQUIRE);
        coefs[next].low=shelf(44100.0,150.0,step_db[bass],0);
        coefs[next].high=shelf(44100.0,6000.0,step_db[treble],1);
        coefs[next].flat=bass==3&&treble==3;
        __atomic_store_n(&coef_cur,next,__ATOMIC_RELEASE);
        last_bass=bass;last_treble=treble;
    }
    static unsigned n;if(n++%30)return;   /* status once a second */
    struct oq_deck_audio_status s;oq_deck_audio_status(&s);char text[200];
    if(!s.installed)snprintf(text,sizeof text,"Deck audio not available on this firmware.");
    else snprintf(text,sizeof text,"Deck audio: %u ch, %u Hz, %u blocks, slowest %u us",
        s.channels,s.sample_rate,s.processed,s.max_ns/1000u);
    api->set_status(text);
}
static void enabled(int on){oq_deck_audio_enable(on);}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info) {
    if(!a||a->version<OQMOD_API_VERSION||a->size<offsetof(struct oqmod_api,choice)+sizeof(void *))return 1;
    api=a;
    api->choice("bass","Bass (decks)",steps,6,3);
    api->choice("treble","Treble (decks)",steps,6,3);
    coefs[0].flat=1;
    int r=oq_deck_audio_install(process,0);
    if(r)api->log("deck-tone: deck audio insert not available");
    info->api_version=OQMOD_API_VERSION;
    info->name="Deck Tone";
    info->description="Bass and treble for the four decks' audio.\nLINE/PHONO/MIC and Beat FX are not affected.";
    info->version="1.0";
    info->tick=tick;info->enabled=enabled;
    return 0;
}

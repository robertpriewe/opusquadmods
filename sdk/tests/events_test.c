/* oqmod_events.h against a fake firmware: each handler fires once per change, the first
 * poll reports what is already there, a new track replaces the old one (with a pause if it
 * was playing), BPM jitter under 0.05 is ignored, near_end fires once per pass. */
#include <assert.h>
#include <stdio.h>
#include "oqmod_events.h"
static struct oqmod_deck decks[5];static int layout;
static int fake_deck(unsigned n,struct oqmod_deck *o){*o=decks[n];return decks[n].loaded;}
static int fake_layout(void){return layout;}
static int loaded[5],unloaded[5],played[5],paused[5],bpm[5],keys[5],ends[5],layouts;
static void on_loaded(unsigned d,const struct oqmod_deck *n){(void)n;loaded[d]++;}
static void on_unloaded(unsigned d){unloaded[d]++;}
static void on_play(unsigned d,const struct oqmod_deck *n){(void)n;played[d]++;}
static void on_pause(unsigned d,const struct oqmod_deck *n){(void)n;paused[d]++;}
static void on_bpm(unsigned d,uint32_t a,uint32_t b){(void)a;(void)b;bpm[d]++;}
static void on_key(unsigned d,const char *k){(void)k;keys[d]++;}
static void on_end(unsigned d,const struct oqmod_deck *n){(void)n;ends[d]++;}
static void on_layout(int a,int b){(void)a;(void)b;layouts++;}
int main(void){
    struct oqmod_api api;memset(&api,0,sizeof api);api.deck=fake_deck;api.layout=fake_layout;
    struct oq_events ev={on_loaded,on_unloaded,on_play,on_pause,on_bpm,on_key,on_end,20,on_layout};
    decks[2]=(struct oqmod_deck){.loaded=1,.playing=1,.length_ms=200000,.bpm_x100=12800};strcpy(decks[2].title,"A");strcpy(decks[2].key,"8A");
    oq_events_poll(&api,&ev);                       /* already loaded and playing */
    assert(loaded[2]==1&&played[2]==1&&keys[2]==0&&!loaded[1]);
    oq_events_poll(&api,&ev);assert(loaded[2]==1&&played[2]==1);   /* nothing changed */
    decks[2].bpm_x100=12803;oq_events_poll(&api,&ev);assert(bpm[2]==0);   /* jitter */
    decks[2].bpm_x100=12900;oq_events_poll(&api,&ev);assert(bpm[2]==1);
    strcpy(decks[2].key,"9A");oq_events_poll(&api,&ev);assert(keys[2]==1);
    decks[2].elapsed_ms=181000;oq_events_poll(&api,&ev);oq_events_poll(&api,&ev);assert(ends[2]==1);
    decks[2].playing=0;oq_events_poll(&api,&ev);assert(paused[2]==1);
    decks[2].playing=1;oq_events_poll(&api,&ev);assert(played[2]==2);
    strcpy(decks[2].title,"B");decks[2].elapsed_ms=0;oq_events_poll(&api,&ev);   /* a new track while playing */
    assert(loaded[2]==2&&paused[2]==2&&played[2]==3);
    decks[2].loaded=0;oq_events_poll(&api,&ev);assert(unloaded[2]==1&&paused[2]==3);
    layout=2;oq_events_poll(&api,&ev);assert(layouts==1);
    char p[64];assert(!oq_track_path(1,p,sizeof p)&&p[0]==0);   /* no firmware here: 0 */
    char dir[300];
#ifdef __linux__
    assert(oq_mod_dir(dir,sizeof dir)&&dir[0]=='/');
#else
    assert(!oq_mod_dir(dir,sizeof dir)); /* /proc/self/maps is a target-Linux facility */
#endif
    puts("PASS: SDK events: load/unload/play/pause/BPM/key/near-end/layout each once per change; first poll reports current state; unavailable native path safely refused; Linux mod-dir lookup tested on Linux.");
    return 0;
}

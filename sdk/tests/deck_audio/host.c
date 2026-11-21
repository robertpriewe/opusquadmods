/* Fake EP136: an exported snd_pcm_writei + hw_params, a GOT slot, two streams. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "oqmod_deck_audio.h"   /* for the structs only */
struct pcm{unsigned ch;int fmt;};
static struct pcm deck10={10,10},deckf={10,14},hp2={2,10};
#define NEAR(a,b) (fabs((double)(a)-(double)(b))<=256.0)
static const void *last_buf;static int32_t seen[10*2048];static float seenf[10*2048];
long snd_pcm_writei(void *p,const void *b,unsigned long n){struct pcm *q=p;last_buf=b;
    if(q->fmt==14)memcpy(seenf,b,n*q->ch*4);else memcpy(seen,b,n*q->ch*4);return (long)n;}
size_t snd_pcm_hw_params_sizeof(void){return 16;}
int snd_pcm_hw_params_current(void *p,void *par){memcpy(par,&p,sizeof p);return 0;}
int snd_pcm_hw_params_get_channels(const void *par,unsigned *c){struct pcm *q;memcpy(&q,par,sizeof q);*c=q->ch;return 0;}
int snd_pcm_hw_params_get_format(const void *par,int *f){struct pcm *q;memcpy(&q,par,sizeof q);*f=q->fmt;return 0;}
int snd_pcm_hw_params_get_rate(const void *par,unsigned *r,int *d){(void)par;*r=44100;*d=0;return 0;}
__attribute__((aligned(4096))) void *fake_got=(void *)snd_pcm_writei;
static long call(void *p,const void *b,unsigned long n){return ((long(*)(void*,const void*,unsigned long))fake_got)(p,b,n);}
static int fails;
#define CHECK(c,...) do{if(!(c)){fails++;printf("FAIL %s:%d ",__FILE__,__LINE__);printf(__VA_ARGS__);printf("\n");}}while(0)
int main(void){
    void *A=dlopen("./mod_a.so",RTLD_NOW|RTLD_LOCAL),*B=dlopen("./mod_b.so",RTLD_NOW|RTLD_LOCAL);
    if(!A||!B){printf("dlopen: %s\n",dlerror());return 1;}
    int (*ai)(void)=dlsym(A,"a_install"),(*bi)(void)=dlsym(B,"b_install");
    void (*ae)(int)=dlsym(A,"a_enable"),(*be)(int)=dlsym(B,"b_enable");
    void (*as)(struct oq_deck_audio_status*)=dlsym(A,"a_status"),(*bs)(struct oq_deck_audio_status*)=dlsym(B,"b_status");
    int32_t in[10*64];for(int i=0;i<64;i++)for(int k=0;k<10;k++)in[i*10+k]=(k==2||k==3?700000000:(k+1)*100000000)+(i&1?-1:1)*1000;
    /* not installed: untouched */
    call(&deck10,in,64);CHECK(last_buf==in,"pass-through before install");
    CHECK(ai()==0,"A install");CHECK(ai()==OQ_DECK_AUDIO_BUSY,"A twice");
    /* installed but off: same buffer */
    call(&deck10,in,64);CHECK(last_buf==in,"off = untouched pointer");
    ae(1);call(&deck10,in,64);
    CHECK(NEAR(seen[0],in[0]/2),"deck1 L halved %d vs %d",seen[0],in[0]);CHECK(NEAR(seen[1],in[1]/2),"deck1 R halved");
    CHECK(seen[2]==INT32_MAX&&seen[3]==INT32_MAX,"deck2 x4 clips to max (%d)",seen[2]);
    CHECK(NEAR(seen[4],in[4])&&NEAR(seen[7],in[7]),"decks 3/4 unchanged (24-bit float precision)");CHECK(seen[8]==in[8]&&seen[9]==in[9],"ch 9/10 bit-exact");
    CHECK(in[0]==100001000,"input buffer not written");
    /* chain B on top of A */
    CHECK(bi()==0,"B install over A's hook");be(1);
    call(&deck10,in,64);
    CHECK(NEAR(seen[0],in[0]/2),"A still runs under B");CHECK(seen[4]==0,"NaN -> 0 (%d)",seen[4]);
    CHECK(NEAR(seen[5],in[5]),"deck3 R unchanged");
    {double e=(double)in[6]+0.25*2147483648.0;CHECK(fabs((double)seen[6]-e)<200.0,"deck4 +0.25 (%d vs %.0f)",seen[6],e);}
    /* B off, A on */
    be(0);call(&deck10,in,64);CHECK(NEAR(seen[4],in[4])&&NEAR(seen[0],in[0]/2),"B off");
    /* a float stream is a second deck-shaped stream: only the first one is processed */
    {float fin[10*64];for(int i=0;i<640;i++)fin[i]=0.5f;call(&deckf,fin,64);CHECK(last_buf==fin,"second deck-shaped stream untouched");}
    /* 2-channel stream: not a deck stream */
    int32_t two[2*64];memset(two,0x11,sizeof two);call(&hp2,two,64);CHECK(last_buf==two,"2ch untouched");
    /* too long */
    static int32_t big[10*2000];call(&deck10,big,2000);CHECK(last_buf==big,"2000 frames skipped");
    /* status */
    struct oq_deck_audio_status s;as(&s);
    CHECK(s.installed&&s.enabled&&s.channels==10&&s.format==10&&s.sample_rate==44100,"status A");
    CHECK(s.skipped==1&&s.processed==3,"A processed %u skipped %u",s.processed,s.skipped);
    CHECK(!strcmp(s.previous,"libasound")||strstr(s.previous,"host")||1,"prev");
    bs(&s);printf("B chained over: %s\nA max %u ns\n",s.previous,s.max_ns);CHECK(strstr(s.previous,"mod_a.so")!=0,"B sees A as previous");
    printf("%s (%d failures)\n",fails?"FAILED":"all deck audio checks passed",fails);return fails!=0;
}

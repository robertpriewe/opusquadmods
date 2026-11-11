/* Exercise the actual extracted PCM injection, without native hooks, files,
 * decoder workers or a player. Original PCM and stem buffers are fixtures. */
#include "../mods/stems/mod.c"
#include <assert.h>
static float pcm[NDECK][NSTEM][512*2];
static unsigned original_reads;
static Pos32 fake_page(void *self,Float2 *out,const Pos32 *pos,long long count){
    (void)self;original_reads++;for(long long i=0;i<count;i++){out[i].L=0.05f;out[i].R=-0.07f;}
    Pos32 r=*pos;long long start;memcpy(&start,pos->b+POS_SAMPLE_OFF,8);start+=count;memcpy(r.b+POS_SAMPLE_OFF,&start,8);r.b[0]=0x9a;return r;
}
static void check(unsigned deck,unsigned mask,int enabled,int original){
    Float2 out[64];Pos32 pos={0};long long start=32;memcpy(pos.b+POS_SAMPLE_OFF,&start,8);
    g_tls_deck=(int)deck;atomic_store(&g_enable,enabled);atomic_store(&g_dk[deck].ctrl,0x80u|mask);
    g_em_prev[deck]=mask;g_orig_prev[deck]=mask==15;Pos32 r=pbread_hook(NULL,out,&pos,64);
    assert(r.b[0]==0x9a);long long end;memcpy(&end,r.b+POS_SAMPLE_OFF,8);assert(end==96);
    float sum=0;for(unsigned k=0;k<4;k++)if(mask>>k&1)sum+=(float)((deck+1)*(k+1))*0.1f;
    for(unsigned i=0;i<64;i++){
        assert(fabsf(out[i].L-(original?0.05f:sum))<0.00001f);
        assert(fabsf(out[i].R-(original?-0.07f:-sum))<0.00001f);
    }
}
int main(void){
    g_orig_pbread=fake_page;atomic_store(&g_inject_on,1);
    for(unsigned d=0;d<4;d++){
        g_dk[d].stem_frames=512;
        for(unsigned k=0;k<4;k++){
            for(unsigned i=0;i<512;i++){pcm[d][k][i*2]=(float)((d+1)*(k+1))*0.1f;pcm[d][k][i*2+1]=-pcm[d][k][i*2];}
            g_dk[d].stem[k]=pcm[d][k];
        }
        check(d,1,1,0);check(d,6,1,0);check(d,0,1,0);check(d,15,1,1);check(d,1,0,1);
    }
    /* A constructor missed before USB mount is resolved from the live unit's
     * independently validated deck field, not first-render arrival order. */
    assert(ep147_addr_init(EPB_OPUS132));atomic_store(&g_djpu_vt,DJPU_VTABLE);
    for(unsigned d=0;d<4;d++){
        unsigned char *unit=calloc(1,0x200),*sub=calloc(1,0x2000);assert(unit&&sub);
        *(uintptr_t *)unit=DJPU_VTABLE;*(uintptr_t *)(unit+DJPU_SUB_OFF)=(uintptr_t)sub;*(unsigned *)(sub+DJPU_NO_OFF)=d;
        assert(ap_deck_of(unit)==(int)d&&deck_slot(unit)==(int)d);
        atomic_store(&g_deck_self[STEM_SLOT[d]],NULL);free(sub);free(unit);
    }
    assert(original_reads==20);
    puts("EXTRACTED_STEMS_FOUR_DECK_PCM_MASKS_ALL_ON_ORIGINAL_OFF_PASSTHROUGH_LATE_IDENTITY_PASSED");return 0;
}

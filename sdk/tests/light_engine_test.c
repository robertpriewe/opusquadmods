/* Exercise the real extracted AZ theme + OPUS adapter, no fixed-address hooks. */
#include <assert.h>
#include "../mods/light-theme/mod.c"
static unsigned char received[0x30];
static int invalidates,glyphs;
static uint32_t glyph_color;
static uint32_t tile_expected;static unsigned tile_draws;
static void mock_tile(void *g,void **img,const void *xf,int fill){
    (void)g;(void)xf;assert(!fill);
    unsigned char *ipd=*img;uint32_t *pixels=*(uint32_t **)(ipd+DM_SPD_DATA);
    assert(pixels[0]==tile_expected);assert(pixels[1]==0xffff7323);
    assert(!pixels[2]);tile_draws++;
}
static void mixer_tiles(void){
    uint32_t *pixels=malloc(244*150*4);assert(pixels);
    for(unsigned i=0;i<244*150;i++)pixels[i]=0xff232323;
    pixels[1]=0xffff7323;pixels[2]=0;
    unsigned char ipd[0x60]={0},state[0x80]={0};
    *(uintptr_t *)ipd=DM_SPD_VTABLE;*(int *)(ipd+DM_IPD_FORMAT)=2;
    *(int *)(ipd+DM_IPD_W)=244;*(int *)(ipd+DM_IPD_H)=150;
    *(int *)(ipd+DM_SPD_PSTR)=4;*(int *)(ipd+DM_SPD_LSTR)=244*4;
    *(uint32_t **)(ipd+DM_SPD_DATA)=pixels;
    uintptr_t ctx[]={DM_SWR_VTABLE,(uintptr_t)state};void *graphics=ctx,*image=ipd;
    state[DM_STATE_ONLYXLATE]=1;float xf[]={1,0,147,0,1,650};
    g_dm_orig_drawimg_t=mock_tile;tile_expected=opus_light_surface(pixels[0]);
    assert(dm_draw_mixer_tile(&graphics,&image,xf));assert(tile_draws==1);
    for(unsigned i=0;i<244*150;i++)assert(pixels[i]==(i==1?0xffff7323:i==2?0:0xff232323));
    assert(dm_draw_mixer_tile(&graphics,&image,xf));assert(tile_draws==2); /* cached copy */
    /* Input popovers can cause JUCE to relayout the bottom strip by a few px.
     * Keep identifying its generated tiles after the popup closes. */
    xf[5]=656;tile_expected=opus_light_surface(pixels[0]);
    assert(dm_draw_mixer_tile(&graphics,&image,xf));
    pixels[0]=0xff313131;tile_expected=opus_light_surface(pixels[0]);
    assert(dm_draw_mixer_tile(&graphics,&image,xf));assert(pixels[0]==0xff313131); /* in-place native refresh */
    xf[5]=100;assert(!dm_draw_mixer_tile(&graphics,&image,xf)); /* waveform excluded */
    xf[5]=650;*(int *)(ipd+DM_IPD_W)=243;assert(!dm_draw_mixer_tile(&graphics,&image,xf));
    *(int *)(ipd+DM_IPD_W)=145;*(int *)(ipd+DM_IPD_H)=55;xf[5]=745;
    assert(dm_draw_mixer_tile(&graphics,&image,xf)); /* selector strip after popover relayout */
    free(pixels);
}
static uintptr_t mock_grid(void *gm,void *a,int b,int c,int d,int e,float f) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;
    unsigned char *p=gm;
    uint32_t *pixels=*(uint32_t **)(p+0x30);
    uint32_t bg=*(uint32_t *)(p+0x0c);
    pixels[0]=bg;pixels[1]=0xff999999;pixels[2]=0xffff0000;pixels[3]=bg;
    return 0x12345678u;
}
static void mock_fill(void *ctx,const void *fill) {(void)ctx;memcpy(received,fill,sizeof received);}
static void mock_glyph(void *ctx,int glyph,const void *xf) {
    (void)glyph;(void)xf;glyphs++;
    unsigned char *s=*(unsigned char **)((unsigned char *)ctx+8);
    glyph_color=*(uint32_t *)(s+0x30);
}
static void invalidate(void *p) {(void)p;invalidates++;}
int main(void) {
    ready=1;
    uint32_t colors[]={0xff007de1,0xffff7323,0xffff0000,0xff0055ff,0xff17be98};
    assert(!opus_light_set(0));
    for(unsigned i=0;i<sizeof colors/sizeof colors[0];i++)
        assert(opus_light_surface(colors[i])==colors[i]);
    assert(opus_light_set(1));
    assert(opus_light_surface(0xff000000)==0xffdcdcdc);
    assert(!opus_light_palette());
    assert(opus_light_surface(0xff404040)==0xffffffff);
    assert(opus_light_surface(0xffffffff)==0xff000000);
    for(unsigned i=0;i<sizeof colors/sizeof colors[0];i++)
        assert(opus_light_surface(colors[i])==colors[i]);
    for(unsigned a=0;a<256;a++)for(unsigned v=0;v<256;v++) {
        uint32_t c=(a<<24)|(v<<16)|(v<<8)|v;
        assert((opus_light_surface(c)>>24)==a);
    }
    unsigned char fill[0x30]={0},original[0x30];
    *(uint32_t *)fill=0xff232323;
    memcpy(original,fill,sizeof fill);g_dm_orig_setfill=mock_fill;
    dm_setfill_hook(NULL,fill);
    assert(!memcmp(fill,original,sizeof fill));
    assert(*(uint32_t *)received==opus_light_surface(0xff232323));
    /* Non-solid fills must pass through byte for byte. */
    *(void **)(fill+8)=(void *)1;dm_setfill_hook(NULL,fill);
    assert(!memcmp(fill,received,sizeof fill));
    *(void **)(fill+8)=NULL;*(void **)(fill+16)=(void *)1;
    dm_setfill_hook(NULL,fill);assert(!memcmp(fill,received,sizeof fill));
    /* Glyph ink changes for the call only; saved fill is never left modified. */
    unsigned char state[0x80]={0};uintptr_t context[2]={DM_SWR_VTABLE,(uintptr_t)state};
    *(uint32_t *)(state+0x30)=opus_light_surface(0xff3c3c3c);
    uint32_t keep=*(uint32_t *)(state+0x30);g_dm_orig_drawglyph=mock_glyph;
    dm_drawglyph_hook(context,1,NULL);
    assert(glyphs==1 && (glyph_color&0xffffff)<=0x606060);
    assert(*(uint32_t *)(state+0x30)==keep);
    /* Real OPUS array size, not AZ's 1220: ASAN guards both ends of this copy. */
    unsigned char *columns=calloc(DM_WF_COLS,DM_WF_COL_SIZE);
    assert(columns && DM_WF_COLS==1124);
    for(int i=0;i<DM_WF_COLS;i++) {
        columns[i*DM_WF_COL_SIZE]=0xff; /* blue native ink, light-only recolor */
        columns[i*DM_WF_COL_SIZE+3]=27; /* height, unchanged */
    }
    const unsigned char *mapped=dm_wf_columns(columns);
    assert(mapped!=columns);
    assert(mapped[0]==0xff && mapped[8]==0xdc);
    mapped=dm_wf3_columns(columns);
    assert(mapped[0]==0xff && mapped[4]==0xdc);
    assert(opus_light_palette_set(1));
    assert(opus_light_surface(0xff404040)==0xfff0f0f0);
    mapped=dm_wf_columns(columns);
    for(int i=0;i<DM_WF_COLS;i++) {
        assert(columns[i*DM_WF_COL_SIZE+8]==0);
        assert(mapped[i*DM_WF_COL_SIZE+8]==0xdc);
        assert(mapped[i*DM_WF_COL_SIZE]==0xe5 && mapped[i*DM_WF_COL_SIZE+3]==27);
    }
    mapped=dm_wf3_columns(columns);
    for(int i=0;i<DM_WF_COLS;i++)
        assert(mapped[i*DM_WF_COL_SIZE+4]==0xdc && mapped[i*DM_WF_COL_SIZE+3]==27);
    assert((dm_wf_ink(0xffffffffu)&0xffffffu)==0xd626af);
    assert((dm_wf_ink(0xffff9d00u)&0xff0000u)==0xff0000u);
    /* A mixed 3-band column yields distinct blue/red/magenta against gray. */
    memset(columns,0,DM_WF_COLS*DM_WF_COL_SIZE);
    columns[0]=255; columns[1]=85;
    columns[4]=0; columns[5]=157; columns[6]=255;
    columns[8]=255; columns[9]=255; columns[10]=255;
    mapped=dm_wf3_columns(columns);
    assert(mapped[0]>mapped[2] && mapped[2]<80);
    assert(mapped[6]>mapped[5] && mapped[5]<40);
    assert(mapped[10]>mapped[9] && mapped[8]>mapped[9]);
    assert(columns[5]==157 && columns[8]==255);
    assert(!opus_light_palette_set(0));
    assert(opus_light_surface(0xff404040)==0xffffffff);
    mapped=dm_wf3_columns(columns);
    assert(mapped[5]==157 && mapped[8]==0 && columns[8]==255);
    assert(opus_light_palette_set(1));
    /* Native ruler rendering remains palette-aware without reinstalling any
     * BPM/grid-edit or grid-density controls. Neutral ticks flip, red beats
     * stay red, and the temporary object background is restored. */
    unsigned char gm[0x50]={0};uint32_t pixels[8]={0xffdcdcdc,0xff999999,0xffff0000,0xffdcdcdc};
    *(uint32_t *)(gm+0xc)=0xff000000;
    *(void **)(gm+0x30)=pixels;*(int *)(gm+0x38)=2;*(int *)(gm+0x3c)=16;
    *(int *)(gm+0x40)=4;*(int *)(gm+0x44)=4;*(int *)(gm+0x48)=1;
    original_grid_light=mock_grid;
    assert(grid_lightmode(gm,NULL,0,0,0,0,0.0f)==0x12345678u);
    assert(*(uint32_t *)(gm+0xc)==0xff000000);
    assert(pixels[0]==opus_light_surface(0xff000000));
    assert(pixels[1]==0xff666666 && pixels[2]==0xffff0000);
    /* Dark mode is an exact native pass-through. */
    assert(!opus_light_set(0));
    memset(pixels,0,sizeof pixels);
    (void)grid_lightmode(gm,NULL,0,0,0,0,0.0f);
    assert(*(uint32_t *)(gm+0xc)==0xff000000 && pixels[0]==0xff000000);
    assert(pixels[1]==0xff999999 && pixels[2]==0xffff0000);
    assert(opus_light_set(1));
    mixer_tiles();
    /* Each cached component invalidates once per palette generation. */
    void (*vt[4])(void *)={0,0,0,invalidate};void *cache[]={vt};
    dm_scci_pre(cache);dm_scci_pre(cache);assert(invalidates==1);
    assert(!opus_light_set(0));dm_scci_pre(cache);dm_scci_pre(cache);
    assert(invalidates==2);
    assert(dm_wf_columns(columns)==columns && dm_wf3_columns(columns)==columns);
    free(columns);
    dm_setfill_hook(NULL,original);assert(!memcmp(received,original,sizeof received));
    ready=0;assert(!opus_light_set(1));
    puts("PASS: alpha preservation, semantic colors, native FillType nonmutation, gradient/image pass-through, readable temporary glyph ink, cache invalidation, exact dark pass-through, unavailable-hook guard.");
}

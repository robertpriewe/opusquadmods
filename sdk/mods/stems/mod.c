#define _GNU_SOURCE
/* GPL-3.0-only azstem engine and project UI in a resident USB add-on. */
#include "oqmod_native_feature.h"
#include "math_compat.h"
#include <pthread.h>
#include <string.h>
static const struct oq_native_option oq_feature_options[]={
};
static const struct oq_native_feature oq_feature={"stems","Stems","Per-deck VOCAL, INST, BASS and DRUMS controls.\nNeeds compatible pre-generated stem files on the USB.\nUSB-owned decoder, audio processing and touch controls.", 140};
#define OQ_NATIVE_CUSTOM_ENTRY
#define OQ_NATIVE_FEATURE_IMPLEMENTATION
#include "oqmod_native_feature.h"
static struct oqmod_stems_audio stems_audio_io;
static int stem_engine_published;
/* Upstream intentionally bounds these display/path copies with snprintf.
 * New cross compilers diagnose their truncation; keep the warning local. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
#include "native/azstem/azstem.c"
#pragma GCC diagnostic pop
#include "control.h"
static unsigned stem_open,stem_toggle,stem_avail,stem_bits,stem_cards;
static struct oqmod_stems_context stem_context;
static void stem_panels(void){
    struct oqmod_panel p[5]={{800,8,100,40,OQMOD_PANEL_TOUCH,0,0}};unsigned count=1;
    unsigned open=__atomic_load_n(&stem_open,__ATOMIC_ACQUIRE);
    if(open){
        static const unsigned row[2][4]={{1,2,0,3},{0,1,2,3}};
        int cards=stem_context.fourlane&&stem_context.layout==0&&stem_context.pitch<157;
        __atomic_store_n(&stem_cards,!!cards,__ATOMIC_RELEASE);
        for(unsigned d=0;d<4;d++){
            unsigned r=row[stem_context.order==1][d];
            if(cards){int h=((int)stem_context.pitch-46)/3-4;if(h<14)h=14;p[count++]=(struct oqmod_panel){5,70+(int)stem_context.pitch*(int)r+46,140,h,OQMOD_PANEL_TOUCH,0,0};}
            else{int four=stem_context.fourlane&&stem_context.layout==0;p[count++]=(struct oqmod_panel){156+(int)r*231,four?699:690,231,four?44:52,OQMOD_PANEL_TOUCH,0,0};}
        }
    }
    oq_feature_api->panels_set(p,count);
}
/* Stem Volume on the EQ (as on the XDJ-AZ): channel 3's HI/MID/LOW set deck 1's vocals,
 * instruments+bass and drums, channel 4's do deck 2's. Left = silent, 12 o'clock = full,
 * right = up to +6 dB (azstem's eq_knob_gain). It stands down for a deck while deck 3/4
 * has a track, since that channel is then a real deck channel. Needs R122 (mixer()). */
static int stem_eq_sent[2]={-2,-2};
static int stem_eq_supported(void){
    const struct oqmod_api *a=oq_feature_api;
    return a->size>=offsetof(struct oqmod_api,mixer)+sizeof a->mixer&&a->mixer;
}
static void stem_eq_release(void){
    for(int d=0;d<2;d++)if(stem_eq_sent[d]!=-1){azstem_stem_eq(d,-1,-1,-1);stem_eq_sent[d]=-1;}
}
static void stem_eq_tick(void){
    if(!stem_engine_published)return;
    if(!oq_feature_api->setting_get("stemeq",1)||!stem_eq_supported()){stem_eq_release();return;}
    struct oqmod_mixer m;m.size=sizeof m;
    if(!oq_feature_api->mixer(&m)||!m.valid){stem_eq_release();return;}
    for(int d=0;d<2;d++){
        int ch=d+2;
        if(azstem_deck_has_track(ch)){if(stem_eq_sent[d]!=-1){azstem_stem_eq(d,-1,-1,-1);stem_eq_sent[d]=-1;}continue;}
        /* the knobs top to bottom match the buttons left to right:
         * TRIM vocals, HI instruments, MID bass, LOW drums */
        int trim=(int)m.trim[ch];
        {   /* turning a muted stem's knob brings the stem back (a move of 20+ steps) */
            static int last[2][4]={{-1,-1,-1,-1},{-1,-1,-1,-1}};
            int now[4]={trim,(int)m.hi[ch],(int)m.mid[ch],(int)m.low[ch]};   /* vocals, inst, bass, drums */
            for(int k=0;k<4;k++){
                if(last[d][k]>=0&&(now[k]-last[d][k]>20||last[d][k]-now[k]>20)){
                    if(!(oqs_on[d]>>k&1u))__atomic_fetch_or(&stem_toggle,1u<<(d*4+k),__ATOMIC_ACQ_REL);
                    last[d][k]=now[k];
                }else if(last[d][k]<0)last[d][k]=now[k];
            }
        }
        int key=((int)m.hi[ch]<<20|(int)m.mid[ch]<<10|(int)m.low[ch])^(trim+1)*0x9e37;
        /* azstem_stem_eq4(deck, vocals, inst, drums, bass) */
        if(key!=stem_eq_sent[d]){azstem_stem_eq4(d,trim,m.hi[ch],m.low[ch],(int)m.mid[ch]);stem_eq_sent[d]=key;}
    }
}
/* ---- STEMS mark in the main browser's track list (R122+, mod-only) -------------------
 * EP136 builds each visible row with TrackListWidget slot 17 (0x1889630: widget, row,
 * const BrowseLineData&); BrowseLineData (vt 0x2587100) holds the title as a juce::String
 * at +0x28. For a track with stems the title is swapped, for that one call only, for
 * "【STEMS】 title": EP136 copies it into the row and draws it in its own font. The original
 * pointer is put back at once, so the library data never changes. Our strings are
 * juce::StringHolder-compatible (refCount, allocatedNumBytes, text) with a refCount JUCE
 * never brings to zero, so they are never freed. Found with Call Trace (hwtest-r122c). */
#include <dirent.h>
#include <stdlib.h>
#include <strings.h>
struct sb_holder {int ref;int pad;size_t bytes;char text[];};
#define SB_NAMES 2048
struct sb_list {unsigned count;char name[SB_NAMES][128];};
static struct sb_list *sb_lists[2];static struct sb_list *_Atomic sb_live;static unsigned sb_which;
static uint64_t sb_scanned;static unsigned sb_hooked,sb_marked;
/* diagnostics (addon_log): what the hooks see */
static unsigned sb_n_rows,sb_n_stems,sb_n_cells,sb_n_icons,sb_n_badctx;static uintptr_t sb_last_ctxvt;
static struct {int col,width,height;unsigned n;} sb_cols[8];
static void (*sb_row_orig)(void *,long,void *,void *,void *,void *,void *,void *);
/* Settings tick: every STEMS folder name on every mounted stick, published by pointer swap. */
static void sb_scan(void){
    uint64_t now=oq_feature_api->now_ms();
    if(sb_scanned&&now-sb_scanned<10000)return;
    sb_scanned=now;
    unsigned w=sb_which^1;
    if(!sb_lists[w]&&!(sb_lists[w]=calloc(1,sizeof *sb_lists[w])))return;
    struct sb_list *l=sb_lists[w];l->count=0;
    DIR *m=opendir("/media/usb");
    if(m){struct dirent *u;
        while((u=readdir(m))){
            if(u->d_name[0]=='.')continue;
            char dir[300];snprintf(dir,sizeof dir,"/media/usb/%s/STEMS",u->d_name);
            DIR *d=opendir(dir);if(!d)continue;
            struct dirent *e;
            while((e=readdir(d))&&l->count<SB_NAMES){
                size_t n=strlen(e->d_name);
                if(e->d_name[0]=='.'||n>=sizeof l->name[0])continue;
                memcpy(l->name[l->count++],e->d_name,n+1);
            }
            closedir(d);
        }
        closedir(m);}
    __atomic_store_n(&sb_live,l,__ATOMIC_RELEASE);sb_which=w;
}
/* Characters (UTF-8 code points) in s. */
static size_t sb_chars(const char *s){size_t c=0;for(;*s;s++)if(((unsigned char)*s&0xc0)!=0x80)c++;return c;}
/* 2.14: file names on the stick are cut short (44 characters seen), and the STEMS folder
 * copies the short name, while the browser row has the full title. A folder that looks cut
 * short ("Artist - Title start", or "Title start") matches a title that starts with its rest. */
#define SB_CUT_CHARS 40
#define SB_MIN_PART 4
static int sb_cut_match(const char *f,const char *title,size_t n){
    if(sb_chars(f)<SB_CUT_CHARS)return 0;
    size_t fn=strlen(f);
    if(fn>=SB_MIN_PART&&n>fn&&!strncasecmp(title,f,fn))return 1;             /* "Title start" */
    for(const char *p=strstr(f," - ");p;p=strstr(p+1," - ")){                  /* "Artist - Title start" */
        const char *rest=p+3;size_t rn=strlen(rest);
        if(rn>=SB_MIN_PART&&n>rn&&!strncasecmp(title,rest,rn))return 1;
    }
    return 0;
}
static int sb_has(const char *title){
    const struct sb_list *l=__atomic_load_n(&sb_live,__ATOMIC_ACQUIRE);
    if(!l||!title[0])return 0;
    size_t n=strlen(title);
    for(unsigned i=0;i<l->count;i++){const char *f=l->name[i];size_t fn=strlen(f);
        /* the folder is the file name: "Title", "Title - Artist" or "Artist - Title" (2.13) */
        if(!strcasecmp(f,title)||(!strncasecmp(f,title,n)&&!strncmp(f+n," - ",3))||
           (fn>n+3&&!strcasecmp(f+fn-n,title)&&!strncmp(f+fn-n-3," - ",3))||sb_cut_match(f,title,n))return 1;}
    return 0;
}
/* Which rows of which list widget have stems (GUI thread only): set as EP136 builds each row. */
#define SB_WIDGETS 4
#define SB_ROWS 4096
static struct {uintptr_t widget;unsigned char bits[SB_ROWS/8];} sb_rows[SB_WIDGETS];
static unsigned char *sb_bits(uintptr_t widget,int create){
    for(unsigned i=0;i<SB_WIDGETS;i++)if(sb_rows[i].widget==widget)return sb_rows[i].bits;
    if(!create)return NULL;
    for(unsigned i=0;i<SB_WIDGETS;i++)if(!sb_rows[i].widget){sb_rows[i].widget=widget;return sb_rows[i].bits;}
    static unsigned next;unsigned i=next++%SB_WIDGETS;sb_rows[i].widget=widget;memset(sb_rows[i].bits,0,sizeof sb_rows[i].bits);return sb_rows[i].bits;
}
static void sb_row(void *w,long row,void *item,void *a3,void *a4,void *a5,void *a6,void *a7){
    sb_row_orig(w,row,item,a3,a4,a5,a6,a7);
    sb_n_rows++;
    if(!w||row<0||row>=SB_ROWS)return;
    unsigned char *bits=sb_bits((uintptr_t)w,1);
    int has=0;
    if(item&&*(uintptr_t *)item==0x2587100ul){
        const char *text=*(const char *const *)((const char *)item+0x28);
        int ref=text?*(const int *)(text-16):0;
        if(text&&ref>0&&ref<1000000)has=sb_has(text);
    }
    if(has)sb_n_stems++;
    if(has)bits[row>>3]|=(unsigned char)(1u<<(row&7));else bits[row>>3]&=(unsigned char)~(1u<<(row&7));
}
/* The icon, drawn with EP136's own JUCE software renderer in the row's Track cell (over
 * the note symbol, next to the cover art), after EP136 painted the cell. Uses
 * LowLevelGraphicsSoftwareRenderer (vt 0x2c9ad90): 14 saveState, 15 restoreState,
 * 18 setFill(const FillType&), 21 fillRect(const Rectangle<int>&, bool) (JUCE 6, checked
 * slot by slot against EP136). State is saved and restored around the drawing. */
struct sb_fill {uint32_t argb,pad;void *gradient,*image;float t[6];};
static void (*sb_cell_orig)(void *,void *,long,long,long,long,long,long);
static void sb_rect(void *ctx,const uintptr_t *vt,int x,int y,int w,int h,uint32_t rgb){
    struct sb_fill f={0xff000000u|rgb,0,NULL,NULL,{1,0,0,0,1,0}};
    int r[4]={x,y,w,h};
    ((void (*)(void *,const struct sb_fill *))vt[18])(ctx,&f);
    ((void (*)(void *,const int *,int))vt[21])(ctx,r,0);
}
static void sb_icon(void *g,int x,int height){
    void *ctx=g?*(void **)g:NULL;
    if(!ctx||*(uintptr_t *)ctx!=0x2c9ad90ul){sb_n_badctx++;sb_last_ctxvt=ctx?*(uintptr_t *)ctx:0;return;}   /* only the software renderer */
    const uintptr_t *vt=*(const uintptr_t *const *)ctx;
    int bw=36,bh=26,y=(height-bh)/2;
    if(y<0)y=0;
    ((void (*)(void *))vt[14])(ctx);
    sb_rect(ctx,vt,x+1,y,bw-2,bh,0xffb000);          /* amber frame, corners clipped */
    sb_rect(ctx,vt,x,y+1,bw,bh-2,0xffb000);
    sb_rect(ctx,vt,x+2,y+2,bw-4,bh-4,0x101418);       /* dark inside */
    static const uint32_t col[4]={0x00e040,0xf03030,0xe000c0,0x2060ff};   /* VOCAL INST BASS DRUMS */
    static const int hgt[4]={14,18,10,16};
    for(int k=0;k<4;k++)sb_rect(ctx,vt,x+6+k*7,y+bh-4-hgt[k],5,hgt[k],col[k]);
    ((void (*)(void *))vt[15])(ctx);
    __atomic_fetch_add(&sb_marked,1,__ATOMIC_RELAXED);sb_n_icons++;
}
/* TableListBoxModel::paintRowBackground(g, row, width, height, selected) on the list's
 * model interface (this = widget + 0x30); found on hardware (R122, Stems 2.7 log: 1177 x
 * 52 rows). Cells (art, number, title) paint over the background afterwards, so the icon
 * goes in the free space just left of the BPM column (about 200 px from the right edge). */
static void sb_cell(void *model,void *g,long row,long width,long height,long selected,long a6,long a7){
    sb_cell_orig(model,g,row,width,height,selected,a6,a7);
    long column=width;
    sb_n_cells++;
    for(unsigned i=0;i<8;i++){if(sb_cols[i].n&&sb_cols[i].col!=(int)column)continue;
        sb_cols[i].col=(int)column;sb_cols[i].width=(int)width;sb_cols[i].height=(int)height;sb_cols[i].n++;break;}
    if(!oq_feature_on||!oq_feature_api->setting_get("browsemark",1)||row<0||row>=SB_ROWS||(int)width<300||(int)height<20)return;
    const unsigned char *bits=sb_bits((uintptr_t)model-0x30,0);
    /* full list (BPM and Key columns at the right): just left of BPM; the narrow track pane
     * of the two-pane views (Artist, Album, ...; 560 px, title to the edge): at its right end */
    int x=(int)width>=700?(int)width-250:(int)width-46;
    if(bits&&(bits[row>>3]>>(row&7)&1))sb_icon(g,x,(int)height);
}
static void sb_install(const struct oqmod_api *a){
    if(!a->ep136_ok||!a->ep136_ok()||!a->patch)return;
    sb_hooked=(a->patch(0x27cdef0u+17*8,0x1889630u,(void *)sb_row,(void **)&sb_row_orig)==1)
             |(a->patch(0x27ce070u+17*8,0x184d308u,(void *)sb_cell,(void **)&sb_cell_orig)==1)<<1;
    a->log(sb_hooked==3?"browser stems icon: row and cell hooks attached":"browser stems icon: a hook did not attach (icon off)");
}
static int stem_gain_drawn[4][4];
static void sb_report(void){
    static unsigned last;static uint64_t last_ms;
    uint64_t now=oq_feature_api->now_ms();
    unsigned key=sb_n_rows*31+sb_n_cells*7+sb_n_icons+sb_n_badctx*13;
    if(key==last||now-last_ms<5000)return;
    last=key;last_ms=now;
    const struct sb_list *l=__atomic_load_n(&sb_live,__ATOMIC_ACQUIRE);
    char s[400];int at=snprintf(s,sizeof s,"browser icon: hooks=%u folders=%u rows=%u stems=%u cells=%u icons=%u badctx=%u ctxvt=%lx cols",
        sb_hooked,l?l->count:0,sb_n_rows,sb_n_stems,sb_n_cells,sb_n_icons,sb_n_badctx,(unsigned long)sb_last_ctxvt);
    for(unsigned i=0;i<8&&sb_cols[i].n&&at<(int)sizeof s-40;i++)at+=snprintf(s+at,sizeof s-at," %d:%dx%d(%u)",sb_cols[i].col,sb_cols[i].width,sb_cols[i].height,sb_cols[i].n);
    oq_feature_api->log(s);
}
static void stem_tick(void){
    oq_feature_tick();if(!oq_feature_on)return;
    sb_scan();sb_report();
    stem_eq_tick();
    {   /* repaint the strip when a stem volume moved (the buttons show it) */
        int changed=0;
        for(int d=0;d<4;d++)for(int k=0;k<4;k++){int g=azstem_stem_gain(d,k);if(g!=stem_gain_drawn[d][k]){stem_gain_drawn[d][k]=g;changed=1;}}
        if(changed&&__atomic_load_n(&stem_open,__ATOMIC_ACQUIRE))oq_feature_api->redraw();
    }
    unsigned toggles=__atomic_exchange_n(&stem_toggle,0,__ATOMIC_ACQ_REL);
    static uint64_t checked;uint64_t now=oq_feature_api->now_ms();
    if(stem_engine_published&&(toggles||now-checked>=100)){
        checked=now;oqs_poll(1,toggles);oqs_engine_flag(1);
        unsigned avail=16,bits=0;for(unsigned d=0;d<4;d++){avail|=(unsigned)oqs_avail[d]<<d;bits|=oqs_on[d]<<(4*d);}
        __atomic_store_n(&stem_avail,avail,__ATOMIC_RELEASE);__atomic_store_n(&stem_bits,bits,__ATOMIC_RELEASE);
    }
    stem_context.size=sizeof stem_context;
    if(oq_feature_api->stems_ui(&stem_context,__atomic_load_n(&stem_avail,__ATOMIC_ACQUIRE),__atomic_load_n(&stem_bits,__ATOMIC_ACQUIRE),__atomic_load_n(&stem_open,__ATOMIC_ACQUIRE)))stem_panels();
}
static void stem_enabled(int on){
    oq_feature_enabled(on);
    if(!on){
        atomic_store_explicit(&g_enable,0,memory_order_release);
        __atomic_store_n(&stem_open,0,__ATOMIC_RELEASE);__atomic_store_n(&stem_toggle,0,__ATOMIC_RELEASE);
        if(stem_engine_published){oqs_poll(0,0);oqs_engine_flag(0);stem_eq_release();}
        oq_feature_api->panels_set(NULL,0);
    }
}
static void stem_draw(struct oqmod_canvas *c,unsigned slot,int w,int h){
    struct oqmod_surface s={0};oq_feature_api->surface(OQMOD_SURFACE_FX_FREQUENCY,&s);
    unsigned open=__atomic_load_n(&stem_open,__ATOMIC_ACQUIRE);
    if(!slot){
        oq_feature_api->fill(c,0,0,w,h,open?s.selected:s.background);oq_feature_api->outline(c,0,0,w,h,2,s.text);
        oq_feature_api->text(c,0,0,w,h,"STEMS",16,s.text);return;
    }
    if(slot>4)return;
    unsigned d=slot-1,avail=__atomic_load_n(&stem_avail,__ATOMIC_ACQUIRE),on=__atomic_load_n(&stem_bits,__ATOMIC_ACQUIRE);
    int cards=w==140,label=cards?0:31,bw=cards?35:50;
    oq_feature_api->fill(c,0,0,w,h,s.background);
    if(label){char deck[4];snprintf(deck,sizeof deck,"%u",d+1);oq_feature_api->text(c,0,0,label,h,deck,20,s.text);}
    if(!(avail>>d&1)){oq_feature_api->text(c,label,0,w-label,h,"NO STEMS",12,s.dim);return;}
    static const uint32_t colors[]={0x00e040,0xf03030,0xe000c0,0x2060ff};
    static const char *const names[]={"VOCAL","INST","BASS","DRUMS"},*const shortnames[]={"V","I","B","D"};
    for(unsigned k=0;k<4;k++){
        int x=label+(int)k*bw,lit=on>>(4*d+k)&1;uint32_t color=colors[k];
        int gain=azstem_stem_gain((int)d,(int)k),inner=bw-3;
        oq_feature_api->fill(c,x+1,2,inner,h-4,lit?(color>>2)&0x3f3f3f:s.background);
        if(lit&&gain>0){   /* volume: a solid fill from the left, full width at 100 % */
            int full=gain>1000?1000:gain,w2=(inner-2)*full/1000;
            if(w2>0)oq_feature_api->fill(c,x+2,3,w2,h-6,(color>>1)&0x7f7f7f);
        }
        oq_feature_api->outline(c,x+1,2,inner,h-4,gain>=999&&lit?2:1,lit?(gain>=999?0xffffff:color):s.divider);   /* white from 100 %: the centre click included */
        oq_feature_api->text(c,x,0,bw,h,cards?shortnames[k]:names[k],cards?14:11,lit?s.text:s.dim);
    }
}
static int stem_touch(unsigned slot,int x,int y){
    (void)y;if(!oq_feature_on)return 0;
    if(!slot){__atomic_fetch_xor(&stem_open,1,__ATOMIC_ACQ_REL);oq_feature_api->redraw();return 1;}
    if(slot>4)return 0;
    unsigned d=slot-1;
    int cards=__atomic_load_n(&stem_cards,__ATOMIC_ACQUIRE);
    int local=x-(cards?0:31),stem=local/(cards?35:50);
    if(local<0||stem<0||stem>3||!(__atomic_load_n(&stem_avail,__ATOMIC_ACQUIRE)>>d&1))return 0;
    __atomic_fetch_or(&stem_toggle,1u<<(d*4+(unsigned)stem),__ATOMIC_ACQ_REL);return 1;
}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->size<offsetof(struct oqmod_api,mixer)||!a->stems_audio||!a->stems_ui||!a->panels_set||!a->panel_renderer||!a->surface||!a->fill||!a->outline||!a->text||!a->now_ms||!a->native_engine)return 1;
    if(oq_feature_init(a,info)||!a->native_get("engine_external",0)||!a->native_engine())return 1;
    stems_audio_io.size=sizeof stems_audio_io;
    if(!a->stems_audio(&stems_audio_io)||!a->panel_renderer(stem_draw,stem_touch))return 1;
    if(stems_audio_io.ready()){
        oqs_engine_flag(0);atomic_store_explicit(&g_enable,0,memory_order_release);
        azstem_init();atomic_store_explicit(&g_enable,0,memory_order_release);stem_engine_published=stems_audio_io.publish();
    }
    if(!stem_engine_published)a->log("stems native audio guard unavailable; engine remains pass-through");
    a->option("stemeq","Stem volume on CH 3/4 EQ",1);
    a->option("browsemark","Mark tracks with stems in the browser",1);
    sb_install(a);
    info->enabled=stem_enabled;info->tick=stem_tick;info->version="3.0";return 0;
}

/* Extracted unchanged policy; source: host-r79-lab/src/hide_titles.h. */
#ifndef OQ_HIDE_TITLES_H
#define OQ_HIDE_TITLES_H
/* ---- Hide Track Names: an eye on each overview box's title ------------------
 * The track boxes under the waveforms are gui::waveform::EntireWaveform2DeckWidget
 * (2 DECK tab) and EntireWaveform4DeckWidget (2/4 DECK). Vtable slot 10 (0x1708358 /
 * 0x1714f10) is EP136's "title changed" call: for a non-empty title it shows the title
 * child (+480 / +440, the scrolling text) and sets its scroll mode; slot 11 (0x17082e0 /
 * 0x1714cb8) hides that child when the deck empties. So a box's title is hidden by keeping
 * that child invisible: re-hidden right after EP136's own show (slot 10) and checked each
 * WaveformView::update. Showing again restores it only if EP136 last asked for it.
 *
 * The state is per deck (bit d = Deck d+1), shared with both side-screen views. A box's
 * eye toggles the deck it shows: 2/4 DECK boxes by position (1 2 / 3 4), a 2 DECK box the
 * selected deck of its side (1/3 left, 2/4 right). Not saved: names show after a restart.
 * Message thread only (EP136 calls both slots and update there); the overlay gets the eye
 * rectangles through mm_publish_eyes and sends taps back through mm_take_eye_toggles. */
#include "title_blank.h"
#ifndef HT2_VT
#define HT2_VT 0x265bc60ul /* N3gui8waveform25EntireWaveform2DeckWidgetE */
#define HT4_VT 0x2662078ul /* N3gui8waveform25EntireWaveform4DeckWidgetE */
#endif
#define HT2_SHOW 0x1708358ul
#define HT2_CLEAR 0x17082e0ul
#define HT4_SHOW 0x1714f10ul
#define HT4_CLEAR 0x1714cb8ul
#define HT2_TITLE 480
#define HT4_TITLE 440
#define HT_EYE_W 30
#define HT_EYE_H 22

struct ht_box {void *self;int has,hidden;};
static struct ht_box ht_boxes[MM_EYES];
static unsigned ht_count,ht_hooks;
/* ht_deck_hidden (bit d = Deck d+1) is declared in opusmod.c: the side screens read it. */
static void (*ht_o_show2)(void *,const void *,int),(*ht_o_show4)(void *,const void *,int);
static void (*ht_o_clear2)(void *),(*ht_o_clear4)(void *);

/* the box's title child as a juce::Component, or 0 */
static char *ht_title_comp(void *self) {
    if(!ptr_ok(self))return 0;
    uintptr_t vt=*(uintptr_t *)self;
    int off=vt==HT2_VT?HT2_TITLE:vt==HT4_VT?HT4_TITLE:-1;
    if(off<0)return 0;
    void *child=*(void **)((char *)self+off);
    return ptr_ok(child)?as_component(child):0;
}
static struct ht_box *ht_box(void *self) {
    for(unsigned i=0;i<ht_count;i++)if(ht_boxes[i].self==self)return &ht_boxes[i];
    if(ht_count>=MM_EYES)return 0;
    ht_boxes[ht_count].self=self;return &ht_boxes[ht_count++];
}
/* Deck 0..3 a box shows, or -1: its place in the overview field (2/4 DECK), or its side's
 * selected deck (2 DECK). */
static int ht_box_deck(void *self) {
    if(!ptr_ok(self))return -1;
    uintptr_t vt=*(uintptr_t *)self;
    if(vt!=HT2_VT&&vt!=HT4_VT)return -1;
    const char *c=as_component(self),*parent=ptr_ok(c)?*(const char *const *)(c+COMP_PARENT):0;
    if(!ptr_ok(parent))return -1;
    const int *b=(const int *)(c+COMP_BOUNDS),*pb=(const int *)(parent+COMP_BOUNDS);
    int right=b[0]+b[2]/2>pb[2]/2,lower=b[1]+b[3]/2>pb[3]/2;
    if(vt==HT4_VT)return (lower?2:0)+(right?1:0);
    unsigned selected=__atomic_load_n(&ls_selected[right],__ATOMIC_ACQUIRE);
    return selected==(unsigned)(right?4:3)?(right?3:2):(right?1:0);
}
static int ht_hidden(const struct ht_box *b) {
    int deck=b?ht_box_deck(b->self):-1;
    return deck>=0&&(__atomic_load_n(&ht_deck_hidden,__ATOMIC_ACQUIRE)>>deck&1u);
}
static void ht_show_hook(void *self,const void *title,int scroll,void (*orig)(void *,const void *,int)) {
    orig(self,title,scroll);
    struct ht_box *b=ht_box(self);
    const char *text=ptr_ok(title)?*(const char *const *)title:0;
    if(b)b->has=text&&*text;
    char *c=ht_title_comp(self);
    if(c&&ht_hidden(b))comp_visible(c,0);
}
static void ht_show2(void *s,const void *t,int k) {ht_show_hook(s,t,k,ht_o_show2);}
static void ht_show4(void *s,const void *t,int k) {ht_show_hook(s,t,k,ht_o_show4);}
static void ht_clear2(void *s) {ht_o_clear2(s);struct ht_box *b=ht_box(s);if(b)b->has=0;}
static void ht_clear4(void *s) {ht_o_clear4(s);struct ht_box *b=ht_box(s);if(b)b->has=0;}

/* Screen rectangle of c; 0 if it or any parent is hidden (the box is not on screen). */
static int ht_screen(const char *c,int self_visible,int r[4]) {
    int x=0,y=0;const char *p=c;
    for(int depth=0;p&&depth<32;depth++) {
        if(!ptr_ok(p)||((p!=c||self_visible)&&!comp_visible_now(p)))return 0;
        x+=((const int *)(p+COMP_BOUNDS))[0];y+=((const int *)(p+COMP_BOUNDS))[1];
        p=*(const char *const *)(p+COMP_PARENT);
    }
    if(p)return 0;
    r[0]=x;r[1]=y;r[2]=((const int *)(c+COMP_BOUNDS))[2];r[3]=((const int *)(c+COMP_BOUNDS))[3];
    return 1;
}
/* R86: on hardware EP136 never called slot 10 through the vtable (boxes=0), so the boxes
 * are also found by walking the WaveformView's component tree. A child Component of a
 * box is its virtual-base subobject: its RTTI names the box class, and the box itself is
 * at the vtable's offset-to-top. Every 16th update; at most 256 components, depth 6. */
#define HT2_NAME "N3gui8waveform25EntireWaveform2DeckWidgetE"
#define HT4_NAME "N3gui8waveform25EntireWaveform4DeckWidgetE"
static unsigned ht_scans,ht_scan_found,ht_scan_nodes,ht_scan_named,ht_fallback_taps;
static void *ht_view;   /* the WaveformView of the latest update, for the fallback eyes */
static char ht_seen[4][64];   /* "Entire..." classes met anywhere in the tree, for the report */
/* R87: R86 walked down from the WaveformView and met no named component at all
 * (scan_found=0, nothing seen). Walk the whole window from its top-level parent
 * instead, wider and deeper, and record every overview-like class it meets. */
__attribute__((unused)) static void ht_scan(void *view) {
    static unsigned tick;
    ht_view=view;
    if(!ptr_ok(view)||(tick++&31))return;
    /* R88: start from the overview field (WaveformView +832, a Component pointer, the
     * same one the 4-deck layout moves); R87 started from as_component(view) and found a
     * single node with no children on hardware. Climb to the window from there. */
    char *top=*(char **)((char *)view+WFV_PREVIEWS);
    if(!ptr_ok(top))return;
    for(int up=0;up<16;up++){char *p=*(char **)(top+COMP_PARENT);if(!ptr_ok(p))break;top=p;}
    enum {HT_STACK=512,HT_VISIT=3000};
    static char *stack[HT_STACK];static unsigned char depth[HT_STACK];
    int sp=0,visited=0,named=0;
    stack[sp]=top;depth[sp++]=0;ht_scans++;
    while(sp>0&&visited<HT_VISIT) {
        char *c=stack[--sp];int d=depth[sp];visited++;
        if(!ptr_ok(c))continue;
        const char *name=rtti_name(c);
        if(name) {
            named++;
            if(strstr(name,"Entire")||strstr(name,"Overview"))for(unsigned i=0;i<4;i++) {
                if(!strcmp(ht_seen[i],name))break;
                if(!ht_seen[i][0]){snprintf(ht_seen[i],sizeof ht_seen[i],"%s",name);break;}
            }
            if(!strcmp(name,HT2_NAME)||!strcmp(name,HT4_NAME)) {
                uintptr_t vt=*(uintptr_t *)c;
                void *self=c+*(const long *)(vt-16);
                if(ptr_ok(self)&&(*(uintptr_t *)self==HT2_VT||*(uintptr_t *)self==HT4_VT)) {
                    unsigned before=ht_count;ht_box(self);if(ht_count!=before)ht_scan_found++;
                }
            }
        }
        if(d>=12)continue;
        char **kids=*(char ***)(c+0x40);int n=*(int *)(c+0x50);
        if(!ptr_ok(kids)||n<0||n>256)continue;
        for(int k=0;k<n&&sp<HT_STACK;k++){stack[sp]=kids[k];depth[sp++]=(unsigned char)(d+1);}
    }
    ht_scan_nodes=(unsigned)visited;ht_scan_named=(unsigned)named;
}
/* WaveformView::update, after EP136's own: apply taps, keep hidden titles hidden, and
 * place each on-screen box's eye just left of its title (over the note icon). */
static void ht_tick(int enabled) {
    unsigned toggles=mm_take_eye_toggles();   /* bit per box */
    unsigned hide=enabled?__atomic_load_n(&ht_deck_hidden,__ATOMIC_ACQUIRE):0,shut=0;
    int deck[MM_EYES];
    for(unsigned i=0;i<ht_count;i++) {
        deck[i]=ht_box_deck(ht_boxes[i].self);
        if(enabled&&deck[i]>=0&&(toggles>>i&1u))hide^=1u<<deck[i];
    }
    __atomic_store_n(&ht_deck_hidden,hide,__ATOMIC_RELEASE);
    struct mm_eye eyes[MM_EYES];memset(eyes,0,sizeof eyes);
    for(unsigned i=0;i<ht_count;i++) {
        struct ht_box *b=&ht_boxes[i];char *c=ht_title_comp(b->self);
        if(!c)continue;
        if(!b->hidden)b->has=comp_visible_now(c);   /* found by the scan: EP136 shows it while there is a title */
        int want=deck[i]>=0&&(hide>>deck[i]&1u);
        shut|=(unsigned)want<<i;
        if(want)comp_visible(c,0);
        else if(b->hidden&&b->has)comp_visible(c,1);
        b->hidden=want;
        int r[4];
        if(enabled&&b->has&&ht_screen(c,0,r)&&r[0]>=HT_EYE_W+4)
            eyes[i]=(struct mm_eye){r[0]-HT_EYE_W-4,r[1]+(r[3]-HT_EYE_H)/2,HT_EYE_W,HT_EYE_H};
    }
    /* R88 fallback, asked for after R87: while no box has been found, 2 DECK gets one eye
     * per box at the title's place (the field's left/right half, title at +85,+4), each
     * for its side's selected deck. The overlay masks a hidden title (slots 6 and 7). */
    if(enabled&&!ht_count&&ht_layout==1&&ptr_ok(ht_view)) {
        char *field=*(char **)((char *)ht_view+WFV_PREVIEWS);int r[4];
        if(ptr_ok(field)&&ht_screen(field,1,r)) {
            for(unsigned s=0;s<2;s++) {
                unsigned sel=__atomic_load_n(&ls_selected[s],__ATOMIC_ACQUIRE);
                int deck=sel>=1&&sel<=4?(int)sel-1:(int)s;
                if(toggles>>(6+s)&1u){hide^=1u<<deck;ht_fallback_taps++;}
                int bx=r[0]+(int)s*r[2]/2;
                eyes[6+s]=(struct mm_eye){bx+85-HT_EYE_W-4,r[1]+4+(30-HT_EYE_H)/2,HT_EYE_W,HT_EYE_H};
                if(hide>>deck&1u)shut|=1u<<(6+s);
            }
            __atomic_store_n(&ht_deck_hidden,hide,__ATOMIC_RELEASE);
        }
    }
    mm_publish_eyes(eyes,shut);
}
/* Constructor: slot patches only, each behind the exact stock function it replaces. */
static unsigned ht_install(void) {
    void **v2=(void **)HT2_VT,**v4=(void **)HT4_VT;
    if((uintptr_t)v2[10]==HT2_SHOW&&(uintptr_t)v2[11]==HT2_CLEAR&&
       patch_slot((uintptr_t)&v2[11],(void *)ht_clear2,(void **)&ht_o_clear2)==1&&
       patch_slot((uintptr_t)&v2[10],(void *)ht_show2,(void **)&ht_o_show2)==1)ht_hooks|=1;
    if((uintptr_t)v4[10]==HT4_SHOW&&(uintptr_t)v4[11]==HT4_CLEAR&&
       patch_slot((uintptr_t)&v4[11],(void *)ht_clear4,(void **)&ht_o_clear4)==1&&
       patch_slot((uintptr_t)&v4[10],(void *)ht_show4,(void **)&ht_o_show4)==1)ht_hooks|=2;
    return ht_hooks;
}
/* TX thread (ss_serialize, after EP136 wrote the side screen's text chunk, before the
 * frame CRC): blank each hidden deck's title in it. */
static void ht_side_filter(unsigned char *chunk,size_t n) {
    unsigned hidden=__atomic_load_n(&ht_deck_hidden,__ATOMIC_ACQUIRE);
    for(unsigned d=0;hidden&&d<4;d++) {
        char name[256];
        if(hidden>>d&1u&&dvh_deck_name(d,name,sizeof name))
            __atomic_fetch_add(&ht_side_blanked,tb_blank(chunk,n,name),__ATOMIC_RELAXED);
        if(hidden>>d&1u&&dvh_deck_artist(d,name,sizeof name))   /* R90: the artist too */
            __atomic_fetch_add(&ht_side_blanked,tb_blank(chunk,n,name),__ATOMIC_RELAXED);
    }
}
__attribute__((unused)) static void ht_report(FILE *out) {
    fprintf(out,"hide_titles fallback_taps=%u hooks=%u scans=%u nodes=%u named=%u scan_found=%u seen=%s,%s,%s,%s boxes=%u hidden_decks=0x%x stock_side_blanked=%u",ht_fallback_taps,ht_hooks,ht_scans,ht_scan_nodes,ht_scan_named,ht_scan_found,
            ht_seen[0],ht_seen[1],ht_seen[2],ht_seen[3],ht_count,
            __atomic_load_n(&ht_deck_hidden,__ATOMIC_ACQUIRE),__atomic_load_n(&ht_side_blanked,__ATOMIC_RELAXED));
    for(unsigned i=0;i<ht_count;i++)
        fprintf(out," %s:d%d:%d",ptr_ok(ht_boxes[i].self)&&*(uintptr_t *)ht_boxes[i].self==HT2_VT?"2deck":"4deck",ht_box_deck(ht_boxes[i].self)+1,ht_boxes[i].has);
    fputc('\n',out);
}
#endif

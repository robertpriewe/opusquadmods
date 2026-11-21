/* USB Deck Colors: the jog ring and the deck buttons of each side take the colour of the
 * USB the shown deck's track was loaded from (the colour set for that USB in rekordbox /
 * on the OPUS). Works for the OPUS's own USBs and for LINK USBs of other players, so in a
 * B2B you can tell whose stick each deck plays from.
 *
 * EP136 1.32 pieces (static RE 2026-11-12):
 *  - TrackLoader per deck (primary vt 0x2328f80) from the ObjectMap at 0x34f84a0, key
 *    0x34f401b2227e9700 + deck - 1 (lock 0x1581610 / unlock 0x5e6ec8 on 0x34f84c8). Both of
 *    its load tasks (0x8a8470 and the indexed 0x8a8250) copy the TrackID into the loader
 *    before loading: +0x108 trackid::MediaID (member word | partition << 32 | ...), +0x110
 *    the track id. So the mod only reads; it hooks nothing.
 *  - USB colour: 0xe65b10(StorageManager, &index, &{member word, partition byte}) is the
 *    getter DeviceIconColorProvider uses (override colour or the stick's profile colour);
 *    the index 0..8 selects an ARGB colour from the palette at 0x3531250 (+8 colours,
 *    +0x18 count). The member word works for link players as well (number lookup
 *    0x10bbf20 / 0x10bc098), so LINK sticks give their colour too.
 *  - The LEDs come from the SDK: led_state() gives what EP136 shows (0..16 per channel),
 *    led_set() replaces it for a short hold; the mod keeps EP136's brightness (selected /
 *    unselected deck button, ring on/off) and only changes the hue.
 *  - 1.1: the deck colour itself. EP136's DeckIndicatorController update 0x1c51f78(self)
 *    (deck 0..3 at self+0x248) asks the palette resolver 0x1c53178(self) for the deck colour
 *    (returned 0x00BBGGRR), then hands it to the indicator policy 0x1c4fb30, which applies
 *    brightness / end-of-track blinking and drives the LEDs. TYTO's Deck Highlight 1.5 reads
 *    the colour at 0x1c4fb30, so returning the USB colour from the resolver (only inside that
 *    deck's update) colours the LEDs and the on-screen highlight alike. Saved DeckColor
 *    settings are never touched. Both first instructions are 0xa9bc7bfd.
 *  - 1.2: option "Brighter selected deck button": the selected deck's button lights at full
 *    brightness and the other one at 3/16, instead of EP136's two levels (which look alike
 *    once both buttons have the USB colour). Deck number colours on the main screen are
 *    pre-drawn images per deck colour (DeckNumber4DeckComponent 0x1713544 swaps them);
 *    not changed yet. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "oqmod.h"
#include "oqmod_firmware.h"

static const struct oqmod_api *api;
static unsigned on, unsupported, numbers_ok;
static unsigned log_lines;
#define LOG(...) do{if(log_lines<120){char line_[200];snprintf(line_,sizeof line_,__VA_ARGS__);api->log(line_);log_lines++;}}while(0)

/* ---- EP136 reads ---------------------------------------------------------------------- */
static uintptr_t loader_for(unsigned deck) {
    if(deck<1||deck>4||!__atomic_load_n((unsigned char *)0x34f8498,__ATOMIC_ACQUIRE))return 0;
    void *mutex=(void *)0x34f84c8;
    if(!((unsigned (*)(void *))0x1581610)(mutex))return 0;
    const unsigned char *map=(const unsigned char *)0x34f84a0;
    int count=*(const int *)(map+24);uintptr_t iface=0;
    uint64_t key=0x34f401b2227e9700ull+deck-1;
    if(count>0&&count<=100000) {
        uintptr_t buckets=*(const uintptr_t *)(map+8);
        uintptr_t node=buckets?*(const uintptr_t *)(buckets+8*(key%(unsigned)count)):0;
        for(unsigned n=0;node&&n<1024;n++) {
            if(*(const uint64_t *)node==key){iface=*(const uintptr_t *)(node+8);break;}
            node=*(const uintptr_t *)(node+16);
        }
    }
    ((void (*)(void *))0x5e6ec8)(mutex);
    if(!iface||*(uintptr_t *)iface!=0x2329070ul)return 0;
    uintptr_t primary=iface-0xd8;
    return *(uintptr_t *)primary==0x2328f80ul?primary:0;
}
static unsigned char *storage_manager(void) {
    uintptr_t mapped[2]={0,((uint64_t (*)(const char *))0x612398)((const char *)0x2277b88)};
    if(!((unsigned (*)(void *,unsigned))0x7661f0)(mapped,0)||!mapped[0]||*(uintptr_t *)mapped[0]!=0x2470688ul)return 0;
    if(((uintptr_t *)0x2470688)[9]!=0xe65b10ul)return 0;
    return (unsigned char *)mapped[0];
}
/* index 0..8 and the colour 0xRRGGBB; returns 0 when EP136 knows no colour */
static int media_color(uint64_t media,int *index,uint32_t *rgb) {
    *index=-1;*rgb=0;
    unsigned char *manager=storage_manager();
    if(!manager||!(uint32_t)media)return 0;
    uint64_t key=media;int value=-1;
    if(!((unsigned (*)(void *,int *,const uint64_t *))0xe65b10)(manager,&value,&key)||value<0||value>8)return 0;
    const unsigned char *palette=(const unsigned char *)0x3531250;
    const uint32_t *colors=*(const uint32_t *const *)(palette+8);
    unsigned count=*(const unsigned *)(palette+0x18);
    if(!colors||count<9||count>32||(unsigned)value>=count)return 0;
    uint32_t c=colors[value];
    if((c>>24)!=255)return 0;
    *index=value;*rgb=c&0xffffffu;return 1;
}

/* ---- per deck ------------------------------------------------------------------------- */
struct deck_state {
    uint64_t media,pending;uint64_t pending_ms,color_ms;
    uint32_t sig_len;char sig_title[128];unsigned loaded;
    int index;uint32_t rgb;unsigned have;          /* have: colour known for media */
    char label[48];
};
static struct deck_state ds[4];

static void media_label(uint64_t media,char *out,size_t n) {
    uint32_t member=(uint32_t)media;
    if(member==1) {
        struct oqmod_usb_snapshot s;memset(&s,0,sizeof s);s.size=sizeof s;
        if(api->usb_sources&&api->usb_sources(&s))
            for(unsigned i=0;i<s.count&&i<OQMOD_USB_MAX;i++)
                if((s.sources[i].key&0xffffffffffull)==(media&0xffffffffffull)) {
                    snprintf(out,n,"USB %u %.30s",s.sources[i].port,s.sources[i].name);return;
                }
        snprintf(out,n,"USB");return;
    }
    if((member&0xff)==2){snprintf(out,n,"LINK %u",((member>>8)&0xff)+1);return;}
    snprintf(out,n,"media %08x",member);
}
static void deck_update(unsigned deck,uint64_t now) {
    struct deck_state *d=&ds[deck-1];
    struct oqmod_deck info;memset(&info,0,sizeof info);
    int has=api->deck(deck,&info);
    unsigned loaded=has&&info.loaded;
    uintptr_t loader=loader_for(deck);
    uint64_t media=loader?*(const uint64_t *)(loader+0x108):0;
    if(!loaded){d->loaded=0;d->have=0;d->media=0;d->pending=0;d->sig_title[0]=0;d->sig_len=0;return;}
    int sig_changed=!d->loaded||info.length_ms!=d->sig_len||strncmp(info.title,d->sig_title,sizeof d->sig_title-1);
    d->loaded=1;
    if(sig_changed){d->sig_len=info.length_ms;snprintf(d->sig_title,sizeof d->sig_title,"%s",info.title);}
    /* A new TrackID in the loader is taken when the deck shows a new track, or when the
     * deck is not playing (same track again from another USB). A load EP136 refused while
     * playing leaves the old colour. */
    if(media!=d->media) {
        if(media!=d->pending){d->pending=media;d->pending_ms=now;}
        if(sig_changed||!info.playing||!d->media) {
            d->media=media;d->pending=0;d->color_ms=0;
            media_label(media,d->label,sizeof d->label);
            LOG("usb-deck-colors: deck %u track from %s (media %016llx)",deck,d->label,(unsigned long long)media);
        } else if(now-d->pending_ms>10000)d->pending=0;
    }
    if(d->media&&(!d->color_ms||now-d->color_ms>1000)) {
        int index;uint32_t rgb;int ok=media_color(d->media,&index,&rgb);
        if(ok!=(int)d->have||index!=d->index||rgb!=d->rgb)
            LOG("usb-deck-colors: deck %u %s colour index %d rgb %06x%s",deck,d->label,index,rgb,ok?"":" (none)");
        d->have=(unsigned)ok;d->index=index;d->rgb=rgb;d->color_ms=now?now:1;
    }
}

/* ---- 1.1: the native deck colour --------------------------------------------------------- */
static unsigned native_override[4];        /* 0 = native colour, else 0x01000000 | 0x00BBGGRR */
static unsigned hooked,update_calls,resolver_calls,replaced;
static void (*update_original)(void *);
static uint32_t (*resolver_original)(void *);
static _Thread_local int indicator_deck=-1;
static void indicator_update(void *self){
    int previous=indicator_deck;
    int deck=self?*(const int *)((uintptr_t)self+0x248):-1;
    indicator_deck=deck>=0&&deck<4?deck:-1;
    __atomic_fetch_add(&update_calls,1,__ATOMIC_RELAXED);
    update_original(self);
    indicator_deck=previous;
}
static void *deck_item[4];               /* 1.3: each deck's Item<DeckColor> (the resolver's self) */
static uint32_t indicator_resolver(void *self){
    uint32_t base=resolver_original(self);
    __atomic_fetch_add(&resolver_calls,1,__ATOMIC_RELAXED);
    int deck=indicator_deck;
    if(deck<0)return base;
    __atomic_store_n(&deck_item[deck],self,__ATOMIC_RELEASE);
    unsigned o=__atomic_load_n(&native_override[deck],__ATOMIC_ACQUIRE);
    if(!o)return base;
    __atomic_fetch_add(&replaced,1,__ATOMIC_RELAXED);
    return o&0xffffffu;
}
static uint32_t to_bgr(uint32_t rgb){return ((rgb>>16)&255)|(rgb&0xff00)|((rgb&255)<<16);}
static uint32_t want_color(unsigned deck,int *use);
static void native_update(void) {
    int native=api->setting_get("native",1);
    for(unsigned deck=1;deck<=4;deck++){
        int use=0;uint32_t c=on&&native?want_color(deck,&use):0;
        unsigned o=use?0x01000000u|to_bgr(c):0u;
        if(o!=__atomic_load_n(&native_override[deck-1],__ATOMIC_ACQUIRE)){
            __atomic_store_n(&native_override[deck-1],o,__ATOMIC_RELEASE);
            LOG("usb-deck-colors: deck %u native colour %s %06x",deck,o?"USB":"stock",use?c:0);
        }
    }
}

/* ---- LEDs ----------------------------------------------------------------------------- */
static unsigned bright(uint32_t v) {   /* 0..16 */
    unsigned r=(v>>16)&255,g=(v>>8)&255,b=v&255,m=r>g?r:g;return m>b?m:b;
}
/* The USB colour at EP136's brightness for this LED (native 0..16 per channel). */
static uint32_t shade(uint32_t rgb,uint32_t native) {
    unsigned level=bright(native);if(level>16)level=16;
    unsigned r=(rgb>>16)&255,g=(rgb>>8)&255,b=rgb&255,m=bright(rgb);
    if(!m||!level)return 0;
    r=r*255u/m*level/16u;g=g*255u/m*level/16u;b=b*255u/m*level/16u;
    return r<<16|g<<8|b;
}
/* Which deck a side shows: the brighter of its two deck buttons (EP136 lights the selected one). */
static unsigned side_deck(unsigned side,uint32_t *led12,uint32_t *led34) {
    *led12=*led34=0;
    int have=api->led_state(side,OQMOD_LED_DECK_1_2,led12)&&api->led_state(side,OQMOD_LED_DECK_3_4,led34);
    /* 1.2: the deck software's own selection first (same source as Slip Mode). */
    if(api->deck_slip_read) {
        struct oqmod_slip_snapshot s;memset(&s,0,sizeof s);s.size=sizeof s;
        if(api->deck_slip_read(&s)) {
            unsigned sel=s.selected[side];
            if(sel>=1&&sel<=4&&((sel-1)&1)==side)return sel;
        }
    }
    if(!have)return side?2:1;
    return bright(*led34)>bright(*led12)?(side?4:3):(side?2:1);
}
static uint32_t want_color(unsigned deck,int *use) {
    const struct deck_state *d=&ds[deck-1];*use=0;
    if(!d->loaded||!d->media)return 0;
    int nocolor=api->setting_get("nocolor",0);
    if(d->have&&!(d->index==0&&nocolor==0)){*use=1;return d->rgb;}
    if(nocolor==1){*use=1;return 0xffffff;}
    return 0;
}
static void paint(void) {
    int ring=api->setting_get("ring",1),buttons=api->setting_get("buttons",1);
    static unsigned last_side_deck[2];static uint32_t last_native[2][3];
    for(unsigned side=0;side<2;side++) {
        uint32_t led12,led34,ring_native=0;
        unsigned deck=side_deck(side,&led12,&led34);
        api->led_state(side,OQMOD_LED_JOG_RING,&ring_native);
        if(deck!=last_side_deck[side]||((led12!=last_native[side][0]||led34!=last_native[side][1])&&log_lines<60)) {
            LOG("usb-deck-colors: side %u shows deck %u (deck buttons %06x / %06x, ring %06x)",side,deck,led12,led34,ring_native);
            last_side_deck[side]=deck;last_native[side][0]=led12;last_native[side][1]=led34;last_native[side][2]=ring_native;
        }
        int use;uint32_t c=want_color(deck,&use);
        if(ring&&use&&bright(ring_native))api->led_set(side,OQMOD_LED_JOG_RING,shade(c,ring_native),400);
        if(buttons) {
            unsigned low=side?2:1,high=side?4:3;   /* the 1/2 button shows deck 1 or 2, the 3/4 button deck 3 or 4 */
            /* 1.2: the selected deck's button at full brightness, the other one dimmed, so the
             * selected deck stands out even when both buttons have the same colour. A button
             * EP136 shows dark (blinking, nothing loaded) stays dark. */
            unsigned contrast=api->setting_get("selected",1)!=0;
            uint32_t full=0x101010u,dim=0x030303u;
            uint32_t level12=contrast?(deck==low?full:dim):led12,level34=contrast?(deck==high?full:dim):led34;
            uint32_t c2=want_color(low,&use);
            if(use&&bright(led12))api->led_set(side,OQMOD_LED_DECK_1_2,shade(c2,level12),400);
            c2=want_color(high,&use);
            if(use&&bright(led34))api->led_set(side,OQMOD_LED_DECK_3_4,shade(c2,level34),400);
        }
    }
}


/* ---- 1.2: the deck numbers on the main screen ---------------------------------------------
 * EP136 draws each deck number from pre-drawn images (12 deck colours x digits 1..4). Three
 * component classes hold them, all with the deck 0..3 at +200 and the colour 0..11 at +0x270;
 * their setter (self, colour) swaps the images:
 *   DeckNumberDeckInfoComponent vtable 0x264cb80, setter 0x16fcdc0 (first word 0xb9427002)
 *   DeckNumberComponent         vtable 0x2654580, setter 0x17060d0 (0xb9427002)
 *   DeckNumber4DeckComponent    vtable 0x265dec8, setter 0x1713544 (0xa9be7bfd)
 * EP136's DeckInfoWidget calls them from several places (setting change, deck updates), so
 * a one-off repaint loses (hardware 2026-11-15: 2465 changes in 3 minutes, still blue). The
 * setters are hooked: EP136's colour is remembered per deck and the USB colour passed on.
 * When the USB colour changes, the mod calls the setter itself from the waveform observer
 * (GUI thread, right after WaveformView::update) for the components it finds in the view
 * (JUCE child array +0x40, count +0x50). Nothing is saved. */
static const struct {uintptr_t vt,set;uint32_t first;} dn_class[3]={
    {0x264cb80u,0x16fcdc0u,0xb9427002u},{0x2654580u,0x17060d0u,0xb9427002u},{0x265dec8u,0x1713544u,0xa9be7bfdu}};
static void (*dn_orig[3])(void *,int);
static unsigned dn_hooked,dn_calls,dn_sets,dn_found;
static int dn_override[4]={-1,-1,-1,-1};   /* deck colour the numbers should show, -1 = EP136's */
static int dn_native[4]={-1,-1,-1,-1};     /* EP136's colour as last passed to a setter */
static int dn_applied[4]={-2,-2,-2,-2};    /* override the view was last brought to */
static void *dn_view;static uint64_t dn_last_ms;
static void dn_setter(unsigned k,void *c,int colour) {
    __atomic_fetch_add(&dn_calls,1,__ATOMIC_RELAXED);
    int deck=c?*(const int *)((uintptr_t)c+200):-1;
    if(deck>=0&&deck<4&&colour>=0&&colour<=11) {
        __atomic_store_n(&dn_native[deck],colour,__ATOMIC_RELEASE);
        int o=__atomic_load_n(&dn_override[deck],__ATOMIC_ACQUIRE);
        if(o>=0)colour=o;
    }
    dn_orig[k](c,colour);
}
static void dn_set0(void *c,int v){dn_setter(0,c,v);}
static void dn_set1(void *c,int v){dn_setter(1,c,v);}
static void dn_set2(void *c,int v){dn_setter(2,c,v);}
static void (*const dn_hook_fn[3])(void *,int)={dn_set0,dn_set1,dn_set2};
/* rekordbox USB colour 1..8 (Pink, Red, Orange, Yellow, Green, Aqua, Blue, Purple) -> deck
 * colour (Amber 0, Yellow 1, Red 2, Purple 3, Blue 4, Green 5, White 6, Light Yellow 7,
 * Pink 8, Coral Pink 9, Aqua 10, Teal 11). */
static const signed char dn_map[9]={-1,8,2,0,1,5,10,4,3};
static int dn_want(unsigned deck) {
    if(!on||unsupported||!api->setting_get("numbers",1))return -1;
    int use=0;want_color(deck,&use);
    if(!use)return -1;
    const struct deck_state *d=&ds[deck-1];
    if(d->have&&d->index>=1&&d->index<=8)return dn_map[d->index];
    return 6;                                   /* "USB without a color: White" */
}
static int dn_ptr_ok(const void *p){return p&&!((uintptr_t)p&7)&&(uintptr_t)p>0x10000;}
static void dn_walk(char *c,unsigned depth,unsigned *budget,char **found,unsigned *n) {
    if(!dn_ptr_ok(c)||depth>12||!*budget)return;
    (*budget)--;
    uintptr_t vt=*(const uintptr_t *)c;
    for(unsigned k=0;k<3;k++)if(vt==dn_class[k].vt){if(*n<24)found[(*n)++]=c;return;}
    char **kids=*(char ***)(c+0x40);int count=*(const int *)(c+0x50);
    if(!dn_ptr_ok(kids)||count<0||count>128)return;
    for(int i=0;i<count;i++)dn_walk(kids[i],depth+1,budget,found,n);
}
/* GUI thread: bring every deck number in the view to its wanted colour (through the hooked
 * setter, which substitutes the override). */
static void dn_refresh(void *view) {
    if(!view||!dn_hooked)return;
    char *found[24];unsigned n=0,budget=2000;
    dn_walk((char *)view,0,&budget,found,&n);
    dn_found=n;
    for(unsigned i=0;i<n;i++) {
        char *c=found[i];
        int deck=*(const int *)(c+200),cur=*(const int *)(c+0x270);
        if(deck<0||deck>3||cur<0||cur>11)continue;
        int o=__atomic_load_n(&dn_override[deck],__ATOMIC_ACQUIRE);
        int native=__atomic_load_n(&dn_native[deck],__ATOMIC_ACQUIRE);
        if(native<0&&(o<0||cur==o))continue;      /* nothing to change; EP136's colour not known yet */
        if(native<0){native=cur;__atomic_store_n(&dn_native[deck],cur,__ATOMIC_RELEASE);}
        int target=o>=0?o:native;
        if(cur==target)continue;
        uintptr_t vt=*(const uintptr_t *)c;
        for(unsigned k=0;k<3;k++)if(vt==dn_class[k].vt){
            ((void (*)(void *,int))dn_class[k].set)(c,native);   /* via the hook */
            dn_sets++;
            LOG("usb-deck-colors: deck %d number colour %d -> %d%s",deck+1,cur,target,o>=0?" (USB)":" (stock)");
        }
    }
}
static void dn_update_wanted(void) {
    for(unsigned deck=1;deck<=4;deck++)__atomic_store_n(&dn_override[deck-1],dn_want(deck),__ATOMIC_RELEASE);
}
static void dn_observe(const struct oqmod_waveform_context *context) {
    if(!context||!context->view)return;
    dn_view=context->view;
    uint64_t now=api->now_ms();
    int changed=0;
    for(unsigned i=0;i<4;i++)if(__atomic_load_n(&dn_override[i],__ATOMIC_ACQUIRE)!=dn_applied[i])changed=1;
    if(!changed&&now-dn_last_ms<2000)return;      /* the hooks keep it; recheck every 2 s */
    dn_last_ms=now;
    for(unsigned i=0;i<4;i++)dn_applied[i]=__atomic_load_n(&dn_override[i],__ATOMIC_ACQUIRE);
    dn_refresh(context->view);
}
static void dn_install(void) {
    for(unsigned k=0;k<3;k++) {
        if(*(const uint32_t *)dn_class[k].set!=dn_class[k].first){LOG("usb-deck-colors: number setter %#lx unexpected",(unsigned long)dn_class[k].set);continue;}
        if(api->hook(dn_class[k].set,dn_class[k].first,(void *)dn_hook_fn[k],(void **)&dn_orig[k])==1)dn_hooked|=1u<<k;
        else LOG("usb-deck-colors: number setter %#lx not hooked",(unsigned long)dn_class[k].set);
    }
}


/* ---- 1.3: the LOAD buttons --------------------------------------------------------------
 * The LOAD 1/3 and 2/4 lights show the selected deck's colour. indicator_control::browser::
 * Load's update 0x1c43768(self) (vtable 0x296fc78 slot 0, first word 0xa9ba7bfd) reads the
 * deck's Item<DeckColor> at self+80 (the same item the deck resolver 0x1c53178 gets), turns
 * the index into RGB with an inlined copy of the palette, and hands the 3 RGB bytes to the
 * LED output 0x1c551c8(out, pattern, rgb*, ...) (first word 0xa9bc7bfd; also used by the deck
 * indicators). The update is hooked to know which deck (item -> deck from the resolver hook),
 * the output to pass the USB colour instead, only inside a LOAD update. */
static void (*load_original)(void *);
static void (*ledout_original)(void *,void *,const unsigned char *,void *);
static _Thread_local int load_deck=-1;
static unsigned load_hooked,load_calls,load_replaced;
static unsigned load_rgb[4];              /* 0 = native, else 0x01000000 | 0xRRGGBB */
static void load_update(void *self){
    int previous=load_deck,deck=-1;
    void *item=self?*(void *const *)((uintptr_t)self+80):0;
    for(int d=0;d<4&&item;d++)if(__atomic_load_n(&deck_item[d],__ATOMIC_ACQUIRE)==item){deck=d;break;}
    load_deck=deck;
    __atomic_fetch_add(&load_calls,1,__ATOMIC_RELAXED);
    load_original(self);
    load_deck=previous;
}
static void led_out(void *out,void *pattern,const unsigned char *rgb,void *extra){
    int deck=load_deck;
    if(deck>=0&&rgb){
        unsigned o=__atomic_load_n(&load_rgb[deck],__ATOMIC_ACQUIRE);
        if(o){unsigned char c[4]={(unsigned char)(o>>16),(unsigned char)(o>>8),(unsigned char)o,0};
            __atomic_fetch_add(&load_replaced,1,__ATOMIC_RELAXED);
            ledout_original(out,pattern,c,extra);return;}
    }
    ledout_original(out,pattern,rgb,extra);
}
static void load_wanted(void){
    int use_load=api->setting_get("load",1);
    for(unsigned deck=1;deck<=4;deck++){
        int use=0;uint32_t c=on&&use_load?want_color(deck,&use):0;
        __atomic_store_n(&load_rgb[deck-1],use?0x01000000u|(c&0xffffffu):0u,__ATOMIC_RELEASE);
    }
}
static void load_install(void){
    if(!hooked)return;                     /* needs the resolver hook for item -> deck */
    if(*(const uint32_t *)0x1c43768!=0xa9ba7bfdu||*(const uint32_t *)0x1c551c8!=0xa9bc7bfdu){LOG("usb-deck-colors: LOAD light code not as expected");return;}
    if(api->hook(0x1c551c8,0xa9bc7bfd,(void *)led_out,(void **)&ledout_original)==1&&
       api->hook(0x1c43768,0xa9ba7bfd,(void *)load_update,(void **)&load_original)==1)load_hooked=1;
    else LOG("usb-deck-colors: LOAD light hooks refused");
}

/* ---- mod ------------------------------------------------------------------------------ */
static const char *const names[]={"","PINK","RED","ORANGE","YELLOW","GREEN","AQUA","BLUE","PURPLE"};
static void status(void) {
    char text[400];size_t at=0;text[0]=0;
    for(unsigned deck=1;deck<=4;deck++) {
        const struct deck_state *d=&ds[deck-1];
        if(!d->loaded||!d->media)continue;
        const char *cname=d->have&&d->index>0&&d->index<=8?names[d->index]:"no color";
        int n=snprintf(text+at,sizeof text-at,"%sDeck %u: %s, %s",at?"\n":"",deck,d->label,cname);
        if(n<0||(size_t)n>=sizeof text-at)break;
        at+=(size_t)n;
    }
    if(at<sizeof text-120){int n=snprintf(text+at,sizeof text-at,"%sDeck colour hook %u: updates %u, replaced %u\nDeck numbers: hooks %u, found %u, EP136 calls %u, refreshes %u%s\nLOAD lights: hook %u, updates %u, replaced %u",at?"\n":"",hooked,
        __atomic_load_n(&update_calls,__ATOMIC_RELAXED),__atomic_load_n(&replaced,__ATOMIC_RELAXED),dn_hooked,dn_found,__atomic_load_n(&dn_calls,__ATOMIC_RELAXED),dn_sets,numbers_ok?"":" (not available)",load_hooked,__atomic_load_n(&load_calls,__ATOMIC_RELAXED),__atomic_load_n(&load_replaced,__ATOMIC_RELAXED));if(n>0)at+=(size_t)n;}
    static char last[400];
    if(strcmp(last,text)){snprintf(last,sizeof last,"%s",text);api->set_status(text[0]?text:"No track loaded.");}
}
static void tick(void) {
    if(!on||unsupported)return;
    uint64_t now=api->now_ms();
    static uint64_t last_ms;
    if(now-last_ms<60)return;
    last_ms=now;
    for(unsigned deck=1;deck<=4;deck++)deck_update(deck,now);
    if(hooked)native_update();
    load_wanted();
    dn_update_wanted();
    paint();
    status();
}
static void enabled(int yes) {
    on=yes?1u:0u;
    for(unsigned i=0;i<4;i++)__atomic_store_n(&load_rgb[i],0u,__ATOMIC_RELEASE);
    if(!yes){for(unsigned i=0;i<4;i++)__atomic_store_n(&dn_override[i],-1,__ATOMIC_RELEASE);
        if(dn_view)dn_refresh(dn_view);}   /* message thread: numbers back to EP136's colours */
    memset(ds,0,sizeof ds);
    for(unsigned i=0;i<4;i++)__atomic_store_n(&native_override[i],0u,__ATOMIC_RELEASE);
}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info) {
    if(!a||a->version<OQMOD_API_VERSION)return 1;
    if(!OQMOD_HAS_R123(a))return oqmod_needs_firmware(a,info,"USB Deck Colors","1.0","R123");
    if(!a->led_state||!a->led_set)return 1;
    api=a;
    if(!api->ep136_ok()){unsupported=1;}
    static const char *const nocolor[]={"Deck color","White"};
    api->option("ring","Color the jog rings",1);
    api->option("buttons","Color the deck buttons",1);
    api->choice("nocolor","USB without a color",nocolor,2,0);
    api->option("native","Use as the deck color (Deck Highlight)",1);
    api->option("selected","Brighter selected deck button",1);
    api->option("numbers","Color the deck numbers on screen",1);
    api->option("load","Color the LOAD buttons",1);
    if(!unsupported&&api->hook){dn_install();numbers_ok=dn_hooked==7u&&api->waveform_observer&&api->waveform_observer(dn_observe)==1;}
    if(!unsupported&&api->hook&&*(const uint32_t *)0x1c51f78==0xa9bc7bfdu&&*(const uint32_t *)0x1c53178==0xa9bc7bfdu&&
       api->hook(0x1c53178,0xa9bc7bfd,(void *)indicator_resolver,(void **)&resolver_original)==1)
        hooked=api->hook(0x1c51f78,0xa9bc7bfd,(void *)indicator_update,(void **)&update_original)==1;
    if(!unsupported&&api->hook)load_install();
    info->api_version=OQMOD_API_VERSION;
    info->name="USB Deck Colors";
    info->description="Jog rings, deck buttons and the deck color show the color\nof the USB (local or LINK) each deck's track was loaded from.";
    info->version="1.3";
    info->tick=tick;
    info->enabled=enabled;
    if(unsupported)api->set_status("Needs the OPUS 1.32 deck software.");
    return 0;
}

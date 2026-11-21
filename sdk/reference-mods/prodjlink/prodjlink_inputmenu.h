/* 1.56 (look 1.59): PRO DJ LINK in the native input menu of channels 3 and 4.
 *
 * EP136 1.32 (Call Trace 1.3 run + photos, 2026-11-12): each mixer channel box is a
 * gui::mixer::ChannelWidget (vt 0x2800558, 246x564 inside the MixerAreaWidget; on screen the
 * boxes are channels 3, 1, 2, 4 from the left). Its input menu is a child
 * gui::mixer::ChannelPopupWidget (vt 0x27fb158) at (0,0) 244x293: "INPUT" title, then four
 * 220x50 entries at x 12, y 50/100/150/200 (DECK n, LINE, PHONO, Bluetooth), and an empty slot at
 * y 250..293 above the CROSSFADER part. While that menu is open the mod puts a 220x43 panel
 * "PRO DJ LINK" into the empty slot; a tap switches link3/link4 (prodjlink_inputs.h). */
#ifndef PRODJLINK_INPUTMENU_H
#define PRODJLINK_INPUTMENU_H
static uint64_t cw_obj[4];          /* ChannelWidget per channel 1..4 */
static uint64_t cw_child_vt(uint64_t obj,uint64_t vt) {
    uint64_t arr=0;int32_t n=0;
    if(!nt_peek(obj+0x40,(unsigned char *)&arr,8)||!nt_peek(obj+0x50,(unsigned char *)&n,4)||n<0||n>64)return 0;
    for(int i=0;i<n;i++){uint64_t k=0,kv=0;if(nt_peek(arr+8u*(unsigned)i,(unsigned char *)&k,8)&&k&&nt_peek(k,(unsigned char *)&kv,8)&&kv==vt)return k;}
    return 0;
}
/* After the memory scan: keep the candidates that have an input menu child, order by x. */
static void cw_assign(void) {
    uint64_t ok[4];int x[4];unsigned n=0;
    for(unsigned i=0;i<cw_ncand&&n<4;i++) {
        int32_t b[4];if(!cw_child_vt(cw_cand[i],0x27fb158ull)||!nt_peek(cw_cand[i]+0x20,(unsigned char *)b,16))continue;
        ok[n]=cw_cand[i];x[n]=b[0];n++;
    }
    if(n!=4){lp_log("inputs: %u channel boxes with an input menu (need 4)",n);return;}
    static const unsigned order[4]={3,1,2,4};   /* left to right on screen */
    for(unsigned r=0;r<4;r++){unsigned rank=0;for(unsigned j=0;j<4;j++)if(x[j]<x[r])rank++;cw_obj[order[rank]-1]=ok[r];}
    lp_log("inputs: channel boxes 1-4 at %llx %llx %llx %llx",(unsigned long long)cw_obj[0],(unsigned long long)cw_obj[1],(unsigned long long)cw_obj[2],(unsigned long long)cw_obj[3]);
}
/* Screen position: sum of the bounds up the parent chain (JUCE +0x18 parent, +0x20 x,y). */
static int cw_abs(uint64_t c,int *ax,int *ay) {
    int x=0,y=0;
    for(int d=0;d<12&&c;d++){int32_t b[4];uint64_t parent=0;if(!nt_peek(c+0x20,(unsigned char *)b,16)||!nt_peek(c+0x18,(unsigned char *)&parent,8))return 0;x+=b[0];y+=b[1];c=parent;}
    *ax=x;*ay=y;return 1;
}
static int cw_visible(uint64_t c){uint32_t f=0;return nt_peek(c+0xc0,(unsigned char *)&f,4)&&((f>>1)&1u);}

static const struct oqmod_api *mi_api;
/* Panels: per channel 3/4 up to three: the menu entry, (while on) the LINE entry redrawn without
 * its check, and (while on, box not shrunk by the 4-deck band) "PRO DJ LINK" over the channel
 * box's input name.
 * 1.59: drawn to match EP136 pixel for pixel. Its menu entries are transparent text images
 * (BinaryData next to vt 0x27fb158) over the pop-up's black fill; the box is a flat image,
 * 0x232323 (arrow up) or 0x323232 (arrow down, menu open). The masks are in prodjlink_menuart.h. */
#include "prodjlink_menuart.h"
enum {MI_ENTRY,MI_LINE,MI_BOX};
#define MI_MAX 6
static unsigned mi_kind[MI_MAX],mi_ch[MI_MAX],mi_bg[MI_MAX],mi_active[2],mi_count;
static unsigned mi_toggle_req;            /* bit c-1: toggle channel c, from touch */
#define MI_MENU_BG 0x000000u
#define MI_BOX_BG 0x232323u
#define MI_BOX_BG_OPEN 0x323232u
static void mi_art(struct oqmod_canvas *c,const struct ma_art *m,uint32_t bg) {
    unsigned br=bg>>16&255,bgc=bg>>8&255,bb=bg&255;
    for(int y=0;y<m->h;y++) {
        const unsigned char *row=m->a+y*m->w;
        for(int x=0;x<m->w;) {
            unsigned a=row[x];int n=1;while(x+n<m->w&&row[x+n]==a)n++;
            if(a){unsigned r=br+(255-br)*a/255,g=bgc+(255-bgc)*a/255,b=bb+(255-bb)*a/255;
                mi_api->fill(c,m->x+x,m->y+y,n,1,r<<16|g<<8|b);}
            x+=n;
        }
    }
}
static void mi_draw(struct oqmod_canvas *c,unsigned index,int w,int h) {
    if(index>=MI_MAX)return;
    unsigned kind=__atomic_load_n(&mi_kind[index],__ATOMIC_RELAXED),ch=__atomic_load_n(&mi_ch[index],__ATOMIC_RELAXED);
    unsigned active=ch>=3&&ch<=4?__atomic_load_n(&mi_active[ch-3],__ATOMIC_RELAXED):0;
    if(kind==MI_LINE){mi_api->fill(c,0,0,w,h,MI_MENU_BG);mi_art(c,&ma_line,MI_MENU_BG);return;}
    if(kind==MI_BOX){uint32_t bg=__atomic_load_n(&mi_bg[index],__ATOMIC_RELAXED);mi_api->fill(c,0,0,w,h,bg);mi_art(c,&ma_box,bg);return;}
    mi_api->fill(c,0,0,w,h,MI_MENU_BG);
    mi_art(c,&ma_entry,MI_MENU_BG);
    if(active)mi_art(c,&ma_check,MI_MENU_BG);
}
static int mi_touch(unsigned index,int x,int y) {
    (void)x;(void)y;if(index>=MI_MAX)return 0;
    if(__atomic_load_n(&mi_kind[index],__ATOMIC_RELAXED)!=MI_ENTRY)return 0;   /* the rest only paint */
    unsigned ch=__atomic_load_n(&mi_ch[index],__ATOMIC_RELAXED);
    if(ch==3||ch==4)__atomic_or_fetch(&mi_toggle_req,1u<<(ch-1),__ATOMIC_RELEASE);
    return 1;
}
static void mi_init(const struct oqmod_api *api) {
    mi_api=api;
    if(api->panel_renderer&&api->panels_set&&!api->panel_renderer(mi_draw,mi_touch))lp_log("inputs: panel renderer refused");
}
static void mi_tick(const struct oqmod_api *api) {
    unsigned req=__atomic_exchange_n(&mi_toggle_req,0,__ATOMIC_ACQ_REL);
    for(unsigned ch=3;ch<=4;ch++)if(req>>(ch-1)&1u) {
        char key[8];snprintf(key,sizeof key,"link%u",ch);
        int now_on=api->setting_get(key,0)==1;api->setting_set(key,!now_on);
        lp_log("inputs: PRO DJ LINK for channel %u %s from the input menu",ch,now_on?"off":"on");
    }
    struct oqmod_panel p[MI_MAX];unsigned n=0,mask=__atomic_load_n(&ch_link_mask,__ATOMIC_ACQUIRE);
    const unsigned flags=OQMOD_PANEL_ABOVE_POPUPS;   /* R124: stay up over the mixer pop-ups */
    if(__atomic_load_n(&ch_scanned,__ATOMIC_ACQUIRE)==2) {
        for(unsigned ch=3;ch<=4;ch++) {
            unsigned on=(mask>>(ch-1))&1u;__atomic_store_n(&mi_active[ch-3],on,__ATOMIC_RELAXED);
            uint64_t w=cw_obj[ch-1];if(!w||!cw_visible(w))continue;
            uint64_t menu=cw_child_vt(w,0x27fb158ull);int ax,ay;
            unsigned menu_open=menu&&cw_visible(menu);
            if(menu_open&&cw_abs(menu,&ax,&ay)&&ax+12>=0&&ay>=0&&ax+232<=1280&&ay+293<=800) {
                p[n]=(struct oqmod_panel){ax+12,ay+250,220,43,OQMOD_PANEL_TOUCH|flags,0,0};mi_kind[n]=MI_ENTRY;mi_ch[n]=ch;n++;
                if(on){p[n]=(struct oqmod_panel){ax+12,ay+100,220,50,flags,0,0};mi_kind[n]=MI_LINE;mi_ch[n]=ch;n++;}
            }
            /* the box's input name: 220x21 at (12,457) in the 564-tall box; hidden when the
             * 4-deck band clips the box (its height is then smaller) */
            int32_t b[4];int bx,by;
            if(on&&nt_peek(w+0x20,(unsigned char *)b,16)&&b[3]>=457+21&&cw_abs(w,&bx,&by)&&bx+12>=0&&by+457>=0&&bx+232<=1280&&by+478<=800) {
                p[n]=(struct oqmod_panel){bx+12,by+457,220,21,flags,0,0};mi_kind[n]=MI_BOX;mi_ch[n]=ch;
                mi_bg[n]=menu_open?MI_BOX_BG_OPEN:MI_BOX_BG;n++;
            }
        }
    }
    static unsigned last_mask=99;
    if(api->panels_set&&(n!=mi_count||n||mask!=last_mask)){
        if(!api->panels_set(p,n)&&n){static unsigned said;if(!said){said=1;lp_log("inputs: the PRO DJ LINK menu entry needs firmware R124 (panel refused)");}}
        mi_count=n;
    }
    unsigned look=0;for(unsigned i=0;i<n;i++)look=look*7u+mi_kind[i]*3u+mi_bg[i]+mi_ch[i];   /* 1.59: box colour follows the menu */
    static unsigned last_look;
    if(mask!=last_mask||look!=last_look){last_mask=mask;last_look=look;if(api->redraw)api->redraw();}
}
#endif

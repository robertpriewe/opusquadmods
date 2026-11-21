/* 0.4: MOBIUS as an extra entry in the native Beat FX list (overlay panel).
 *
 * EP136 1.32 (static, mobius-fx-lab): gui::fx::SelectFxListWidget vt 0x28429d8 holds the 14
 * gui::fx::SelectFxWidget entries (vt 0x2843168, 143x34 each); SelectFxListController vt
 * 0x281b8b0 selects through 0x190b030 (index 0..13 only). The mixer gets the effect number, so
 * MOBIUS is never sent to it: the stock list and selection stay untouched, the extra entry is
 * a mod panel in the next free slot of the grid, and tapping a stock entry leaves MOBIUS.
 * Found like Pro DJ Link's input-menu boxes: one scan of heap/anonymous memory for the vtable,
 * then JUCE component fields (+0x18 parent, +0x20 bounds, +0x40/+0x50 children, +0xc0 flags).
 *
 * 0.5 (hardware 0.4: list "closed" all the time, MOBIUS never shown; photo: ONE column of 14,
 * DELAY .. HELIX, frame ends just below HELIX; the BEAT FX SELECT knob also goes through
 * 0x190b030, 88 calls): the list comes from the controller the select hook sees: 0x190b030
 * calls entry[index]->vt[12](bool) with the entries at controller +0xb0 + 8*index, current
 * index at +0x50. MOBIUS goes right under the 14th entry (HELIX). The memory scan is gone.
 *
 * 0.6 (hardware 0.5: MOBIUS showed under HELIX, but only while HELIX was selected; turning on
 * from HELIX wrapped to DELAY): the 14 objects are the entries' highlight, visible only for the
 * selected one, so "list open" = the entries' parent (the list) and its parent visible.
 * While MOBIUS is selected a second panel paints HELIX unselected over the native highlight.
 * Knob: mixer frame byte 20 counts BEAT FX SELECT steps, byte 89 echoes the effect (0.4 log).
 * All knobs pass input_devices encoder vt 0x29bbbc0 slot 4 (identity at +0xd0); the mod learns
 * which identity is BEAT FX SELECT (its events line up with byte 20 changes) and then steps
 * HELIX <-> MOBIUS <-> DELAY, holding back the native step where needed (acknowledged at
 * +0xd8 / +0xdc like SHIFT + BROWSE zoom).
 *
 * 0.7 (hardware 0.6: MOBIUS shows with any effect selected, but the effect box on the right still
 * says HELIX): the box is gui::fx::SelectedFxWidget (vt 0x2839028, EP136 1.32 RTTI). It is found
 * in the component tree around the list (each ancestor's children, a few levels deep); while
 * MOBIUS is selected a third panel paints MOBIUS over it. Its size and children are logged.
 * The cover colour of the list rows is darker (0.6's grey looked lighter than the stock rows).
 *
 * 0.8 (hardware 0.7: MOBIUS in list + box works, sound OK; box drew over native menus; the list is
 * not touchable natively, so MOBIUS must not be either; text looks different from the native names):
 * no touch; the box panel only while the list is closed, without ABOVE_POPUPS (the host hides it
 * under any native pop-up; the open list covers the box anyway). The native names are bitmaps
 * (each Fx*Component ctor loads a resource image into +0xc8, SelectedFxWidget paint 0x19251d8 draws
 * its +0xc8 image 143x34): 0.8 saves every such image it finds in the list / box to
 * MODS/mobius/native/ so 0.9 can draw MOBIUS from the same pixels.
 *
 * 0.9 (hardware 0.8: 56 native name images saved; user: MOBIUS must use the native font, regular
 * weight, white on black like ECHO): the names are Helvetica Neue Regular (list 18.1 px +0.8 px
 * tracking, box 21 px +0.5), fitted against the saved images to ~1/255 mean error; MOBIUS in that
 * style is mobius_name_px.h (mobius-fx-lab/make_mobius_px.py), drawn as colour runs. List: white
 * on black, selected black on white; box: orange (255,115,35) with BEAT FX on, white off. The
 * native highlight under MOBIUS is now switched off with the entry's own vt[12](false) (what the
 * select routine calls) instead of painting a cover, and back on when MOBIUS is left. */
#include "mobius_name_px.h"
#ifndef MOBIUS_LIST_H
#define MOBIUS_LIST_H
#include <sys/uio.h>
#include <sys/syscall.h>
static int ml_peek(uint64_t a,void *o,size_t n){
    struct iovec l={o,n},r={(void *)(uintptr_t)a,n};
    return a>0x400000&&a<0x1000000000000ull&&syscall(SYS_process_vm_readv,getpid(),&l,1,&r,1,0)==(long)n;
}
static int ml_abs(uint64_t c,int *ax,int *ay){
    int x=0,y=0;
    for(int d=0;d<16&&c;d++){int32_t b[4];uint64_t parent=0;
        if(!ml_peek(c+0x20,b,16)||!ml_peek(c+0x18,&parent,8))return 0;
        x+=b[0];y+=b[1];c=parent;}
    *ax=x;*ay=y;return 1;
}
/* 0.5: the entry and its list visible (0.4 also required every parent and never showed);
 * the full chain is logged once for research */
static int ml_vis(uint64_t c){uint32_t fl=0;return ml_peek(c+0xc0,&fl,4)&&((fl>>1)&1u);}
static int ml_shown(uint64_t c){   /* c = an entry: its list and the list's parent */
    uint64_t list=0,up=0;
    if(!ml_peek(c+0x18,&list,8)||!list||!ml_vis(list)||!ml_peek(list+0x18,&up,8)||!up)return 0;
    return ml_vis(up);
}
static void ml_chain_text(uint64_t c,char *out,size_t cap){   /* live flags for the status */
    int n=snprintf(out,cap,"flags");
    for(int d=0;d<5&&c&&n>0&&(size_t)n<cap;d++){uint32_t fl=0;uint64_t parent=0;
        ml_peek(c+0xc0,&fl,4);n+=snprintf(out+n,cap-(size_t)n," %x",fl);
        if(!ml_peek(c+0x18,&parent,8))break;
        c=parent;}
}
static void ml_chain_log(uint64_t c){
    char line[180];int n=snprintf(line,sizeof line,"mobius: entry 14 chain flags:");
    for(int d=0;d<10&&c&&n>0&&(size_t)n<sizeof line;d++){uint32_t fl=0;uint64_t parent=0;
        ml_peek(c+0xc0,&fl,4);n+=snprintf(line+n,sizeof line-(size_t)n," %x",fl);
        if(!ml_peek(c+0x18,&parent,8))break;
        c=parent;}
    LOG("%s",line);
}
struct ml_item {int x,y,w,h;};
static volatile uint64_t ml_ctrl;              /* SelectFxListController, from the select hook */
static unsigned ml_why;                         /* why the entry is not shown (status) */
static int ml_entry_rect(uint64_t ctrl,unsigned i,struct ml_item *out,uint64_t *obj){
    uint64_t c=0;int32_t b[4];int ax,ay;
    if(!ml_peek(ctrl+0xb0+8u*i,&c,8)||!c||!ml_peek(c+0x20,b,16)||!ml_abs(c,&ax,&ay))return 0;
    *out=(struct ml_item){ax,ay,b[2],b[3]};
    if(obj)*obj=c;
    return 1;
}
/* ---- the panel ---- */
static volatile int ml_selected;              /* MOBIUS chosen in the list */
static volatile int ml_tap_req;
static volatile int ml_fx_on;                 /* for the drawing */
static const char *const ml_names[14]={"DELAY","ECHO","PING PONG","SPIRAL","REVERB","TRANS","FILTER",
    "FLANGER","PHASER","PITCH","SLIP ROLL","ROLL","VINYL BRAKE","HELIX"};
static volatile unsigned char ml_kind[3];   /* 0.7: per panel: 0 entry, 1 cover, 2 effect box */
static void ml_draw(struct oqmod_canvas *c,unsigned index,int w,int h){
    if(index<3)index=ml_kind[index];
    int sel=__atomic_load_n(&ml_selected,__ATOMIC_RELAXED);
    /* 0.9: native-style pixels (mobius_name_px.h) */
    const struct mn_img *im=index==2?(__atomic_load_n(&ml_fx_on,__ATOMIC_RELAXED)?&mn_box_on:&mn_box_off):sel?&mn_list_on:&mn_list_off;
    uint32_t bg=im->runs[0].rgb;   /* top-left: the background */
    int ox=(w-im->w)/2,oy=(h-im->h)/2;
    if(ox||oy)api->fill(c,0,0,w,h,bg);
    for(unsigned i=0;i<im->n;i++){const struct mn_run *r=&im->runs[i];api->fill(c,ox+r->x,oy+r->row,r->len,1,r->rgb);}
}
/* 0.9: the native highlight of an entry, switched like the select routine does */
static uint64_t ml_hl_off;   /* entry whose highlight the mod switched off */
static int ml_entry_hl(uint64_t entry,int on){
    uint64_t vt=0,fn=0;
    if(!entry||!ml_peek(entry,&vt,8)||!ml_peek(vt+12*8,&fn,8)||fn<0x400000||fn>=0x2400000)return 0;
    ((void (*)(uint64_t,int))(uintptr_t)fn)(entry,on);return 1;
}
/* ---- 0.7: SelectedFxWidget (the effect box) ---- */
#define ML_SELECTED_VT 0x2839028ull
static uint64_t ml_box;static unsigned ml_box_logged;static struct ml_item ml_box_rect;
static uint64_t ml_find_vt(uint64_t root,uint64_t vt,int depth,unsigned *budget){
    uint64_t arr=0;int32_t n=0;
    if(depth<0||!*budget||!ml_peek(root+0x40,&arr,8)||!ml_peek(root+0x50,&n,4)||n<0||n>80)return 0;
    for(int i=0;i<n&&*budget;i++){uint64_t k=0,kv=0;(*budget)--;
        if(!ml_peek(arr+8u*(unsigned)i,&k,8)||!k||!ml_peek(k,&kv,8))continue;
        if(kv==vt)return k;
        uint64_t r=ml_find_vt(k,vt,depth-1,budget);if(r)return r;}
    return 0;
}
static uint64_t ml_find_box(uint64_t entry){
    uint64_t vt=0;
    if(ml_box&&ml_peek(ml_box,&vt,8)&&vt==ML_SELECTED_VT)return ml_box;
    ml_box=0;uint64_t c=entry;
    for(int up=0;up<8&&c;up++){uint64_t parent=0;if(!ml_peek(c+0x18,&parent,8)||!parent)break;
        unsigned budget=1500;uint64_t r=ml_find_vt(parent,ML_SELECTED_VT,4,&budget);
        if(r){ml_box=r;
            if(!ml_box_logged){ml_box_logged=1;int32_t b[4]={0};int ax=0,ay=0;ml_peek(r+0xc0-0xa0,b,16);ml_abs(r,&ax,&ay);
                uint64_t arr=0;int32_t n=0;ml_peek(r+0x40,&arr,8);ml_peek(r+0x50,&n,4);
                char kids[150];int k=snprintf(kids,sizeof kids,"children %d:",n);
                for(int i=0;i<n&&i<6&&k>0&&(size_t)k<sizeof kids;i++){uint64_t ch=0,cv=0;int32_t cb[4]={0};ml_peek(arr+8u*(unsigned)i,&ch,8);ml_peek(ch,&cv,8);ml_peek(ch+0x20,cb,16);
                    k+=snprintf(kids+k,sizeof kids-(size_t)k," vt %llx %d,%d %dx%d;",(unsigned long long)cv,cb[0],cb[1],cb[2],cb[3]);}
                LOG("mobius: effect box %llx (ancestor %d up) at %d,%d %dx%d",(unsigned long long)r,up+1,ax,ay,b[2],b[3]);LOG("mobius: effect box %.150s",kids);}
            return r;}
        c=parent;}
    return 0;
}
static volatile unsigned ml_taps;
static int ml_touch(unsigned index,int x,int y){(void)index;(void)x;(void)y;return 0;}   /* 0.8: like the native list: knob only */
/* ---- 0.8: save the native name images ---- */
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
struct ml_img{uint64_t data,pd,vt;int w,h,ps,ls,x,y,depth;unsigned char *px;};
#define ML_IMGS 128
static struct ml_img ml_imgs[ML_IMGS];static unsigned ml_nimg,ml_saved,ml_dump_busy;
static int ml_image(uint64_t comp,struct ml_img *o){
    uint64_t pd=0;if(!ml_peek(comp+0xc8,&pd,8)||!pd)return 0;
    int32_t hdr[26];if(!ml_peek(pd,hdr,sizeof hdr))return 0;
    int w=hdr[4],h=hdr[5];if(w<8||w>400||h<8||h>200)return 0;
    for(int k=6;k<24;k+=2){uint64_t q;memcpy(&q,&hdr[k],8);int ps=hdr[k+2],ls=hdr[k+3];
        if(q>0x400000&&(ps==4||ps==3||ps==1)&&ls>=w*ps&&ls<=w*ps+64){o->data=q;o->pd=pd;o->w=w;o->h=h;o->ps=ps;o->ls=ls;return 1;}}
    return 0;
}
static void ml_collect(uint64_t c,int depth,unsigned *budget){
    if(!c||!*budget||ml_nimg>=ML_IMGS)return;
    (*budget)--;
    struct ml_img im;memset(&im,0,sizeof im);
    if(ml_image(c,&im)){
        int dup=0;for(unsigned i=0;i<ml_nimg;i++)if(ml_imgs[i].data==im.data){dup=1;break;}
        size_t n=(size_t)im.ls*(size_t)im.h;
        if(!dup&&(im.px=malloc(n))){
            if(ml_peek(im.data,im.px,n)){ml_peek(c,&im.vt,8);int ax=0,ay=0;ml_abs(c,&ax,&ay);im.x=ax;im.y=ay;im.depth=depth;ml_imgs[ml_nimg++]=im;}
            else free(im.px);}
    }
    if(depth<=0)return;
    uint64_t arr=0;int32_t n=0;
    if(!ml_peek(c+0x40,&arr,8)||!ml_peek(c+0x50,&n,4)||n<0||n>80)return;
    for(int i=0;i<n;i++){uint64_t k=0;if(ml_peek(arr+8u*(unsigned)i,&k,8))ml_collect(k,depth-1,budget);}
}
static void *ml_writer(void *arg){
    unsigned upto=(unsigned)(uintptr_t)arg;
    char dir[300]="";DIR *d=opendir("/media/usb");
    if(d){struct dirent *e;while((e=readdir(d))){if(e->d_name[0]=='.')continue;char t[300];struct stat st;
        snprintf(t,sizeof t,"/media/usb/%.200s/MODS/mobius",e->d_name);if(!stat(t,&st)&&S_ISDIR(st.st_mode)){snprintf(dir,sizeof dir,"%.280s/native",t);break;}}closedir(d);}
    if(dir[0]){mkdir(dir,0755);
        char ip[340];snprintf(ip,sizeof ip,"%s/index.txt",dir);FILE *ix=fopen(ip,"a");
        for(unsigned i=ml_saved;i<upto;i++){struct ml_img *m=&ml_imgs[i];char fp[360];
            snprintf(fp,sizeof fp,"%s/img%03u-vt%llx-%dx%d.bgra",dir,i,(unsigned long long)m->vt,m->w,m->h);
            FILE *f=fopen(fp,"wb");if(f){for(int y=0;y<m->h;y++)fwrite(m->px+(size_t)y*(size_t)m->ls,1,(size_t)m->w*(size_t)m->ps,f);fclose(f);}
            if(ix)fprintf(ix,"img%03u vt %llx pd %llx %dx%d ps %d ls %d at %d,%d depth %d\n",i,(unsigned long long)m->vt,(unsigned long long)m->pd,m->w,m->h,m->ps,m->ls,m->x,m->y,m->depth);}
        if(ix){fflush(ix);fsync(fileno(ix));fclose(ix);}
        sync();}
    __atomic_store_n(&ml_saved,upto,__ATOMIC_RELEASE);__atomic_store_n(&ml_dump_busy,0,__ATOMIC_RELEASE);
    return 0;
}
static void ml_dump_tick(uint64_t ctrl){
    static unsigned t;if(t++%30||!ctrl||__atomic_load_n(&ml_dump_busy,__ATOMIC_ACQUIRE))return;
    uint64_t e13=0,list=0,root=0;
    if(!ml_peek(ctrl+0xb0+8u*13,&e13,8)||!e13||!ml_peek(e13+0x18,&list,8)||!list)return;
    unsigned budget=600;
    /* the list: its entries and their children; one more level up for whatever holds it */
    if(ml_peek(list+0x18,&root,8)&&root)ml_collect(root,4,&budget);else ml_collect(list,3,&budget);
    if(ml_box)ml_collect(ml_box,3,&budget);
    unsigned n=ml_nimg;
    if(n>ml_saved){
        __atomic_store_n(&ml_dump_busy,1,__ATOMIC_RELEASE);
        pthread_t th;pthread_attr_t at;pthread_attr_init(&at);pthread_attr_setdetachstate(&at,PTHREAD_CREATE_DETACHED);
        if(pthread_create(&th,&at,ml_writer,(void *)(uintptr_t)n))__atomic_store_n(&ml_dump_busy,0,__ATOMIC_RELEASE);
        pthread_attr_destroy(&at);}
}
static unsigned ml_panel_n,ml_list_seen;static struct ml_item ml_slot;
static void ml_tick(int want_entry){
    if(__atomic_exchange_n(&ml_tap_req,0,__ATOMIC_ACQ_REL)){
        int s=!__atomic_load_n(&ml_selected,__ATOMIC_RELAXED);__atomic_store_n(&ml_selected,s,__ATOMIC_RELEASE);
        LOG("mobius: MOBIUS entry tapped, now %s",s?"selected":"off");if(api->redraw)api->redraw();
    }
    struct oqmod_panel p[3];unsigned n=0;int list_open=0;
    uint64_t ctrl=__atomic_load_n(&ml_ctrl,__ATOMIC_ACQUIRE);
    ml_why=!want_entry?1:!ctrl?2:0;
    /* 0.9: MOBIUS selected = the native entry under it shows unselected (and back afterwards) */
    if(ctrl){
        unsigned cur=99;uint64_t entry=0;
        if(ml_peek(ctrl+0x50,&cur,4)&&cur<14)ml_peek(ctrl+0xb0+8u*cur,&entry,8);
        int sel=want_entry&&__atomic_load_n(&ml_selected,__ATOMIC_ACQUIRE);
        if(sel&&entry&&entry!=ml_hl_off){
            if(ml_hl_off)ml_entry_hl(ml_hl_off,1);
            if(ml_entry_hl(entry,0))ml_hl_off=entry;
        }else if(!sel&&ml_hl_off){
            if(entry==ml_hl_off)ml_entry_hl(entry,1);   /* left MOBIUS back to this one */
            ml_hl_off=0;
        }
    }
    if(want_entry&&ctrl){
        struct ml_item a13,a12;uint64_t o13=0;
        if(!ml_entry_rect(ctrl,13,&a13,&o13)||!ml_entry_rect(ctrl,12,&a12,0))ml_why=3;
        else if(!ml_shown(o13)){ml_why=4;static uint64_t logged;if(logged!=ctrl){logged=ctrl;ml_chain_log(o13);}}
        else{
            int dy=a13.y-a12.y;
            if(dy<a13.h||dy>3*a13.h)dy=a13.h;
            struct ml_item s={a13.x,a13.y+dy,a13.w,a13.h};
            if(s.x<0||s.y<0||s.x+s.w>1280||s.y+s.h>800||s.w<20||s.h<10)ml_why=5;
            else{
                ml_kind[n]=0;p[n++]=(struct oqmod_panel){s.x,s.y,s.w,s.h,OQMOD_PANEL_ABOVE_POPUPS,0,0};list_open=1;
                unsigned cur=99;ml_peek(ctrl+0x50,&cur,4);
                if(!ml_list_seen||ml_slot.x!=s.x||ml_slot.y!=s.y){
                    struct ml_item a0={0,0,0,0};ml_entry_rect(ctrl,0,&a0,0);
                    LOG("mobius: list open: entry 1 at %d,%d %dx%d, entry 14 at %d,%d, step %d -> MOBIUS at %d,%d",a0.x,a0.y,a0.w,a0.h,a13.x,a13.y,dy,s.x,s.y);
                }
                ml_list_seen=1;ml_slot=s;
            }
        }
    }
    /* 0.7: the effect box shows MOBIUS whether the list is open or not */
    if(want_entry&&ctrl&&!list_open&&__atomic_load_n(&ml_selected,__ATOMIC_RELAXED)){   /* 0.8: list closed only */
        uint64_t o13=0;
        if(ml_peek(ctrl+0xb0+8u*13,&o13,8)&&o13){
            uint64_t box=ml_find_box(o13);int32_t bb[4];int bx,by;
            if(box&&ml_vis(box)&&ml_peek(box+0x20,bb,16)&&ml_abs(box,&bx,&by)&&bb[2]>10&&bb[3]>8&&bx>=0&&by>=0&&bx+bb[2]<=1280&&by+bb[3]<=800){
                ml_kind[n]=2;p[n++]=(struct oqmod_panel){bx,by,bb[2],bb[3],0,0,0};ml_box_rect=(struct ml_item){bx,by,bb[2],bb[3]};}
        }
    }
    {static unsigned said;
     if(ctrl&&!said){said=1;uint64_t e[14]={0},vt0=0,par=0,pvt=0;ml_peek(ctrl+0xb0,e,sizeof e);ml_peek(e[0],&vt0,8);ml_peek(e[13]+0x18,&par,8);ml_peek(par,&pvt,8);
        LOG("mobius: list controller %llx: entry vt %llx, entry 14 parent %llx vt %llx",(unsigned long long)ctrl,(unsigned long long)vt0,(unsigned long long)par,(unsigned long long)pvt);}}
    if(api->panels_set&&(n||ml_panel_n)){
        static struct oqmod_panel last[3];
        if(n!=ml_panel_n||memcmp(last,p,n*sizeof p[0])){api->panels_set(p,n);memcpy(last,p,n*sizeof p[0]);if(api->redraw)api->redraw();}
        ml_panel_n=n;
    }
}
#endif

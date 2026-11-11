/* Native overview re-hide/restoration in the actual external title engine. */
#include <assert.h>
#include <stdint.h>
static uintptr_t vt2[18],vt4[18];
#define HT2_VT ((uintptr_t)&vt2[3])
#define HT4_VT ((uintptr_t)&vt4[3])
#include "../mods/hide-titles/mod.c"
static struct oqmod_panel last[10];
static unsigned publishes;
static int panels(const struct oqmod_panel *p,unsigned n){assert(n==10||n==0);if(n)memcpy(last,p,sizeof last);publishes++;return 1;}
static void visible(void *c,int on){unsigned *f=(unsigned *)((char *)c+COMP_FLAGS);*f=(*f&~2u)|((unsigned)!!on<<1);}
static void show2(void *c,const void *t,int k){(void)t;(void)k;visible(*(void **)((char *)c+HT2_TITLE),1);}
static void show4(void *c,const void *t,int k){(void)t;(void)k;visible(*(void **)((char *)c+HT4_TITLE),1);}
static void clear2(void *c){visible(*(void **)((char *)c+HT2_TITLE),0);}
struct component {_Alignas(16) char p[512];};
static uintptr_t cvt[20];
static void comp(struct component *c,uintptr_t *vt,struct component *parent,int x,int y,int w,int h){
    *(uintptr_t **)c->p=vt;*(void **)(c->p+COMP_PARENT)=parent;int b[]={x,y,w,h};memcpy(c->p+COMP_BOUNDS,b,sizeof b);visible(c,1);
}
int main(void){
    struct oqmod_api a={.panels_set=panels};oq_feature_api=&a;cvt[15]=(uintptr_t)visible;
    struct component root={0},field={0},b2={0},b4={0},t2={0},t4={0};
    comp(&root,cvt+3,NULL,0,0,1280,800);comp(&field,cvt+3,&root,0,441,1280,204);
    comp(&b2,vt2+3,&field,645,0,630,204);comp(&b4,vt4+3,&field,5,104,630,100);
    comp(&t2,cvt+3,&b2,85,4,380,30);comp(&t4,cvt+3,&b4,85,4,380,30);
    *(void **)(b2.p+HT2_TITLE)=&t2;*(void **)(b4.p+HT4_TITLE)=&t4;
    ht_o_show2=show2;ht_o_show4=show4;ht_o_clear2=clear2;ls_selected[1]=2;
    const char *song="Example";ht_show2(&b2,&song,1);ht_show4(&b4,&song,1);assert(ht_count==2);
    ht_tick(1);assert(last[0].x==696&&last[0].y==449&&last[1].x==56);
    ht_pending=1;ht_tick(1);assert(ht_deck_hidden==2&&!comp_visible_now(t2.p)&&comp_visible_now(t4.p));
    ht_show2(&b2,&song,1);assert(!comp_visible_now(t2.p));
    ls_selected[1]=4;ht_tick(1);assert(comp_visible_now(t2.p)&&ht_deck_hidden==2);
    ls_selected[1]=2;ht_tick(1);assert(!comp_visible_now(t2.p));
    ht_pending=2;ht_tick(1);assert(ht_deck_hidden==6&&!comp_visible_now(t4.p));
    ht_tick(0);assert(!ht_deck_hidden&&comp_visible_now(t2.p)&&comp_visible_now(t4.p));
    ht_clear2(&b2);assert(!ht_boxes[0].has&&!comp_visible_now(t2.p));
    assert(ht_scans==0&&publishes>0); /* never resurrect the disabled tree scan */
    unsigned char ascii[]="Artist Test";assert(tb_blank(ascii,sizeof ascii-1,"Artist Test")==11);
    unsigned char utf16[]={0xe9,0,'t',0,0xe9,0};assert(tb_blank(utf16,sizeof utf16,"\xc3\xa9t\xc3\xa9")==3&&utf16[0]==' ');
    puts("TITLE_ENGINE_NATIVE_POLICY_PASSED");return 0;
}

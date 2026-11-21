#ifndef OQ_PRODJLINK_ICONS_H
#define OQ_PRODJLINK_ICONS_H
/* "LINK n" source icons for another player's USB on the OPUS (R122 / EP136 1.32).
 * Device icons live in a global two-level map (0x35216b0): key1 = size (0 source tile,
 * 1 status area, 2 waveform, 4 shortcut), key2 = image (0x16/0x17/0x18 = USB 1/2/3).
 * gui::device_icon getImage(this, MediaID*) = 0x17d1168: kind = *(int*)MediaID, number =
 * MediaID[4], store = *(this+8), key1 at this+0x10; image lookup 0x17d3190(store, &key1,
 * &key2) -> juce::Image via x8. Our PNGs (MODS/prodjlink/icons/link<n>_<size>.png, copied
 * to /tmp/oq-link-icons) are loaded with EP136's own juce calls under key2 = 0x60 + n:
 * 0x17d94c0 inner map, 0x17d98e8 slot, 0x2001b40 ImageFileFormat::loadFrom (File in x0,
 * Image via x8), 0x1fc4d70 Image::operator=, 0x1fb7e90 ~Image, 0x1f2da90 String(const char*),
 * 0x1f0fe40 ~String (a juce::File is its path String). This build: load + log MediaIDs. */
#include <sys/stat.h>
#define LI_KEY_BASE 0x60
static void *li_orig;static unsigned li_installed,li_lines,li_loaded,li_failed,li_use;
static char li_dir[200];
/* x8-returning calls: (fn, x0, x8) */
extern void li_call_sret(uint64_t fn,uint64_t x0,uint64_t x8);
__asm__(".text\n.p2align 2\n.globl li_call_sret\n.type li_call_sret,%function\nli_call_sret:\n"
        "mov x16,x0\nmov x0,x1\nmov x8,x2\nbr x16\n.size li_call_sret,.-li_call_sret\n");
static int li_copy(const char *from,const char *to) {
    FILE *a=fopen(from,"rb");if(!a)return 0;FILE *b=fopen(to,"wb");if(!b){fclose(a);return 0;}
    char buf[4096];size_t r;int ok=1;while((r=fread(buf,1,sizeof buf,a))>0)if(fwrite(buf,1,r,b)!=r)ok=0;
    fclose(a);fclose(b);return ok;
}
/* copy the icons off the USB once (they must survive the stick being pulled) */
static void li_prepare(void) {
    if(li_dir[0])return;
    if(!lp_path[0])lp_find_path();
    if(!lp_path[0])return;
    char src[300];snprintf(src,sizeof src,"%s",lp_path);char *s=strrchr(src,'/');if(!s)return;*s=0;
    mkdir("/tmp/oq-link-icons",0755);
    static const char *const sizes[]={"source","statusArea","waveform","shortcut"};
    unsigned n=0;
    for(unsigned p=1;p<=6;p++)for(unsigned k=0;k<4;k++) {
        char a[400],b[200];snprintf(a,sizeof a,"%s/icons/link%u_%s.png",src,p,sizes[k]);snprintf(b,sizeof b,"/tmp/oq-link-icons/link%u_%s.png",p,sizes[k]);
        n+=(unsigned)li_copy(a,b);
    }
    snprintf(li_dir,sizeof li_dir,"/tmp/oq-link-icons");
    lp_log("link icons: %u of 24 copied to %s",n,li_dir);
}
/* load our six icons for one size (key1) into the store; once per key1 */
static void li_load(uint64_t store,int key1) {
    static unsigned done;if(key1<0||key1>7||(done&(1u<<key1)))return;
    done|=1u<<key1;
    static const char *const names[8]={"source","statusArea","waveform",0,"shortcut",0,0,0};
    if(!names[key1]||!li_dir[0])return;
    for(unsigned p=1;p<=6;p++) {
        char path[260];snprintf(path,sizeof path,"%s/link%u_%s.png",li_dir,p,names[key1]);
        struct stat st;if(stat(path,&st)){li_failed++;continue;}
        int k1=key1,k2=LI_KEY_BASE+(int)p;
        uint64_t inner=((uint64_t (*)(uint64_t,int *))0x17d94c0)(store,&k1);
        uint64_t slot=((uint64_t (*)(uint64_t,int *))0x17d98e8)(inner,&k2);
        uint64_t file=0,img=0;
        ((void (*)(uint64_t *,const char *))0x1f2da90)(&file,path);
        li_call_sret(0x2001b40,(uint64_t)(uintptr_t)&file,(uint64_t)(uintptr_t)&img);
        int ok=img!=0;
        ((void (*)(uint64_t,uint64_t *))0x1fc4d70)(slot,&img);
        ((void (*)(uint64_t *))0x1fb7e90)(&img);
        ((void (*)(uint64_t *))0x1f0fe40)(&file);
        if(ok)li_loaded++;else li_failed++;
        if(li_lines<40){li_lines++;lp_log("link icon %s size %d key %#x: %s",path,key1,k2,ok?"loaded":"FAILED");}
    }
}
/* decide per call: 0 = the normal icon, else our key2 */
static unsigned li_key_for(uint64_t self,uint64_t media) {
    unsigned char m[16]={0};nt_peek(media,m,sizeof m);
    uint64_t store=0;int key1=-1;nt_peek(self+8,(unsigned char *)&store,8);nt_peek(self+0x10,(unsigned char *)&key1,4);
    li_prepare();
    if(store)li_load(store,key1);
    /* 48 bytes of the MediaID and the caller, every call (first 60 per size 0, 20 others) */
    {static unsigned per[8];unsigned char w[48]={0};nt_peek(media,w,sizeof w);
     if(key1>=0&&key1<8&&per[key1]<(key1==0?60u:20u)){per[key1]++;
        char h[100];lp_hex(h,sizeof h,w,48);
        if(!LP_QUIET)lp_log("icon request size %d media@%llx %s",key1,(unsigned long long)media,h);}}
    /* A source owned by another player: MediaID +40 = its LinkMemberID {kind 2 = player,
     * index = player - 1} (local USB has 01 00 there; seen 2026-11-11: XDJ stick 02 03).
     * Only while another player's USB is mounted (nt_link_ip), and only for USB/SD kinds. */
    unsigned char w[48]={0};
    if(li_use&&nt_link_ip[0]==0xa9&&nt_peek(media,w,sizeof w)) {
        int kind=*(int *)w;
        if((kind==5||kind==6)&&w[40]==2&&w[41]<6&&key1>=0&&key1<8) {
            unsigned player=w[41]+1u;
            static unsigned said;
            if(said<6){said++;lp_log("icon: source of player %u shown as LINK %u (size %d)",player,player,key1);}
            return LI_KEY_BASE+player;
        }
    }
    return 0;
}
/* getImage hook: x0 this, x1 MediaID, x8 result. Our key -> 0x17d3190(store, this+0x10, &key) */
uint64_t li_orig_fn;
unsigned li_key_c(uint64_t self,uint64_t media){return li_key_for(self,media);}
__asm__(".text\n.p2align 2\n.globl li_hook\n.type li_hook,%function\nli_hook:\n"
        "stp x29,x30,[sp,#-64]!\nmov x29,sp\nstp x19,x20,[sp,#16]\nstp x21,x22,[sp,#32]\n"
        "mov x19,x0\nmov x20,x1\nmov x21,x8\n"
        "bl li_key_c\n"
        "cbz w0,1f\n"
        "str w0,[sp,#48]\n"   /* key2 in its own slot of our frame */
        "ldr x0,[x19,#8]\nadd x1,x19,#0x10\nadd x2,sp,#48\nmov x8,x21\n"
        "movz x16,#0x3190\nmovk x16,#0x17d,lsl #16\nblr x16\n"
        "mov x0,x21\nldp x21,x22,[sp,#32]\nldp x19,x20,[sp,#16]\nldp x29,x30,[sp],#64\nret\n"
        "1:\nmov x0,x19\nmov x1,x20\nmov x8,x21\n"
        "ldp x21,x22,[sp,#32]\nldp x19,x20,[sp,#16]\nldp x29,x30,[sp],#64\n"
        "adrp x16,li_orig_fn\nadd x16,x16,:lo12:li_orig_fn\nldr x16,[x16]\nbr x16\n"
        ".size li_hook,.-li_hook\n");
extern char li_hook[];
static void li_install(const struct oqmod_api *api) {
    if(li_installed||!api->hook||!api->ep136_ok||!api->ep136_ok())return;
    li_installed=1;
    int ok=api->hook(0x17d1168,0xa9bc7bfdu,(void *)li_hook,&li_orig)==1;
    li_orig_fn=(uint64_t)(uintptr_t)li_orig;
    lp_log("link icons: getImage hook %s",ok?"on":"off");
}
#endif

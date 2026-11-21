/* 1.60: the SOURCE screen's library row for another player's USB says "Pro DJ Link" with a
 * LAN-plug icon (instead of "Local Library" + USB plug), and has no USB STOP button.
 *
 * EP136 1.32 (static RE 2026-11-12): the row is gui::source_select::DeviceLibraryComponent
 * (vt 0x2691140). DeviceLibraryTableModel (vt 0x28f7d90) slot 5 0x19d2660 builds/refreshes it
 * and calls update(this, const Row &) = 0x1738090 with Row+0 = the library's MediaID (byte 0
 * = member kind: 1 local, 2 Pro DJ Link player, byte 1 = player - 1). update() sets the icon
 * (81x90 image with text, skin16/picture/SourceSelect/img_deviceLibrary_localLibraryUsbMedia
 * .png) on the ImageComponent at +0xd8 via ImageComponent::setImage 0x209d700. The USB STOP
 * button is a child component named "UsbStopBtn" (juce::Component name String at +8).
 * After the original update(), a link row gets our image and the button is hidden
 * (Component::setVisible = vtable slot 12). All on EP136's message thread. */
#ifndef PRODJLINK_DEVLIB_H
#define PRODJLINK_DEVLIB_H
static void *dl_orig;static unsigned dl_installed,dl_use,dl_lines;
static uint64_t dl_img;            /* juce::Image, loaded once and kept */
static unsigned dl_load_tried;
static void dl_load(void) {
    if(dl_load_tried)return;
    li_prepare();
    if(!li_dir[0]||!lp_path[0])return;
    dl_load_tried=1;
    char src[300];snprintf(src,sizeof src,"%s",lp_path);char *s=strrchr(src,'/');if(!s)return;*s=0;
    char from[400],to[260];snprintf(from,sizeof from,"%s/icons/devlib_link.png",src);snprintf(to,sizeof to,"%s/devlib_link.png",li_dir);
    if(!li_copy(from,to)){lp_log("devlib: %s missing",from);return;}
    uint64_t file=0;
    ((void (*)(uint64_t *,const char *))0x1f2da90)(&file,to);
    li_call_sret(0x2001b40,(uint64_t)(uintptr_t)&file,(uint64_t)(uintptr_t)&dl_img);
    ((void (*)(uint64_t *))0x1f0fe40)(&file);
    lp_log("devlib: Pro DJ Link library image %s",dl_img?"loaded":"FAILED");
}
static int dl_name_is(uint64_t comp,const char *want) {
    uint64_t p=0;char t[24]={0};
    if(!nt_peek(comp+8,(unsigned char *)&p,8)||!p||!nt_peek(p,(unsigned char *)t,sizeof t-1))return 0;
    return !strcmp(t,want);
}
static void dl_apply(uint64_t self,uint64_t row) {
    unsigned char id[8]={0};
    if(!nt_peek(row,id,8)||id[0]!=2||id[1]>5)return;   /* not another player's library */
    uint64_t vt=0;if(!nt_peek(self,(unsigned char *)&vt,8)||vt!=0x2691140ull)return;
    dl_load();
    uint64_t icon=0;
    if(dl_img&&nt_peek(self+0xd8,(unsigned char *)&icon,8)&&icon)
        ((void (*)(uint64_t,const uint64_t *))0x209d700)(icon,&dl_img);
    /* hide USB STOP */
    uint64_t arr=0;int32_t n=0;unsigned hidden=0;
    if(nt_peek(self+0x40,(unsigned char *)&arr,8)&&nt_peek(self+0x50,(unsigned char *)&n,4)&&n>0&&n<=64) {
        for(int i=0;i<n;i++) {
            uint64_t k=0,kvt=0;
            if(!nt_peek(arr+8u*(unsigned)i,(unsigned char *)&k,8)||!k||!nt_peek(k,(unsigned char *)&kvt,8)||!kvt)continue;
            if(dl_lines<24){int32_t b[4]={0};nt_peek(k+0x20,(unsigned char *)b,16);uint64_t p=0;char t[24]={0};
                if(nt_peek(k+8,(unsigned char *)&p,8)&&p)nt_peek(p,(unsigned char *)t,sizeof t-1);
                for(unsigned j=0;j<sizeof t;j++)if(t[j]&&(t[j]<32||t[j]>126))t[j]='?';
                dl_lines++;lp_log("devlib: child %d '%s' at %d,%d %dx%d",i,t,b[0],b[1],b[2],b[3]);}
            if(dl_name_is(k,"UsbStopBtn")) {
                uint64_t setvis=0;if(!nt_peek(kvt+12*8,(unsigned char *)&setvis,8)||setvis!=0x20b1950ull)continue;
                ((void (*)(uint64_t,int))setvis)(k,0);hidden++;
            }
        }
    }
    static unsigned said;if(said<6){said++;lp_log("devlib: player %u's library row: icon %s, USB STOP hidden %u",id[1]+1u,dl_img?"Pro DJ Link":"unchanged",hidden);}
}
static uint64_t dl_hook(uint64_t self,uint64_t row) {
    uint64_t r=((uint64_t (*)(uint64_t,uint64_t))dl_orig)(self,row);
    if(dl_use)dl_apply(self,row);
    return r;
}
static void dl_install(const struct oqmod_api *api) {
    if(dl_installed||!api->hook||!api->ep136_ok||!api->ep136_ok())return;
    dl_installed=1;
    int ok=api->hook(0x1738090,0xa9b87bfdu,(void *)dl_hook,&dl_orig)==1;
    lp_log("devlib: library row hook %s",ok?"on":"off");
}
#endif

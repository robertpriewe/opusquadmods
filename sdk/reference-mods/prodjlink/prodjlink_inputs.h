/* 1.55: Pro DJ Link per mixer channel (user's design, 2026-11-12). Channel 3 or 4 set to
 * PRO DJ LINK = that channel's input is LINE (the CDJ/XDJ is wired there), its deck shows the
 * link player with the same number, and only that deck is locked against deck select. Picking
 * another input for that channel (the native menu) ends it.
 *
 * EP136 1.32 (Call Trace 1.3 run, hwtest-r123/from-TESTUSB-22):
 *  - usecase::mixer::Channel, vt 0x232c5a0, one object per mixer channel, 0xc0 apart. Slot 2
 *    select(this, const InputSource *) = 0x8b1018 (first insn ldrb w3,[x1] 0x39400023): the
 *    same call the input menu makes. Current input byte at +0x6c.
 *    InputSource: 0 DECK, 1 LINE, 2 PHONO, 3 BLUETOOTH (menu order DECK n/LINE/PHONO/BLUETOOTH).
 *  - Each object names its setting: *(*(obj+0x30)+0x20) = "InputSelect<n>", n = channel - 1.
 *    Found once by scanning writable memory for the vtable. */
#ifndef PRODJLINK_INPUTS_H
#define PRODJLINK_INPUTS_H
enum {CH_DECK=0,CH_LINE=1,CH_PHONO=2,CH_BT=3};
static uint64_t ch_obj[4];static unsigned ch_found,ch_scanned;
/* 1.56: the same scan collects the channel boxes (gui::mixer::ChannelWidget, vt 0x2800558) */
static uint64_t cw_cand[16];static unsigned cw_ncand;
static void cw_assign(void);   /* prodjlink_inputmenu.h */
static int ch_name(uint64_t obj) {
    uint64_t p1=0,p2=0;char s[16]={0};
    if(!nt_peek(obj+0x30,(unsigned char *)&p1,8)||!nt_peek(p1+0x20,(unsigned char *)&p2,8)||!nt_peek(p2,(unsigned char *)s,13))return -1;
    if(memcmp(s,"InputSelect",11)||s[11]<'0'||s[11]>'3'||s[12])return -1;
    return s[11]-'0';
}
/* One scan of the writable anonymous/heap mappings for the Channel vtable (on the tick thread,
 * about a few hundred MB at worst, in 1 MB pieces). */
static void *ch_scan_thread(void *unused) {
    (void)unused;
    FILE *f=fopen("/proc/self/maps","r");if(!f)return 0;
    static uint64_t buf[1<<17];   /* 1 MB */
    char line[512];unsigned regions=0;uint64_t bytes=0;
    while(fgets(line,sizeof line,f)) {
        unsigned long lo,hi;char perms[8]={0};
        if(sscanf(line,"%lx-%lx %7s",&lo,&hi,perms)!=3||perms[0]!='r'||perms[1]!='w')continue;
        const char *path=strchr(line,'/');if(path&&!strstr(line,"[heap]"))continue;   /* heap + anonymous only */
        if(hi-lo>(1ul<<30))continue;
        regions++;
        for(uint64_t a=lo;a<hi;a+=sizeof buf) {
            size_t n=hi-a<sizeof buf?(size_t)(hi-a):sizeof buf;
            if(!nt_peek(a,(unsigned char *)buf,n))continue;
            bytes+=n;
            for(size_t i=0;i<n/8;i++) {
                if(buf[i]==0x232c5a0ull) {
                    uint64_t obj=a+i*8;int c=ch_name(obj);
                    if(c>=0&&!ch_obj[c]){ch_obj[c]=obj;ch_found++;}
                } else if(buf[i]==0x2800558ull&&cw_ncand<16)cw_cand[cw_ncand++]=a+i*8;
            }
        }
    }
    fclose(f);
    lp_log("inputs: %u mixer channels found (%u regions, %llu MB scanned): %llx %llx %llx %llx",ch_found,regions,(unsigned long long)(bytes>>20),
        (unsigned long long)ch_obj[0],(unsigned long long)ch_obj[1],(unsigned long long)ch_obj[2],(unsigned long long)ch_obj[3]);
    cw_assign();
    __atomic_store_n(&ch_scanned,2u,__ATOMIC_RELEASE);
    return 0;
}
static void ch_scan(void) {   /* once, on its own thread: the tick never waits for it */
    if(ch_scanned)return;
    ch_scanned=1;pthread_t t;
    if(pthread_create(&t,0,ch_scan_thread,0))ch_scanned=2;else pthread_detach(t);
}
static int ch_input(unsigned channel) {   /* 1..4; -1 unknown */
    unsigned char v=0;if(channel<1||channel>4||!ch_obj[channel-1]||!nt_peek(ch_obj[channel-1]+0x6c,&v,1))return -1;
    return v;
}
static int ch_select(unsigned channel,unsigned char source) {
    if(channel<1||channel>4||!ch_obj[channel-1]||source>3)return 0;
    if(*(const uint32_t *)0x8b1018!=0x39400023u)return 0;
    uint64_t vt=0;if(!nt_peek(ch_obj[channel-1],(unsigned char *)&vt,8)||vt!=0x232c5a0ull)return 0;
    ((void (*)(uint64_t,const unsigned char *))0x8b1018)(ch_obj[channel-1],&source);
    return 1;
}

/* ---- PRO DJ LINK on channels 3/4 (settings link3 / link4) ---------------------------------- */
struct ch_link {unsigned active;int before;uint64_t off_since;};
static struct ch_link ch_links[2];
/* Which decks show a link player: bit d-1. */
static unsigned ch_link_mask;
static void ch_tick(const struct oqmod_api *api) {
    if(!api->ep136_ok||!api->ep136_ok())return;
    /* 1.68 (user): PRO DJ LINK never comes back by itself after a restart. A channel that had
     * it when the OPUS was switched off starts as DECK 3/4 (its saved input is LINE, from us);
     * PRO DJ LINK is chosen again by hand. Other inputs (LINE, PHONO, ...) are left alone. */
    static int boot_seen;static unsigned boot_link;
    if(!boot_seen){boot_seen=1;
        for(unsigned c=3;c<=4;c++){char key[8];snprintf(key,sizeof key,"link%u",c);
            if(api->setting_get(key,0)==1){boot_link|=1u<<c;api->setting_set(key,0);}}
        if(boot_link)lp_log("inputs: start-up: PRO DJ LINK was on for channel(s)%s%s: off, back to DECK",boot_link&8u?" 3":"",boot_link&16u?" 4":"");}
    ch_scan();
    if(boot_link&&__atomic_load_n(&ch_scanned,__ATOMIC_ACQUIRE)==2) {
        for(unsigned c=3;c<=4;c++)if(boot_link>>c&1u) {
            int in=ch_input(c);
            if(in==CH_LINE){ch_select(c,CH_DECK);lp_log("inputs: start-up: channel %u LINE -> DECK %u",c,c);}
        }
        boot_link=0;
    }
    uint64_t now=lk_now_ms();unsigned mask=0;
    for(unsigned i=0;i<2;i++) {
        unsigned channel=3+i;struct ch_link *c=&ch_links[i];
        char key[8];snprintf(key,sizeof key,"link%u",channel);
        if(__atomic_load_n(&ch_scanned,__ATOMIC_ACQUIRE)!=2||!ch_obj[channel-1])continue;   /* not found (yet) */
        int want=api->setting_get(key,0)==1,input=ch_input(channel);
        if(want&&!c->active) {                       /* switched on: LINE + mirror + lock */
            c->before=input;c->active=1;c->off_since=0;
            if(input!=CH_LINE)ch_select(channel,CH_LINE);
            lp_log("inputs: channel %u = PRO DJ LINK (input was %d, now LINE)",channel,input);
        } else if(!want&&c->active) {                /* switched off: the input from before */
            c->active=0;
            int back=c->before>=0&&c->before!=CH_LINE?c->before:CH_DECK;
            if(ch_input(channel)==CH_LINE)ch_select(channel,(unsigned char)back);
            lp_log("inputs: channel %u PRO DJ LINK off (input back to %d)",channel,back);
        } else if(c->active&&input>=0&&input!=CH_LINE) {   /* another input picked in the menu */
            if(!c->off_since)c->off_since=now;
            else if(now-c->off_since>800){c->active=0;api->setting_set(key,0);
                lp_log("inputs: channel %u input changed to %d in the menu: PRO DJ LINK off",channel,input);}
        } else c->off_since=0;
        if(c->active)mask|=1u<<(channel-1);
    }
    __atomic_store_n(&ch_link_mask,mask,__ATOMIC_RELEASE);
}
#endif

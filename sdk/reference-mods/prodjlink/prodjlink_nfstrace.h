#ifndef OQ_PRODJLINK_NFSTRACE_H
#define OQ_PRODJLINK_NFSTRACE_H
/* Research (R122 / EP136 1.32): which entry points of EP136's nfs::NfsManager run while a
 * player with a USB is on the link. Its interface vtables (bases from the class's
 * vmi typeinfo 0x24a39f8): INfsLinkEventHandler +696 (vt 0x24a3bb8), INfsManagerNotifyDetect
 * +704 (0x24a3bf8), INfsManagerNotifyDisconnect +712 (0x24a3c20), INfsMediaEventHandler
 * +720 (0x24a3c48), INfsManagerEventListenerEntry +728 (0x24a3c78),
 * IUdp_ExportList_Listener +736 (0x24a3ca0), INfsRbmCertification +744 (0x24a3cc8).
 * Each non-destructor slot is wrapped: the call is logged (first 24 bytes behind x1, x2)
 * and passed on unchanged. vtables are read-only data, so api->patch is the right tool. */
#include <sys/mman.h>
#define NT_SLOTS 11
static const struct {uintptr_t slot,fn;const char *name;} nt_slots[NT_SLOTS]={
    {0x24a3bc8,0x109d618,"LinkEvent[2]"},{0x24a3bd0,0x109db68,"LinkEvent[3]"},
    {0x24a3bd8,0x109e0f0,"LinkEvent[4]"},{0x24a3be0,0x109ebe0,"LinkEvent[5]"},
    {0x24a3c08,0x109f138,"NotifyDetect"},{0x24a3c30,0x109f690,"NotifyDisconnect"},
    {0x24a3c58,0x109abf8,"MediaEvent[2]"},{0x24a3c60,0x109abf0,"MediaEvent[3]"},
    {0x24a3c88,0x109b548,"EventListenerEntry"},{0x24a3cb0,0x10a06f8,"ExportList"},
    {0x24a3cd8,0x109e658,"RbmCertification"}};
typedef uint64_t (*nt_fn)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static void *nt_orig[NT_SLOTS];
static unsigned nt_calls[NT_SLOTS],nt_installed;
static int nt_peek(uint64_t a,unsigned char *o,size_t n){
    struct iovec l={o,n},r={(void *)(uintptr_t)a,n};
    return a>0x400000&&a<0x1000000000000ull&&syscall(SYS_process_vm_readv,getpid(),&l,1,&r,1,0)==(long)n;
}
/* Link media for an all-in-one (R122 research, option "nfsdetect"):
 * nfs::NfsManager::onPlink_NfsDetect(const LinkMemberID&, StorageDeviceIfID) - entry thunk
 * 0x109f138 on the INfsManagerNotifyDetect base (NfsManager+704) - mounts a member's SD (2)
 * or USB (3). Its task (0x109bcc8) first returns when this unit's own member kind is 3
 * (all-in-one): `b.eq` at 0x109bdd8 (0x54fffe40) after `cmp w0,#3`. The XDJ-AZ in PRO DJ
 * LINK Mode registers as kind 2, so the check passes there. 1.14 turns that branch into a
 * NOP and, 2 s after EP136 adds another player (LinkEvent[4] = add member, x0 = base+696,
 * x1 = its 4-byte LinkMemberID then IP), calls the entry itself for that player's USB. */
static uint64_t nt_base;static uint32_t nt_member;static uint64_t nt_member_ms;
static unsigned nt_detect_calls,nt_gate_patched;
static unsigned char nt_link_ip[4];   /* the mounted player (network order) */
/* The same "own kind == 3 (all-in-one): return" check guards the NFS client's mount
 * request too: FUN_01098318 (b.eq at 0x1098354). Both branches become NOPs. */
static int nt_patch_one(uint32_t *at,uint32_t expected) {
    if(*at==0xd503201fu)return 1;
    if(*at!=expected)return 0;
    long pg=sysconf(_SC_PAGESIZE);uintptr_t base=(uintptr_t)at&~(uintptr_t)(pg-1);
    if(mprotect((void *)base,(size_t)pg,PROT_READ|PROT_WRITE|PROT_EXEC))return 0;
    __atomic_store_n(at,0xd503201fu,__ATOMIC_RELEASE);
    mprotect((void *)base,(size_t)pg,PROT_READ|PROT_EXEC);
    __builtin___clear_cache((char *)at,(char *)at+4);
    return 1;
}
static int nt_patch_gate(void) {
    int a=nt_patch_one((uint32_t *)0x109bdd8,0x54fffe40u),b=nt_patch_one((uint32_t *)0x1098354,0x54000160u);
    lp_log("nfsdetect: gates %s / %s",a?"open":"NOT patched",b?"open":"NOT patched");
    return a&&b;
}
/* FUN_01098318 (NfsClientMgr: queue a mount of member x1's storage *x3): logged */
static void *nt_mount_orig;static unsigned nt_mount_calls;
static void nt_mount_hook(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5) {
    unsigned char m[8]={0},info[24]={0},st=0;
    nt_peek(a1,m,4);nt_peek(a2,info,sizeof info);nt_peek(a3,&st,1);
    unsigned char self[4]={0};nt_peek(a0+0x1bc,self,4);
    char h1[60],h2[60];lp_hex(h1,sizeof h1,info,sizeof info);lp_hex(h2,sizeof h2,self,4);
    if(nt_mount_calls++<20)lp_log("nfsclient mount request: member %02x%02x%02x%02x storage %u info %s self %s",m[0],m[1],m[2],m[3],st,h1,h2);
    /* EP136 byte-swaps the IP before formatting the FuseFilsine command (FUN_01094b70), so
     * this path expects it stored reversed; from the member lookup it arrives in network
     * order (a9 fe .. ..) and the command read "236.172.254.169" (2026-11-11). Reversed here. */
    unsigned char fixed[32];
    if(nt_peek(a2,fixed,sizeof fixed)&&fixed[0]==0xa9&&fixed[1]==0xfe)memcpy(nt_link_ip,fixed,4);
    if(nt_peek(a2,fixed,sizeof fixed)&&fixed[0]==0xa9&&fixed[1]==0xfe) {
        unsigned char x=fixed[0];fixed[0]=fixed[3];fixed[3]=x;x=fixed[1];fixed[1]=fixed[2];fixed[2]=x;
        a2=(uint64_t)(uintptr_t)fixed;
        if(nt_mount_calls<=20)lp_log("nfsclient: member IP passed byte-reversed (%u.%u.%u.%u)",fixed[3],fixed[2],fixed[1],fixed[0]);
    }
    ((void (*)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t))nt_mount_orig)(a0,a1,a2,a3,a4,a5);
}
static void nt_detect_tick(int enabled) {
    if(!enabled||!nt_base||!nt_member||!nt_member_ms||lk_now_ms()-nt_member_ms<2000)return;
    nt_member_ms=0;
    if(!nt_gate_patched)nt_gate_patched=nt_patch_gate();
    if(!nt_gate_patched){lp_log("nfsdetect: gate at 0x109bdd8 not as expected, not calling");return;}
    if(*(const uint32_t *)0x109f138!=0xd10b0000u){lp_log("nfsdetect: entry 0x109f138 not as expected");return;}
    uint32_t id=nt_member;
    typedef void (*detect_fn)(uint64_t,const uint32_t *,uint64_t);
    /* StorageDeviceIfType here: 2 = USB ("/C/"), 3 = SD ("/B/") (FUN_01098318) */
    ((detect_fn)0x109f138)(nt_base+704,&id,2);
    nt_detect_calls++;
    lp_log("nfsdetect: called onPlink_NfsDetect(member %08x, USB=2) (%u)",id,nt_detect_calls);
}
/* printable text behind a pointer (juce::String char*) */
static void text_or_empty(uint64_t p,char *out,size_t cap) {
    unsigned char b[64];out[0]=0;if(!nt_peek(p,b,sizeof b))return;
    size_t n=0;while(n+1<cap&&n<sizeof b&&b[n]>=32&&b[n]<127){out[n]=(char)b[n];n++;}out[n]=0;
}
static void nt_note(unsigned k,uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3) {
    if(k<4&&a0&&!nt_base)nt_base=a0-696;   /* INfsLinkEventHandler base -> NfsManager */
    if(k==2&&a1) {   /* LinkEvent[4]: a member was added */
        uint32_t id;unsigned char ipb[4];
        if(nt_peek(a1,(unsigned char *)&id,4)&&nt_peek(a1+4,ipb,4)&&(id&0xff)==2) {
            uint32_t ip=(uint32_t)ipb[0]<<24|(uint32_t)ipb[1]<<16|(uint32_t)ipb[2]<<8|ipb[3];
            if(ip!=lk_my_ip){nt_member=id;nt_member_ms=lk_now_ms();}
        }
    }
    if(nt_calls[k]++>=40)return;
    unsigned char b1[24],b2[24];char h1[60]="-",h2[60]="-";
    if(nt_peek(a1,b1,sizeof b1))lp_hex(h1,sizeof h1,b1,sizeof b1);
    if(nt_peek(a2,b2,sizeof b2))lp_hex(h2,sizeof h2,b2,sizeof b2);
    lp_log("nfsmgr %s #%u x1=%llx [%s] x2=%llx [%s] x3=%llx",nt_slots[k].name,nt_calls[k],(unsigned long long)a1,h1,(unsigned long long)a2,h2,(unsigned long long)a3);
}
#define NT_WRAP(k) static uint64_t nt_w##k(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5){ \
    nt_note(k,a0,a1,a2,a3);return ((nt_fn)nt_orig[k])(a0,a1,a2,a3,a4,a5);}
NT_WRAP(0) NT_WRAP(1) NT_WRAP(2) NT_WRAP(3) NT_WRAP(4) NT_WRAP(5) NT_WRAP(6) NT_WRAP(7) NT_WRAP(8) NT_WRAP(9) NT_WRAP(10)
static void *const nt_wrappers[NT_SLOTS]={nt_w0,nt_w1,nt_w2,nt_w3,nt_w4,nt_w5,nt_w6,nt_w7,nt_w8,nt_w9,nt_w10};
static void nt_install(const struct oqmod_api *api) {
    if(nt_installed||!api->ep136_ok||!api->ep136_ok())return;
    unsigned ok=0;
    for(unsigned k=0;k<NT_SLOTS;k++)if(api->patch(nt_slots[k].slot,nt_slots[k].fn,nt_wrappers[k],&nt_orig[k])==1)ok++;
    int mh=api->hook&&api->hook(0x1098318,0xa9b77bfdu,(void *)nt_mount_hook,&nt_mount_orig)==1;
    nt_installed=1;lp_log("nfsmgr trace: %u of %u slots, mount hook %s",ok,NT_SLOTS,mh?"on":"off");
}
#endif

#ifndef OQ_PRODJLINK_SYNCTRACE_H
#define OQ_PRODJLINK_SYNCTRACE_H
/* Research (R123 / EP136 1.32): EP136's tempo-master system at runtime.
 * usecase::sync::LocalSyncMember (vt 0x2393dc0, one per deck; +0x374 master id, +0x380 own
 * id, +0x393/+0x394/+0x39c/+0x3e4 flags; abandonMasterPost body FUN_00a15328(this,bool)),
 * LinkedSyncMember (vt 0x238ffe0, link players), MasterPost (vt 0x2394468). Every
 * class-specific virtual slot goes through a trampoline that saves x0-x8, x30 and q0-q7,
 * logs the object (on change) and jumps to the original. Nothing is changed. */
#define ST_SLOTS 23
static const struct {char cls;uintptr_t slot,fn;} st_slots[ST_SLOTS]={
    {'L',0x2393e40,0xa109d8},
    {'L',0x2393e48,0xa186b0},
    {'L',0x2393e50,0xa117d8},
    {'L',0x2393e58,0xa11b78},
    {'L',0x2393e60,0xa0ecc0},
    {'L',0x2393e68,0xa0efd0},
    {'L',0x2393e70,0xa0f2d0},
    {'L',0x2393e78,0xa0dc38},
    {'L',0x2393e80,0xa0f5e0},
    {'L',0x2393e88,0xa0f8e0},
    {'L',0x2393e90,0xa0fbf0},
    {'L',0x2393e98,0xa15bc0},
    {'K',0x2390040,0xa0cac8},
    {'K',0x2390048,0xa07068},
    {'K',0x2390050,0xa0a188},
    {'K',0x2390058,0xa0a7a8},
    {'K',0x2390060,0xa07bb0},
    {'K',0x2390068,0xa08138},
    {'K',0x2390070,0xa086a8},
    {'K',0x2390078,0xa07070},
    {'P',0x2394478,0xa1ce60},
    {'P',0x2394480,0xa1cd50},
    {'P',0x2394488,0xa1e270}};
void *st_orig[ST_SLOTS];
static unsigned st_lines,st_installed;
struct st_obj {uint64_t self;unsigned char sig[48];};
static struct st_obj st_seen[24];static unsigned st_nseen;
static pthread_mutex_t st_lock=PTHREAD_MUTEX_INITIALIZER;
uint64_t st_masterpost;   /* EP136's MasterPost object, once seen */
void st_note(unsigned k,uint64_t self,uint64_t a1,uint64_t a2) {
    if(k>=ST_SLOTS||!self)return;
    if(st_slots[k].cls=='P') {   /* every call: its state +0x40/+0x44 and, for post (slot 4), the id and flag */
        st_masterpost=self;
        unsigned char state[8]={0},id[16]={0};nt_peek(self+0x40,state,8);
        int is_post=st_slots[k].fn==0xa1e270;
        if(is_post)nt_peek(a1,id,16);
        static unsigned last_state_lines;
        if(st_lines<250&&(is_post||last_state_lines<20)) {
            if(!is_post)last_state_lines++;
            st_lines++;char h1[40],h2[40];lp_hex(h1,sizeof h1,state,8);lp_hex(h2,sizeof h2,id,16);
            lp_log("masterpost %s state %s%s%s flag %llu",is_post?"POST":"get/listen",h1,is_post?" id ":"",is_post?h2:"",(unsigned long long)(a2&255));
        }
        return;
    }
    char cls=st_slots[k].cls;unsigned char sig[48];memset(sig,0,sizeof sig);
    if(cls=='L'){nt_peek(self+0x370,sig,0x18);nt_peek(self+0x390,sig+0x18,0x10);unsigned char f;nt_peek(self+0x3e4,&f,1);sig[0x28]=f;}
    else if(cls=='K'){nt_peek(self+0x2c8,sig,0x30);}
    else {nt_peek(self+8,sig,0x30);}
    pthread_mutex_lock(&st_lock);
    unsigned i;for(i=0;i<st_nseen;i++)if(st_seen[i].self==self)break;
    int changed=i==st_nseen||memcmp(st_seen[i].sig,sig,sizeof sig);
    if(i==st_nseen&&st_nseen<24){st_seen[st_nseen].self=self;st_nseen++;}
    if(i<24)memcpy(st_seen[i].sig,sig,sizeof sig);
    pthread_mutex_unlock(&st_lock);
    if(!changed||st_lines>=250)return;
    st_lines++;char h1[100];lp_hex(h1,sizeof h1,sig,cls=='L'?0x29:0x30);
    lp_log("sync %s %llx slot %u: %s",cls=='L'?"local":cls=='K'?"linked":"masterpost",(unsigned long long)self,(unsigned)((st_slots[k].slot-(cls=='L'?0x2393dc0:cls=='K'?0x238ffe0:0x2394468))/8),h1);
}
/* trampoline k: save everything, st_note(k, x0), restore, jump to the original */
#define ST_STUB(k) __asm__(".text\n.p2align 2\n.globl st_w" #k "\n.type st_w" #k ",%function\nst_w" #k ":\n" \
    "sub sp,sp,#256\nstp x0,x1,[sp]\nstp x2,x3,[sp,#16]\nstp x4,x5,[sp,#32]\nstp x6,x7,[sp,#48]\nstp x8,x30,[sp,#64]\n" \
    "stp q0,q1,[sp,#80]\nstp q2,q3,[sp,#112]\nstp q4,q5,[sp,#144]\nstp q6,q7,[sp,#176]\nstp x29,x9,[sp,#208]\n" \
    "mov x3,x2\nmov x2,x1\nmov x1,x0\nmov w0," #k "\nbl st_note\n" \
    "ldp q0,q1,[sp,#80]\nldp q2,q3,[sp,#112]\nldp q4,q5,[sp,#144]\nldp q6,q7,[sp,#176]\nldp x29,x9,[sp,#208]\n" \
    "ldp x0,x1,[sp]\nldp x2,x3,[sp,#16]\nldp x4,x5,[sp,#32]\nldp x6,x7,[sp,#48]\nldp x8,x30,[sp,#64]\nadd sp,sp,#256\n" \
    "adrp x16,st_orig\nadd x16,x16,:lo12:st_orig\nldr x16,[x16,#" #k "*8]\nbr x16\n.size st_w" #k ",.-st_w" #k "\n");
ST_STUB(0)
ST_STUB(1)
ST_STUB(2)
ST_STUB(3)
ST_STUB(4)
ST_STUB(5)
ST_STUB(6)
ST_STUB(7)
ST_STUB(8)
ST_STUB(9)
ST_STUB(10)
ST_STUB(11)
ST_STUB(12)
ST_STUB(13)
ST_STUB(14)
ST_STUB(15)
ST_STUB(16)
ST_STUB(17)
ST_STUB(18)
ST_STUB(19)
ST_STUB(20)
ST_STUB(21)
ST_STUB(22)
extern char st_w0[],st_w1[],st_w2[],st_w3[],st_w4[],st_w5[],st_w6[],st_w7[],st_w8[],st_w9[],st_w10[],st_w11[],st_w12[],st_w13[],st_w14[],st_w15[],st_w16[],st_w17[],st_w18[],st_w19[],st_w20[],st_w21[],st_w22[];
static void *const st_wrappers[ST_SLOTS]={st_w0,st_w1,st_w2,st_w3,st_w4,st_w5,st_w6,st_w7,st_w8,st_w9,st_w10,st_w11,st_w12,st_w13,st_w14,st_w15,st_w16,st_w17,st_w18,st_w19,st_w20,st_w21,st_w22};
/* LocalSyncMember take/abandon task bodies (R123 / EP136 1.32):
 *   take    0xa14460(task): this = task+0x18
 *   abandon 0xa15740(task): bool at task+0x18, this at task+0x20  -> FUN_00a15328(this,bool)
 * Both logged with the member's fields; the last member that took master is kept so the
 * mod can give master up natively when another player takes it (st_abandon). */
static void *st_take_orig,*st_abandon_orig,*st_inner_orig;
static uint64_t st_master_lsm;      /* LocalSyncMember that last took master */
static unsigned st_takes,st_abandons,st_native;
uint64_t st_lsm[4];static unsigned st_nlsm;
static void st_lsm_note(uint64_t lsm){for(unsigned i=0;i<st_nlsm;i++)if(st_lsm[i]==lsm)return;if(st_nlsm<4)st_lsm[st_nlsm++]=lsm;}
static void st_member_log(const char *what,uint64_t lsm,unsigned flag) {
    unsigned char f[0x80]={0};nt_peek(lsm+0x370,f,0x80);
    char h[300];lp_hex(h,sizeof h,f,0x80);
    if(!LP_QUIET&&st_lines<250){st_lines++;lp_log("sync %s member %llx flag %u +370: %s",what,(unsigned long long)lsm,flag,h);}
}
static void st_take_hook(uint64_t task) {
    uint64_t lsm=0;nt_peek(task+0x18,(unsigned char *)&lsm,8);
    st_takes++;st_master_lsm=lsm;st_lsm_note(lsm);st_member_log("TAKE",lsm,0);
    ((void (*)(uint64_t))st_take_orig)(task);
    st_member_log("TAKE done",lsm,0);
}
static void st_abandon_hook(uint64_t task) {
    uint64_t lsm=0;unsigned char flag=0;nt_peek(task+0x20,(unsigned char *)&lsm,8);nt_peek(task+0x18,&flag,1);
    st_abandons++;st_member_log("ABANDON",lsm,flag);
    ((void (*)(uint64_t))st_abandon_orig)(task);
    st_member_log("ABANDON done",lsm,flag);
}
static void st_inner_hook(uint64_t lsm,uint64_t flag) {
    st_lsm_note(lsm);
    static uint64_t last_lsm;static unsigned last_flag=9;
    if(lsm!=last_lsm||(unsigned)(flag&255)!=last_flag){last_lsm=lsm;last_flag=(unsigned)(flag&255);st_member_log("abandon-inner",lsm,last_flag);}
    ((void (*)(uint64_t,uint64_t))st_inner_orig)(lsm,flag);
}
/* give master up natively: run the abandon task body for the deck that holds it */
static void st_abandon(unsigned flag) {
    if(!st_master_lsm||!st_abandon_orig)return;
    uint64_t fake[6];memset(fake,0,sizeof fake);
    ((unsigned char *)fake)[0x18]=(unsigned char)flag;fake[4]=st_master_lsm;
    st_native++;lp_log("sync: OPUS gives up master natively (member %llx, flag %u)",(unsigned long long)st_master_lsm,flag);
    st_abandon_hook((uint64_t)(uintptr_t)fake);
}
/* LinkedSyncMember (one per link player): constructor 0xa0af40 (x0 this; +0x2d0 kind,
 * +0x2d4 id; +0x2dc.. beat-sync state) and slot 17 0xa08138 = onReciveUdp_BeatSyncGetMasterReq
 * (this, LinkMemberID*, ?, ?, request*): a link player's master request (0x26); its task
 * body 0xa0a060 checks *LinkMemberID == id(+0x2d0) and *request == byte this+0x2f9, then
 * asks the master exchanger (this+0x318, slot +0x20) to move master. Both logged. */
typedef uint64_t (*st_fn8)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t);
static void *st_lctor_orig,*st_lreq_orig;
uint64_t st_linked[8];static unsigned st_nlinked;
static void st_linked_log(const char *what,uint64_t self) {
    unsigned char f[0x58]={0};nt_peek(self+0x2d0,f,sizeof f);
    char h[200];lp_hex(h,sizeof h,f,sizeof f);
    if(!LP_QUIET&&st_lines<250){st_lines++;lp_log("sync linked %s %llx +2d0: %s",what,(unsigned long long)self,h);}
}
static uint64_t st_lctor_hook(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5,uint64_t a6,uint64_t a7) {
    uint64_t r=((st_fn8)st_lctor_orig)(a0,a1,a2,a3,a4,a5,a6,a7);
    if(st_nlinked<8)st_linked[st_nlinked++]=a0;
    st_linked_log("CREATED",a0);
    return r;
}
static uint64_t st_lreq_hook(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5,uint64_t a6,uint64_t a7) {
    unsigned char id[8]={0},req[16]={0};nt_peek(a1,id,8);nt_peek(a4,req,16);
    char h1[20],h2[40];lp_hex(h1,sizeof h1,id,8);lp_hex(h2,sizeof h2,req,16);
    static unsigned req_lines;if(req_lines<40){req_lines++;lp_log("sync linked MASTER REQUEST to %llx: member %s x2 %llx x3 %llx request %s",(unsigned long long)a0,h1,(unsigned long long)a2,(unsigned long long)a3,h2);}
    {unsigned char f[0x58]={0};nt_peek(a0+0x2d0,f,sizeof f);char h[200];lp_hex(h,sizeof h,f,sizeof f);if(req_lines<40)lp_log("sync linked at request %llx +2d0: %s",(unsigned long long)a0,h);}
    return ((st_fn8)st_lreq_orig)(a0,a1,a2,a3,a4,a5,a6,a7);
}
/* The master exchanger the request goes to (linked member +0x318, method +0x20): found at
 * runtime from the XDJ's linked member, its slots 2..7 wrapped (8-arg C passthrough). */
#define SX_SLOTS 6
void *sx_orig[SX_SLOTS];static unsigned sx_done,sx_lines;
void sx_note(unsigned k,uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3) {
    unsigned char b1[16]={0},b2[16]={0};nt_peek(a1,b1,16);nt_peek(a2,b2,16);char h1[40],h2[40];lp_hex(h1,sizeof h1,b1,16);lp_hex(h2,sizeof h2,b2,16);
    if(k==0)return;
    if(!LP_QUIET&&sx_lines<120){sx_lines++;lp_log("sync EXCHANGER slot %u this %llx x1 [%s] x2 [%s] x3 %llx",k+2,(unsigned long long)a0,h1,h2,(unsigned long long)a3);}
}
#define SX_STUB(k) __asm__(".text\n.p2align 2\n.globl sx_w" #k "\n.type sx_w" #k ",%function\nsx_w" #k ":\n" \
    "sub sp,sp,#256\nstp x0,x1,[sp]\nstp x2,x3,[sp,#16]\nstp x4,x5,[sp,#32]\nstp x6,x7,[sp,#48]\nstp x8,x30,[sp,#64]\n" \
    "stp q0,q1,[sp,#80]\nstp q2,q3,[sp,#112]\nstp q4,q5,[sp,#144]\nstp q6,q7,[sp,#176]\nstp x29,x9,[sp,#208]\n" \
    "mov x4,x3\nmov x3,x2\nmov x2,x1\nmov x1,x0\nmov w0," #k "\nbl sx_note\n" \
    "ldp q0,q1,[sp,#80]\nldp q2,q3,[sp,#112]\nldp q4,q5,[sp,#144]\nldp q6,q7,[sp,#176]\nldp x29,x9,[sp,#208]\n" \
    "ldp x0,x1,[sp]\nldp x2,x3,[sp,#16]\nldp x4,x5,[sp,#32]\nldp x6,x7,[sp,#48]\nldp x8,x30,[sp,#64]\nadd sp,sp,#256\n" \
    "adrp x16,sx_orig\nadd x16,x16,:lo12:sx_orig\nldr x16,[x16,#" #k "*8]\nbr x16\n.size sx_w" #k ",.-sx_w" #k "\n");
/* Slot 2 = 0xa1a128 member-state lookup (x0 exchanger, x1 PlayerID*, x8 record out).
 * EP136's LocalSyncMembers ask only for link members {2, kind 4, index 0/1} (0xa14b88:
 * the two external decks 3/4), never for {2, kind 2, index n} = a Pro DJ Link player,
 * so an XDJ/CDJ can never be the master for EP136's own hand-over (0xa14df0), master
 * view (0xa14490) or auto-take (0xa158e0). sx_w0 answers {2,4,i} with the record of the
 * player taking deck 3+i (option deckalias), then sx_fix_record turns "yield to player
 * 1/2" (our decks' numbers) into the yield number EP136's own deck uses (+0x3a9). */
uint32_t sx_alias[2][4];            /* {2, id lo, id hi, 0} of player 3 / 4, tag 0 = none */
unsigned char sx_deck_no[2];        /* EP136's own number for deck 1 / 2 (LSM +0x3a9) */
_Atomic unsigned sx_alias_on,sx_mapped,sx_fixed;
const uint32_t *sx_map_id(const uint32_t *id) {
    if(!id||!__atomic_load_n(&sx_alias_on,__ATOMIC_RELAXED))return 0;
    uint32_t tag=id[0],lo=id[1],hi=id[2];
    if(tag!=2||(lo&0xff)!=4||hi!=0||(lo>>16)!=0)return 0;
    unsigned idx=(lo>>8)&0xff;if(idx>1)return 0;
    if(sx_alias[idx][0]!=2)return 0;
    __atomic_fetch_add(&sx_mapped,1,__ATOMIC_RELAXED);
    return sx_alias[idx];
}
void sx_fix_record(unsigned char *rec) {
    if(!rec)return;
    unsigned y=rec[0x10];
    /* only when EP136's two decks have different numbers, else both would take master */
    if((y==1||y==2)&&sx_deck_no[0]!=sx_deck_no[1]){unsigned char n=sx_deck_no[y-1];if(n&&n!=0xff){rec[0x10]=n;__atomic_fetch_add(&sx_fixed,1,__ATOMIC_RELAXED);}}
}
__asm__(".text\n.p2align 2\n.globl sx_w0\n.type sx_w0,%function\nsx_w0:\n"
    "sub sp,sp,#272\nstp x0,x1,[sp]\nstp x2,x3,[sp,#16]\nstp x4,x5,[sp,#32]\nstp x6,x7,[sp,#48]\nstp x8,x30,[sp,#64]\n"
    "stp q0,q1,[sp,#80]\nstp q2,q3,[sp,#112]\nstp q4,q5,[sp,#144]\nstp q6,q7,[sp,#176]\nstp x29,x9,[sp,#208]\n"
    "mov x0,x1\nbl sx_map_id\nstr x0,[sp,#224]\ncbz x0,1f\nstr x0,[sp,#8]\n1:\n"
    "ldp q0,q1,[sp,#80]\nldp q2,q3,[sp,#112]\nldp q4,q5,[sp,#144]\nldp q6,q7,[sp,#176]\nldp x29,x9,[sp,#208]\n"
    "ldp x0,x1,[sp]\nldp x2,x3,[sp,#16]\nldp x4,x5,[sp,#32]\nldp x6,x7,[sp,#48]\nldr x8,[sp,#64]\n"
    "adrp x16,sx_orig\nadd x16,x16,:lo12:sx_orig\nldr x16,[x16]\nblr x16\n"
    "ldr x9,[sp,#224]\ncbz x9,2f\nstp x0,x1,[sp]\nldr x0,[sp,#64]\nbl sx_fix_record\nldp x0,x1,[sp]\n2:\n"
    "ldr x30,[sp,#72]\nadd sp,sp,#272\nret\n.size sx_w0,.-sx_w0\n");
SX_STUB(1) SX_STUB(2) SX_STUB(3) SX_STUB(4) SX_STUB(5)
extern char sx_w0[],sx_w1[],sx_w2[],sx_w3[],sx_w4[],sx_w5[];
static void *const sx_wrappers[SX_SLOTS]={sx_w0,sx_w1,sx_w2,sx_w3,sx_w4,sx_w5};
/* LocalSyncMember hand-over (R123 / EP136 1.32). receiveRequestTakeMasterPost body
 * 0xa11368 (task +0x18 = LSM): the master deck accepts a link player's request and sets
 * +0x39c = that player's number ("yielding"). changedOtherSyncMemberState body 0xa15298
 * (task +0x18 PlayerID variant, +0x28 LSM) -> 0xa150a0 -> 0xa10ca0 gives master up
 * (+0x394 = 0) when another member is master, but ONLY while +0x39c == 0xff. Newer
 * players finish the hand-over so it gets cleared; with an XDJ-1000MK2 it never is, so
 * the deck keeps master forever. st_release: once the player it yields to really is
 * master (its LinkedSyncMember record +0x2e4 = 1), clear +0x39c and rerun EP136's own
 * changedOtherSyncMemberState for that player. Both bodies hooked to find the LSMs. */

/* 1.44 research: who makes the deck master again ~200 ms after the release? Candidates
 * that write LSM +0x394 (found by scanning strb #0x394): 0xa10e68 (in changedOther...),
 * 0xa12fd0 (take), 0xa13168 (from LSM slot 27), 0xa158e0 (task body?), and MasterPost
 * post 0xa1e270 (who posts what). Logged with the caller (LR) for 4 s after a release
 * (post: always, own budget). */
static uint64_t st_watch_until;
static unsigned st_wlines;
static uint64_t st_find_lsm(uint64_t x0) {
    uint64_t vt=0;nt_peek(x0,(unsigned char *)&vt,8);if(vt==0x2393dc0)return x0;
    for(unsigned o=0x18;o<=0x30;o+=8){uint64_t p=0;nt_peek(x0+o,(unsigned char *)&p,8);vt=0;nt_peek(p,(unsigned char *)&vt,8);if(vt==0x2393dc0)return p;}
    return 0;
}
#define ST_WATCH(name,addr,first) \
static void *name##_orig; \
static uint64_t name##_hook(uint64_t a0,uint64_t a1,uint64_t a2,uint64_t a3,uint64_t a4,uint64_t a5,uint64_t a6,uint64_t a7) { \
    uint64_t lr=(uint64_t)(uintptr_t)__builtin_return_address(0); \
    int watch=lk_now_ms()<st_watch_until; uint64_t lsm=watch?st_find_lsm(a0):0; unsigned char m0=9,y0=9,m1=9,y1=9; \
    if(lsm){nt_peek(lsm+0x394,&m0,1);nt_peek(lsm+0x39c,&y0,1);} \
    uint64_t r=((st_fn8)name##_orig)(a0,a1,a2,a3,a4,a5,a6,a7); \
    if(lsm){nt_peek(lsm+0x394,&m1,1);nt_peek(lsm+0x39c,&y1,1);} \
    if(!LP_QUIET&&watch&&st_wlines<80){st_wlines++;lp_log("sync watch %s x0 %llx lr %llx member %llx master %u->%u yield %02x->%02x",#name,(unsigned long long)a0,(unsigned long long)lr,(unsigned long long)lsm,m0,m1,y0,y1);} \
    return r; }
ST_WATCH(w_other,0xa10e68,0)
ST_WATCH(w_take,0xa12fd0,0)
ST_WATCH(w_slot27,0xa13168,0)
ST_WATCH(w_task,0xa158e0,0)
static void *st_post_orig;static unsigned st_plines;
static void st_post_hook(uint64_t mp,uint32_t *id,uint64_t flag) {
    uint64_t lr=(uint64_t)(uintptr_t)__builtin_return_address(0);
    uint32_t v[3]={9,9,9};nt_peek((uint64_t)(uintptr_t)id,(unsigned char *)v,12);
    uint32_t b[3]={9,9,9};nt_peek(mp+0x40,(unsigned char *)b,12);
    ((void (*)(uint64_t,uint32_t *,uint64_t))st_post_orig)(mp,id,flag);
    uint32_t c[3]={9,9,9};nt_peek(mp+0x40,(unsigned char *)c,12);
    if(st_plines<80&&(memcmp(b,c,12)||lr<0x3000000)){st_plines++;lp_log("sync POST by %llx: %u/%x/%x flag %u : %u/%x/%x -> %u/%x/%x",(unsigned long long)lr,v[0],v[1],v[2],(unsigned)(flag&255),b[0],b[1],b[2],c[0],c[1],c[2]);}
}
static uint64_t st_linked_for(unsigned player);
static void *st_reqtake_orig,*st_changed_orig;
static unsigned st_hlines;
static void st_reqtake_hook(uint64_t task) {
    uint64_t lsm=0;nt_peek(task+0x18,(unsigned char *)&lsm,8);st_lsm_note(lsm);
    if(st_hlines<40){st_hlines++;st_member_log("REQUEST-TAKE",lsm,0);}
    ((void (*)(uint64_t))st_reqtake_orig)(task);
    if(st_hlines<40){st_hlines++;st_member_log("REQUEST-TAKE done",lsm,0);}
}
static void st_changed_hook(uint64_t task) {
    uint64_t lsm=0;nt_peek(task+0x28,(unsigned char *)&lsm,8);st_lsm_note(lsm);
    ((void (*)(uint64_t))st_changed_orig)(task);
}
static unsigned st_releases;
/* Fallback only (deckalias makes EP136 finish the hand-over itself in 0xa14df0): if a
 * deck still yields to a player that has been master for 2.5 s, clear the yield and
 * rerun EP136's changedOtherSyncMemberState for that player. Nothing else is forced. */
static void st_release_tick(void) {
    if(!st_changed_orig)return;
    static uint64_t since[4];uint64_t now=lk_now_ms();
    for(unsigned i=0;i<st_nlsm&&i<4;i++) {
        uint64_t lsm=st_lsm[i];unsigned char master=0,yield=0xff;
        if(!nt_peek(lsm+0x394,&master,1)||!nt_peek(lsm+0x39c,&yield,1)||!master||yield==0xff){since[i]=0;continue;}
        uint64_t lm=st_linked_for(yield);if(!lm){since[i]=0;continue;}
        unsigned char other_master=0;nt_peek(lm+0x2e4,&other_master,1);
        if(!other_master){since[i]=0;continue;}
        if(!since[i]){since[i]=now;continue;}
        if(now-since[i]<2500)continue;
        since[i]=0;
        {unsigned char f[0xa0]={0};nt_peek(lsm+0x370,f,sizeof f);char h[400];lp_hex(h,sizeof h,f,sizeof f);
         if(st_hlines<60){st_hlines++;lp_log("sync release (fallback): member %llx still yields to %u, +370: %s",(unsigned long long)lsm,yield,h);}}
        *(volatile unsigned char *)(uintptr_t)(lsm+0x39c)=0xff;
        uint64_t fake[8];memset(fake,0,sizeof fake);
        nt_peek(lm+0x2d0,(unsigned char *)fake+0x18,12);fake[5]=lsm;
        ((void (*)(uint64_t))st_changed_orig)((uint64_t)(uintptr_t)fake);
        unsigned char after=9;nt_peek(lsm+0x394,&after,1);
        st_releases++;st_watch_until=now+4000;
        if(st_hlines<60){st_hlines++;lp_log("sync: fallback release of member %llx, EP136 master flag %u -> %u",(unsigned long long)lsm,master,after);}
    }
}
/* MasterPost (vt 0x2394468) = EP136's network tempo-master record: +0x40 variant tag
 * (0 none, 1 local deck {index}, 2 link member {8 bytes = LinkedSyncMember +0x2d4}),
 * post 0xa1e270(this, id*, flag) sets it (flag 0 = no-op) and notifies its listeners.
 * EP136 never posts on the OPUS (state stays 0), so a link player's master request
 * (0xa0a060 -> exchanger slot 4 keyed by MasterPost's master) finds nobody. Option
 * mpsync: the mod posts the real master (our deck or the link player) itself. */
static uint64_t st_mp_find(void) {
    for(unsigned i=0;i<st_nlinked&&i<8;i++){uint64_t o=0,vt=0;nt_peek(st_linked[i]+0x320,(unsigned char *)&o,8);nt_peek(o,(unsigned char *)&vt,8);
        if(vt==0x2394468)return o;}
    return st_masterpost;
}
static uint64_t st_linked_for(unsigned player) {
    for(unsigned i=0;i<st_nlinked&&i<8;i++){unsigned char n=0;if(nt_peek(st_linked[i]+0x2f9,&n,1)&&n==player)return st_linked[i];}
    return 0;
}
static unsigned st_posts;
/* deck 1-2 = our deck, 10+n = link player n; returns 1 if posted */
static int st_mp_set(unsigned who) {
    uint64_t mp=st_mp_find();if(!mp)return 0;
    uint32_t id[4]={0,0,0,0};
    if(who>=1&&who<=2){id[0]=1;id[1]=who-1;}
    else if(who>10){uint64_t lm=st_linked_for(who-10);if(!lm)return 0;id[0]=2;nt_peek(lm+0x2d4,(unsigned char *)&id[1],8);}
    else return 0;
    uint32_t cur[3]={9,9,9};nt_peek(mp+0x40,(unsigned char *)cur,12);
    if(cur[0]==id[0]&&cur[1]==id[1]&&(id[0]!=2||cur[2]==id[2]))return 0;
    /* 1.97: hardware 2026-11-16: MASTER on deck 4 -> EP136 posts deck index 3, the mod
     * (sides only: 1/2) posted index 1 back every tick and the two fought (MASTER and the
     * master bars flashing). A deck on the same side (1/3 left, 2/4 right) is EP136's own
     * choice: leave it. */
    if(id[0]==1&&cur[0]==1&&cur[1]<4&&(cur[1]&1)==id[1])return 0;
    ((void (*)(uint64_t,uint32_t *,int))0xa1e270)(mp,id,1);
    uint32_t now[3]={0};nt_peek(mp+0x40,(unsigned char *)now,12);
    if(st_posts<60){st_posts++;lp_log("sync: posted master %s %u to MasterPost %llx: was %u/%x/%x now %u/%x/%x",who>10?"player":"deck",who>10?who-10:who,(unsigned long long)mp,cur[0],cur[1],cur[2],now[0],now[1],now[2]);}
    return 1;
}
/* tick: hook the exchanger once, and log every linked member's state when it changes */
static void st_tick(const struct oqmod_api *api) {
    static uint64_t last_poll;uint64_t now=lk_now_ms();if(now-last_poll<200)return;last_poll=now;
    /* deck 3/4 alias for EP136's sync (sx_w0): player 3 -> {2,4,0}, player 4 -> {2,4,1} */
    {unsigned on=api->setting_get("deckalias",1)?1u:0u;
     for(unsigned idx=0;idx<2;idx++){uint64_t lm=st_linked_for(3+idx);uint32_t a[4]={0,0,0,0};
        if(lm){uint32_t t=0;nt_peek(lm+0x2d0,(unsigned char *)&t,4);if(t==2){a[0]=2;nt_peek(lm+0x2d4,(unsigned char *)&a[1],8);}}
        if(memcmp(sx_alias[idx],a,16)){sx_alias[idx][1]=a[1];sx_alias[idx][2]=a[2];sx_alias[idx][3]=0;__atomic_store_n(&sx_alias[idx][0],a[0],__ATOMIC_RELEASE);
            lp_log("sync: EP136 external deck %u = %s (id %u/%x/%x)",3+idx,a[0]?"link player":"none",a[0],a[1],a[2]);}}
     for(unsigned i=0;i<st_nlsm;i++){uint32_t id[2]={0,0};unsigned char n=0;
        if(nt_peek(st_lsm[i]+0x374,(unsigned char *)id,8)&&id[0]==1&&id[1]<2&&nt_peek(st_lsm[i]+0x3a9,&n,1)&&n!=sx_deck_no[id[1]]){
            sx_deck_no[id[1]]=n;lp_log("sync: EP136 deck %u uses number %u for hand-overs",id[1]+1,n);}}
     if(on!=__atomic_load_n(&sx_alias_on,__ATOMIC_RELAXED)){__atomic_store_n(&sx_alias_on,on,__ATOMIC_RELEASE);lp_log("sync: deck 3/4 alias %s",on?"on":"off");}
     static uint64_t last_count;if(now-last_count>10000){last_count=now;static unsigned lm_,lf_;unsigned m=sx_mapped,f=sx_fixed;
        if(m!=lm_||f!=lf_){lm_=m;lf_=f;lp_log("sync: alias lookups %u, yield fixes %u",m,f);}}}
    {static unsigned char last[4][0x80];static unsigned lines;
     for(unsigned i=0;i<st_nlsm;i++){unsigned char f[0x80];if(!nt_peek(st_lsm[i]+0x370,f,sizeof f))continue;
        if(memcmp(last[i],f,0x38)){memcpy(last[i],f,sizeof f);if(!LP_QUIET&&lines<120){lines++;char h[300];lp_hex(h,sizeof h,f,0x38);lp_log("sync local %llx state +370: %s",(unsigned long long)st_lsm[i],h);}}}
     static uint32_t mp_last[3]={9,9,9};uint64_t mp=st_mp_find();uint32_t c[3]={0};
     if(mp&&nt_peek(mp+0x40,(unsigned char *)c,12)&&memcmp(c,mp_last,12)){memcpy(mp_last,c,12);if(lines<120){lines++;lp_log("sync MasterPost %llx now %u/%x/%x",(unsigned long long)mp,c[0],c[1],c[2]);}}}
    for(unsigned i=0;i<st_nlinked&&i<8;i++) {
        static unsigned char last[8][0x58];unsigned char f[0x58];
        if(!nt_peek(st_linked[i]+0x2d0,f,sizeof f))continue;
        if(memcmp(last[i],f,0x30)){memcpy(last[i],f,sizeof f);st_linked_log("state",st_linked[i]);}
        if(!sx_done) {
            uint64_t ex=0,vt=0;nt_peek(st_linked[i]+0x318,(unsigned char *)&ex,8);nt_peek(ex,(unsigned char *)&vt,8);
            if(ex&&vt>0x400000&&vt<0x3600000) {
                sx_done=1;unsigned ok=0;
                for(unsigned k=0;k<SX_SLOTS;k++){uintptr_t slot=vt+8*(k+2);if(api->patch(slot,*(const uintptr_t *)slot,sx_wrappers[k],&sx_orig[k])==1)ok++;}
                lp_log("sync exchanger %llx vt %llx: %u slots wrapped",(unsigned long long)ex,(unsigned long long)vt,ok);
            }
        }
    }
}
static void st_install(const struct oqmod_api *api) {
    if(st_installed||!api->ep136_ok||!api->ep136_ok())return;
    st_installed=1;unsigned ok=0;
    for(unsigned k=0;k<ST_SLOTS;k++)if(api->patch(st_slots[k].slot,st_slots[k].fn,st_wrappers[k],&st_orig[k])==1)ok++;
    int a=api->hook&&api->hook(0xa14460,0xa9be7bfdu,(void *)st_take_hook,&st_take_orig)==1;
    int b=api->hook&&api->hook(0xa15740,0xa9be7bfdu,(void *)st_abandon_hook,&st_abandon_orig)==1;
    int c=api->hook&&api->hook(0xa15328,0xa9b07bfdu,(void *)st_inner_hook,&st_inner_orig)==1;
    int e=api->hook&&api->hook(0xa0af40,0xa9b47bfdu,(void *)st_lctor_hook,&st_lctor_orig)==1;
    int f=api->hook&&api->hook(0xa08138,0xa9b37bfdu,(void *)st_lreq_hook,&st_lreq_orig)==1;
    int g=api->hook&&api->hook(0xa11368,0xa9b97bfdu,(void *)st_reqtake_hook,&st_reqtake_orig)==1;
    int h=api->hook&&api->hook(0xa15298,0xa9bb7bfdu,(void *)st_changed_hook,&st_changed_orig)==1;
    int w1=api->hook(0xa10e68,0xa9b47bfdu,(void *)w_other_hook,&w_other_orig)==1;
    int w2=api->hook(0xa12fd0,0xa9ba7bfdu,(void *)w_take_hook,&w_take_orig)==1;
    int w3=api->hook(0xa13168,0xa9b57bfdu,(void *)w_slot27_hook,&w_slot27_orig)==1;
    int w4=api->hook(0xa158e0,0xa9b67bfdu,(void *)w_task_hook,&w_task_orig)==1;
    int w5=api->hook(0xa1e270,0xa9b37bfdu,(void *)st_post_hook,&st_post_orig)==1;
    lp_log("sync hand-over hooks: request-take %d changed-other %d watch %d%d%d%d post %d",g,h,w1,w2,w3,w4,w5);
    lp_log("sync trace: %u of %u slots, take %d abandon %d inner %d, linked ctor %d request %d",ok,ST_SLOTS,a,b,c,e,f);
}
#endif

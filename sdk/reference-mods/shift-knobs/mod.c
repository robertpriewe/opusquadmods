/* SHIFT Search & Zoom (folder shift-knobs): SHIFT + hold PREVIOUS / NEXT TRACK searches
 * through the track, SHIFT + BROWSE or BEAT LOOP zooms the waveforms.
 *
 * 1.0 (2026-11-16) searched with SHIFT + BEAT LOOP (worked on hardware); the user found that
 * confusing: 1.1 uses the TRACK SEARCH buttons of each side instead, held like the search
 * buttons of a CDJ (moves while held, faster the longer it is held). BEAT LOOP is untouched.
 * 1.2 (user, after 1.1 worked): SHIFT + BEAT LOOP search back as an option (off by default):
 *    encoder_bind identities 0x6b0ebd3ccc927600 (left) / ...601 (right).
 * 1.3 (user): two options only. SHIFT + hold TRACK SEARCH searches; SHIFT + BROWSE or either
 *    BEAT LOOP knob zooms the waveforms (the BEAT LOOP search of 1.2 is gone). BEAT LOOP zoom
 *    calls the zoom from the encoder_bind callback, as TYTO 2.7 did (hardware OK 2026-11-15).
 *
 * EP136 1.32 pieces:
 *  - PREVIOUS / NEXT TRACK buttons: deck_buttons ids 0x22b9b592e46a7e00 / 0xcdacccaccdd3fe00
 *    + side (side-browser browser_input.h, from stock factories and captures).
 *  - browse knob: input_devices encoder vtable 0x29bbbc0 slot 4, identity self+0xd0
 *    0x01d690928446227a, source BrowseRotaryEncoderAccelerator (vt 0x29a1fc8); a swallowed
 *    turn is acknowledged at self+0xd8 / +0xdc (TYTO 2.7 zoom, hardware OK 2026-11-15).
 *  - zoom: DispStateHolder slot 4 = 0x1843ea8(0,0,+-1), the firmware's own 6-level zoom.
 *  - deck move: PlayPositionControlFacade per deck (vt 0x24b6178/0x24b61c8 in the object
 *    map at 0x34e94c8, keys ending in the deck index 0..3), moveBy 0x11200f0(facade, ms, 0, 0)
 *    (Pro DJ Link 1.75+; SHIFT Knobs 1.0 on hardware).
 *  - selected deck per side: deck_slip_read().selected[side] (USB Deck Colors 1.2). */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include "oqmod.h"
#include "oqmod_firmware.h"

static const struct oqmod_api *api;
static volatile int want;
static int unsupported;

static int peek(uint64_t a,void *o,size_t n){
    struct iovec l={o,n},r={(void *)(uintptr_t)a,n};
    return a>0x400000&&a<0x1000000000000ull&&syscall(SYS_process_vm_readv,getpid(),&l,1,&r,1,0)==(long)n;
}
static int shift_bits_held(uint32_t s){return s&&!(s&0x80000000u);}
static int knob_delta(int raw){unsigned d=(unsigned)raw&65535u;return d<32768u?(int)d:(int)d-65536;}

/* ---------- SHIFT + BROWSE: waveform zoom ---------- */
struct sz_input {int delta;unsigned char push;};
static void (*sz_original)(void *,const void *,const struct sz_input *);
static volatile int zoom_on=1,search_on=1;
static int sz_hooked,zoom_ok;
static volatile unsigned zoom_in,zoom_out;
static int shift_held(void){
    if(!api->fx_target_read)return 0;
    struct oqmod_fx_target t;memset(&t,0,sizeof t);t.size=sizeof t;
    return api->fx_target_read(&t)&&shift_bits_held(t.shift);
}
static int zoom_code_ok(void){
    return ((const uintptr_t *)0x27c24f0)[4]==0x1843ea8ul&&*(const uint32_t *)0x1843ea8==0x7100005fu&&
        *(const uint32_t *)0x1843eb0==0x9000e321u&&*(const uint32_t *)0x1844168==0xf000e300u;
}
static void sz_input(void *self,const void *source,const struct sz_input *value){
    if(self&&value&&value->push<=1&&*(const uintptr_t *)self==0x29bbbc0ul&&source&&*(const uintptr_t *)source==0x29a1fc8ul&&
       *(const uint64_t *)((const char *)self+0xd0)==0x01d690928446227aull&&
       __atomic_load_n(&zoom_on,__ATOMIC_ACQUIRE)&&__atomic_load_n(&want,__ATOMIC_ACQUIRE)&&
       !*(const unsigned char *)((const char *)self+0xdc)&&!value->push&&shift_held()){
        int move=knob_delta(value->delta);
        if(move&&move>=-64&&move<=64){
            ((int (*)(void *,void *,int))0x1843ea8)(0,0,move>0?1:-1);
            __atomic_fetch_add(move>0?&zoom_in:&zoom_out,1,__ATOMIC_RELAXED);
        }
        __atomic_fetch_add((unsigned *)((char *)self+0xd8),(unsigned)value->delta,__ATOMIC_RELEASE);
        __atomic_store_n((unsigned char *)((char *)self+0xdc),value->push,__ATOMIC_RELEASE);
        return;
    }
    sz_original(self,source,value);
}

/* ---------- SHIFT + hold PREVIOUS / NEXT TRACK: track search ---------- */
/* Button edges arrive on the deck software's input thread (no API calls there), so SHIFT
 * is read in tick and cached. A press that starts with SHIFT held belongs to the search
 * until its release (both edges kept from the deck software); any other press is left
 * alone, so PREVIOUS / NEXT TRACK work as normal without SHIFT. */
static volatile int shift_cache;
static volatile int held_dir[2];            /* per side: +1 searching forward, -1 back, 0 none */
static volatile unsigned presses[2],last_ids_n;
static uint64_t last_ids[4];
static int button_hooked;
static volatile int owned[2][2];             /* [side][0 previous, 1 next]: press kept by the mod */
static int button_event(unsigned side,uint64_t id,int down){
    if(down){unsigned i=__atomic_fetch_add(&last_ids_n,1,__ATOMIC_RELAXED)%4;last_ids[i]=id;}
    if(side>1)return 0;
    int dir=0;
    if(id==0x22b9b592e46a7e00ull+side)dir=-1;
    else if(id==0xcdacccaccdd3fe00ull+side)dir=1;
    if(!dir)return 0;
    volatile int *own=&owned[side][dir>0];
    if(down){
        int mine=__atomic_load_n(&search_on,__ATOMIC_ACQUIRE)&&__atomic_load_n(&want,__ATOMIC_ACQUIRE)&&
                 __atomic_load_n(&shift_cache,__ATOMIC_ACQUIRE);
        __atomic_store_n(own,mine,__ATOMIC_RELEASE);
        if(!mine)return 0;
        __atomic_store_n(&held_dir[side],dir,__ATOMIC_RELEASE);
        __atomic_fetch_add(&presses[side],1,__ATOMIC_RELAXED);
        return 1;
    }
    /* release: kept from the deck software only if its press was (both edges alike) */
    if(!__atomic_exchange_n(own,0,__ATOMIC_ACQ_REL))return 0;
    int other=__atomic_load_n(&owned[side][dir<0],__ATOMIC_ACQUIRE);
    __atomic_store_n(&held_dir[side],other?-dir:0,__ATOMIC_RELEASE);
    return 1;
}

/* ---------- SHIFT + BEAT LOOP: waveform zoom (same zoom as SHIFT + BROWSE) ---------- */
static volatile unsigned loop_in,loop_out;
static int loop_bound;
static int loop_event(const struct oqmod_encoder_event *e){
    if(!e||(e->identity!=0x6b0ebd3ccc927600ull&&e->identity!=0x6b0ebd3ccc927601ull)||e->push||!e->raw||
       !shift_bits_held(e->shift)||!zoom_ok||!__atomic_load_n(&zoom_on,__ATOMIC_ACQUIRE)||!__atomic_load_n(&want,__ATOMIC_ACQUIRE))return 0;
    int move=knob_delta(e->raw);
    if(!move||move< -64||move>64)return 1;
    ((int (*)(void *,void *,int))0x1843ea8)(0,0,move>0?1:-1);
    __atomic_fetch_add(move>0?&loop_in:&loop_out,1,__ATOMIC_RELAXED);
    return 1;
}

/* PlayPositionControlFacade per deck */
static uint64_t facade[4];static int facades_found;
static void find_facades(void){
    unsigned char ready=0;if(!peek(0x34e94c8,&ready,1)||!(ready&1))return;
    uint64_t buckets=0;int32_t count=0;
    if(!peek(0x34e94d8,&buckets,8)||!peek(0x34e94e8,&count,4)||!buckets||count<=0||count>4096)return;
    uint64_t key[16],obj[16];unsigned n=0;
    for(int b=0;b<count;b++){
        uint64_t node=0;peek(buckets+8u*(unsigned)b,&node,8);
        for(unsigned hop=0;node&&hop<64;hop++){
            uint64_t f[3]={0},vt[2]={0};if(!peek(node,f,24))break;
            if(f[1]&&peek(f[1],vt,16)&&vt[0]==0x24b6178&&vt[1]==0x24b61c8&&n<16){key[n]=f[0];obj[n]=f[1];n++;}
            node=f[2];
        }
    }
    unsigned found=0;uint64_t pick[4]={0};
    for(unsigned i=0;i<n;i++){unsigned k=(unsigned)(key[i]&255);if(k<4){unsigned same=0;for(unsigned j=0;j<n;j++)if((key[j]>>8)==(key[i]>>8)&&(key[j]&255)<4)same++;if(same==4&&!pick[k]){pick[k]=obj[i];found++;}}}
    if(found!=4&&n==4){for(unsigned i=0;i<4;i++){unsigned r=0;for(unsigned j=0;j<4;j++)if(key[j]<key[i])r++;pick[r]=obj[i];}found=4;}
    if(found==4){memcpy(facade,pick,sizeof pick);facades_found=1;}
}
static unsigned moves,clamped,skipped;
static int move_deck(unsigned deck,int ms){
    if(!facades_found)find_facades();
    if(!facades_found||deck<1||deck>4||!facade[deck-1]||*(const uint32_t *)0x11200f0!=0xa9b07bfdu)return 0;
    struct oqmod_deck d;memset(&d,0,sizeof d);
    if(!api->deck(deck,&d)||!d.loaded||!d.length_ms){skipped++;return 0;}
    /* stay inside the track: not before 0, not closer than 1 s to the end */
    long at=(long)d.elapsed_ms,to=at+ms,end=(long)d.length_ms-1000;
    if(end<0)end=0;
    if(to<0)to=0;
    if(to>end)to=end;
    if(to!=at+ms)clamped++;
    if(to==at)return 0;
    ((void (*)(uint64_t,double,unsigned,unsigned))0x11200f0)(facade[deck-1],(double)(to-at),0,0);
    moves++;return 1;
}
static unsigned side_deck(unsigned side){
    if(api->deck_slip_read){
        struct oqmod_slip_snapshot s;memset(&s,0,sizeof s);s.size=sizeof s;
        if(api->deck_slip_read(&s)){unsigned sel=s.selected[side];if(sel>=1&&sel<=4&&((sel-1)&1)==side)return sel;}
    }
    return side?2:1;
}


/* Search speed: the deck moves this many times faster than normal play while held,
 * doubling after 1 s and again after 2.5 s (like holding search on a CDJ). */
static const int speed_x[3]={4,8,16};
static uint64_t held_since[2],last_move[2];
static void search_tick(uint64_t now){
    int base=api->setting_get("speed",1);if(base<0||base>2)base=1;
    for(unsigned side=0;side<2;side++){
        int dir=__atomic_load_n(&held_dir[side],__ATOMIC_ACQUIRE);
        if(!dir||!want){held_since[side]=0;continue;}
        if(!held_since[side]){held_since[side]=now;last_move[side]=now;
            move_deck(side_deck(side),dir*200);continue;}            /* a short tap still moves a bit */
        uint64_t dt=now-last_move[side];if(dt<40)continue;if(dt>200)dt=200;
        uint64_t held=now-held_since[side];
        int x=speed_x[base];if(held>1000)x*=2;if(held>2500)x*=2;
        /* forward: the deck already plays 1x itself */
        long ms=(long)dt*(dir>0?x-1:x);
        move_deck(side_deck(side),dir*(int)ms);last_move[side]=now;
    }
}

static void tick(void){
    if(unsupported)return;
    __atomic_store_n(&shift_cache,shift_held(),__ATOMIC_RELEASE);
    __atomic_store_n(&zoom_on,api->setting_get("zoom",1)?1:0,__ATOMIC_RELEASE);
    __atomic_store_n(&search_on,api->setting_get("search",1)?1:0,__ATOMIC_RELEASE);
    uint64_t now=api->now_ms();
    search_tick(now);
    static uint64_t last;
    if(now-last>=1000){last=now;
        char t[480];
        snprintf(t,sizeof t,"Track search: buttons %s, decks %s, presses left %u right %u, %u moves (%u at the track's start or end)\n"
                 "last buttons %016llx %016llx %016llx %016llx\nZoom: browse knob %s, %u in, %u out; BEAT LOOP knobs %s, %u in, %u out",
                 button_hooked?"ok":"not available",facades_found?"ok":"not found yet",presses[0],presses[1],moves,clamped,
                 (unsigned long long)last_ids[0],(unsigned long long)last_ids[1],(unsigned long long)last_ids[2],(unsigned long long)last_ids[3],
                 !zoom_ok?"not available":sz_hooked?"ok":"not hooked",zoom_in,zoom_out,
                 !zoom_ok?"not available":loop_bound?"ok":"not bound",loop_in,loop_out);
        static char prev[480];if(strcmp(prev,t)){strcpy(prev,t);api->set_status(t);}
    }
}
static void enabled(int on){
    __atomic_store_n(&want,on?1:0,__ATOMIC_RELEASE);
    if(!on){held_since[0]=held_since[1]=0;}
}

int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->version<OQMOD_API_VERSION)return 1;
    if(!OQMOD_HAS_CALL(a,deck_slip_read))return oqmod_needs_firmware(a,info,"SHIFT Search & Zoom","1.3","R124");
    api=a;
    if(!api->ep136_ok()||!api->deck_buttons||!api->deck||!api->now_ms||!api->fx_target_read)unsupported=1;
    static const char *const speeds[]={"Slow","Normal","Fast"};
    api->option("search","SHIFT + hold TRACK SEARCH searches",1);
    api->choice("speed","Search speed",speeds,3,1);
    api->option("zoom","SHIFT + BROWSE or BEAT LOOP zooms",1);
    if(!unsupported){
        button_hooked=api->deck_buttons(button_event)!=0;
        zoom_ok=zoom_code_ok();
        if(zoom_ok&&api->encoder_bind)loop_bound=api->encoder_bind(0x6b0ebd3ccc927600ull,loop_event)&&api->encoder_bind(0x6b0ebd3ccc927601ull,loop_event);
        if(zoom_ok&&api->patch){
            uintptr_t cur=((const uintptr_t *)0x29bbbc0)[4];
            if(cur&&api->patch(0x29bbbc0+4*8,cur,(void *)sz_input,(void **)&sz_original)==1)sz_hooked=1;
        }
    }
    info->api_version=OQMOD_API_VERSION;
    info->name="SHIFT Search & Zoom";
    info->description="Hold SHIFT + a TRACK SEARCH button to search through the\ntrack. SHIFT + BROWSE or BEAT LOOP zooms the waveforms.";
    info->version="1.3";
    info->tick=tick;
    info->enabled=enabled;
    if(unsupported)api->set_status("Needs the OPUS 1.32 deck software.");
    return 0;
}

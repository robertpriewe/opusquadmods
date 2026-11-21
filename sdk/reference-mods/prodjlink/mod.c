/* Pro DJ Link (Experimental), as an add-on mod: MODS/prodjlink/mod.so.
 * Switched on, the OPUS joins the DJ Link network on its LAN port as a free player number (1-4) and
 * follows the other players (prodjlink_link.h); their BPM, pitch, beat and play state
 * show under the mod's switch on Mod Menu > Add-on Mods. Nothing is sent to the mixer.
 * Option "offer": the link probe (prodjlink_probe.h) announces a USB and logs what other
 * players then ask for. Logs: MODS/prodjlink/logs (1.99). */
#include <stdio.h>
#include <stddef.h>
#include "prodjlink_link.h"
#include "oqmod.h"
#include "prodjlink_dbfix.h"
#include <sys/uio.h>
#include "prodjlink_nfstrace.h"
#include "prodjlink_linkfilter.h"
#include "prodjlink_fsfix.h"
#include "prodjlink_names.h"
#include "prodjlink_icons.h"
#include "prodjlink_wavetrace.h"   /* 1.71: research */
#include "prodjlink_devset.h"   /* 1.72: research */
#include "prodjlink_loadtrace.h"   /* 1.77: research */
#include "prodjlink_looptrace.h"   /* 1.82: research */
#include "prodjlink_devlib.h"   /* 1.60: "Pro DJ Link" library row on SOURCE */
#include "prodjlink_synctrace.h"
#include "prodjlink_inputs.h"   /* 1.55: PRO DJ LINK per channel */
#include "prodjlink_inputmenu.h"   /* 1.56: ... chosen in the native input menu */
#include "prodjlink_xdeck.h"
#include "prodjlink_audioprobe.h"   /* 1.93: research, deck audio silent */
#include "prodjlink_watch.h"   /* 1.95: freeze watch */
/* 1.99: logs per session in MODS/prodjlink/logs (session / watch / crash-NNN.txt), crash
 * handler with registers, backtrace and the last log lines (prodjlink_crashlog.h), settings
 * and status in the log.
 * 2.0: release build. Only the four user options are registered; the internal switches keep
 * their tested defaults (read with setting_get). No menu action. */
#include "oqmod.h"

static const struct oqmod_api *api;
static unsigned ticks;

/* R123: deck buttons (logged, to find MASTER), deck 3/4 lock, MASTER light watch */
#define BT_RING 64
static struct {uint64_t id;unsigned side,down;} bt_ring[BT_RING];
static _Atomic unsigned bt_head;static unsigned bt_tail,bt_logged;
static _Atomic unsigned bt_master_press;   /* deck whose MASTER was pressed (1-2), 0 none */
static int bt_handler(unsigned side,uint64_t id,int down) {
    if(down&&(id>>8)==0x63bbf6fc9b5a20ull&&side<2)__atomic_store_n(&bt_master_press,side+1,__ATOMIC_RELEASE);
    unsigned h=__atomic_fetch_add(&bt_head,1,__ATOMIC_ACQ_REL)%BT_RING;
    bt_ring[h].id=id;bt_ring[h].side=side;bt_ring[h].down=(unsigned)down;
    return 0;   /* never swallowed in this build */
}
static int r123(void){return api->size>=offsetof(struct oqmod_api,led_state)+sizeof(void *);}
static unsigned ep_master;   /* deck whose MASTER light EP136 shows (1-2), 0 none */
static void r123_tick(void) {
    if(!r123())return;
    static int registered;if(!registered)registered=api->deck_buttons(bt_handler);
    unsigned head=__atomic_load_n(&bt_head,__ATOMIC_ACQUIRE);
    while(bt_tail!=head){unsigned i=bt_tail++%BT_RING;
        if(bt_logged<300&&bt_ring[i].down){bt_logged++;lp_log("button side %u id %016llx down",bt_ring[i].side,(unsigned long long)bt_ring[i].id);}}
    api->deck_select_lock(__atomic_load_n(&ch_link_mask,__ATOMIC_ACQUIRE));   /* 1.55: only PRO DJ LINK decks */
    /* another player took master from us: EP136's own deck gives it up natively */
    lb_abandon_pending=0;   /* (1.38 fake abandon task retired: EP136 releases natively below) */
    if(api->setting_get("ep136abandon",1))st_release_tick();
    /* MASTER pressed on deck 1/2 (id 63bbf6fc9b5a20 + side, found 2026-11-11) */
    unsigned pressed=__atomic_exchange_n(&bt_master_press,0,__ATOMIC_ACQ_REL);
    if(pressed){lp_log("MASTER pressed on deck %u (master now %u)",pressed,lb_master);if(lb_master!=pressed)lb_request(pressed);}
    /* Another player is master: EP136 still lights MASTER on its own deck (its sync
     * doesn't know), so both MASTER lights are held off until our deck is master again. */
    /* (1.35's cosmetic MASTER-light override removed: EP136 itself must give master up) */
    /* EP136's MASTER light, logged only (2-bit value: lit = 2 or more) */
    unsigned now_master=0;uint32_t v[2]={0,0};
    for(unsigned side=0;side<2;side++)if(api->led_state(side,OQMOD_LED_MASTER,&v[side])&&v[side]>=2)now_master=side+1;
    static uint32_t last_v[2]={9,9};
    if(now_master!=ep_master||v[0]!=last_v[0]||v[1]!=last_v[1]) {
        lp_log("master light: deck %u (values %u/%u)",now_master,v[0],v[1]);
        ep_master=now_master;last_v[0]=v[0];last_v[1]=v[1];
    }
    /* EP136's network master record follows the real master, so EP136's own sync code
     * can hand master to (and take it from) the other players */
    if(api->setting_get("mpsync",1)) {
        /* only where EP136 agrees, so it never fights the post (1.41 flickered) */
        if(lb_master>=1&&lb_master<=2&&ep_master==lb_master)st_mp_set(lb_master);
        /* (an external master is now EP136's own business via the deck 3/4 alias) */
    }
}
/* 1.99: every option's value in the log at start and whenever one changes */
static const char *const set_keys[]={"link3","link4","xwide","xmute","offer","drop0b","yieldnum","decks12","plink",
    "nfsdetect","fixlist","linkdb","linkicon","deckalias","mpsync","ep136abandon","xfollow","xmaster","xplay",
    "wave3band","wavetrace","linkcolour","devsettrace","loadtrace","looptrace","cueshift","cuefix"};
static void settings_log_tick(void) {
    static unsigned n;if (n++ % 60) return;        /* every ~2 s */
    char line[600];size_t k=0;static char last[600];
    for (unsigned i=0;i<sizeof set_keys/sizeof set_keys[0]&&k<sizeof line-40;i++)
        k+=(size_t)snprintf(line+k,sizeof line-k," %s=%d",set_keys[i],api->setting_get(set_keys[i],-1));
    if (strcmp(line,last)) { memcpy(last,line,sizeof last); lp_log("settings:%s",line); }
}
static void tick(void) {
    wt_tick(); wt_start();                         /* 1.95 */
    if (!cl_installed) { cl_maps_refresh(); cl_install(); lp_log("Pro DJ Link " PDL_VERSION ": crash log ready"); }   /* 1.99 */
    settings_log_tick();                           /* 1.99 */
    CL_STEP("tick: settings");
    unsigned offer = api->setting_get("offer", 0) ? 1u : 0u;
    __atomic_store_n(&lp_offer, offer, __ATOMIC_RELEASE);
    if (offer && !db_installed) db_install(api);   /* once; stays until the app restarts */
    if (!nt_installed) nt_install(api);            /* research: NfsManager entry points */
    lf_drop0b = api->setting_get("drop0b", 1) ? 1u : 0u;
    lf_yield = api->setting_get("yieldnum", 1) ? 1u : 0u;
    lf_two = lk_two = api->setting_get("decks12", 1) ? 1u : 0u;
    lf_upgrade = api->setting_get("plink", 0) ? 1u : 0u;
    nt_detect_tick(api->setting_get("nfsdetect", 1));
    fs_fixlist = api->setting_get("fixlist", 1) ? 1u : 0u;
    fs_linkdb = api->setting_get("linkdb", 0) ? 1u : 0u;   /* off: EP136 browses its own USB via 127.0.0.1 */
    if (!fs_installed && api->ep136_ok && api->ep136_ok()) fs_install();
    if (!nm_installed) nm_install(api);
    li_use = api->setting_get("linkicon", 1) ? 1u : 0u;
    if (!li_installed) li_install(api);
    dl_use = li_use;
    w3_on = api->setting_get("wave3band", 1) ? 1u : 0u;   /* 1.64 */
    if (!dl_installed) dl_install(api);
    if (!wt_installed && api->setting_get("wavetrace", 0)) wt_install(api);
    ds_colour_follow = api->setting_get("linkcolour", 1) ? 1u : 0u;   /* 1.73 */
    if (!ds_installed && api->setting_get("devsettrace", 1)) ds_install(api);
    if (!lt_installed && api->setting_get("loadtrace", 0)) lt_install(api);
    if (!lo_installed && api->setting_get("looptrace", 1)) lo_install(api);
    if (!ls_installed && api->setting_get("looptrace", 1)) ls_install(api);
    if (!lt3_installed && api->setting_get("looptrace", 1)) lt3_install(api);   /* 1.85 */
    w3_cueshift = api->setting_get("cueshift", 1) ? 1u : 0u;   /* 1.83 */
    w3_cuefix = api->setting_get("cuefix", 1) ? 1u : 0u;   /* 1.82 */
    CL_STEP("tick: sync");
    if (!st_installed) st_install(api);
    st_tick(api);
    if (!lf_installed) lf_install(api);            /* research: EP136's own link packets */
    CL_STEP("tick: master/buttons");
    r123_tick();
    CL_STEP("tick: input menus");
    mi_tick(api);                                  /* 1.56: PRO DJ LINK entry in the input menus */
    CL_STEP("tick: channels");
    ch_tick(api);                                  /* 1.55: channels 3/4 set to PRO DJ LINK */
    CL_STEP("tick: audio probe");
    au_tick(api);                                  /* 1.93: audio probe */
    CL_STEP("tick: decks 3/4");
    xd_tick(api);                                  /* players 3/4 on decks 3/4 */
    if (ticks++ % 15) return;                      /* about twice a second */
    CL_STEP("tick: status");
    char text[512];
    lk_summary(text, sizeof text);
    api->set_status(text);
    cl_set_status(text);                           /* 1.99: for crash reports */
    {static unsigned n;static char last[512];      /* and into the log every 10 s when it changed */
     if (n++ % 20 == 0 && strcmp(last, text)) { snprintf(last, sizeof last, "%s", text); lp_log("status: %s", text); }}
    CL_STEP("idle");
}
static void enabled(int on) {
    if (!on) __atomic_store_n(&wt_tick_ms, 0, __ATOMIC_RELEASE);   /* 1.95: no stall reports while off */
    lk_set_enabled(on);
    ticks = 0;
    api->set_status(on ? "Joining the link ..." : "");
    api->log(on ? "joining the link (player 1-4, whichever is free)" : "left the link");
}
int oqmod_init(const struct oqmod_api *a, struct oqmod_info *info) {
    if (!a || a->version < OQMOD_API_VERSION || a->size < sizeof *a) return 1;
    api = a;
    lb_api = a;
    /* The four user options. Everything else keeps its tested default (settings.txt may override). */
    api->option("link3", "Channel 3 = PRO DJ LINK", 0);
    api->option("link4", "Channel 4 = PRO DJ LINK", 0);
    api->option("xwide", "PRO DJ LINK decks use the WIDE tempo range", 1);
    api->option("xmute", "Mute PRO DJ LINK decks' own audio", 1);
    mi_init(api);
    info->api_version = OQMOD_API_VERSION;
    info->name = "Pro DJ Link (Experimental)";
    info->description = "EXPERIMENTAL. Joins the DJ Link network on the LAN port\nas a free player number (1-4).";
    info->version = PDL_VERSION;
    info->tick = tick; info->enabled = enabled;
    return 0;
}

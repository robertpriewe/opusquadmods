/* SPDX-License-Identifier: MIT */
#include "oqmod.h"
#include <stddef.h>
static const struct oqmod_api *api;
static unsigned bank;static int on;
static const unsigned ids[4][8]={{0,1,2,3,4,5,6,7},{8,9,10,11,12,26,25,16},
    {13,14,15,18,19,20,22,23},{24,17,21,27,28,29,30,31}};
static const char *const banks[]={"Jog / Cue A-D","Cue E-H / Ring","Transport / Sync","Loop In"};
static const char *const sides[]={"Left","Right","Both"};
static const char *const colors[]={"White","Red","Green","Blue","Orange","Purple","Cyan","Yellow"};
static const uint32_t rgb[]={0xffffff,0xff0000,0x00ff00,0x0000ff,0xff8000,0xb000ff,0x00ffff,0xffff00};
static const char *const labels[4][8]={{"C00 Jog center","C01 Deck 1/2","C02 Deck 3/4","C03 Inner circle","C04 Hot Cue A","C05 Hot Cue B","C06 Hot Cue C","C07 Hot Cue D"},
    {"C08 Hot Cue E","C09 Hot Cue F","C10 Hot Cue G","C11 Hot Cue H","C12 RGB jog ring","E13 Cue","E12 Play / Pause","E03 Slip Reverse"},
    {"E00 Vinyl","E01 Master Tempo","E02 Master","E05 Key Sync","E06 Reset","E07 Beat Sync","E09 Quantize","E10 Loop Out"},
    {"E11 Loop In", "--", "--", "--", "--", "--", "--", "--"}};
static void pulse(unsigned n){
    bank=(unsigned)api->setting_get("bank",0);
    if(!on||bank>3||n>7||ids[bank][n]>=OQMOD_LED_COUNT)return;
    unsigned side=(unsigned)api->setting_get("side",0),color=(unsigned)api->setting_get("color",0);
    if(side>2)side=0;
    if(color>7)color=0;
    int ok=0;for(unsigned s=0;s<2;s++)if(side==2||s==side)ok|=api->led_set(s,ids[bank][n],rgb[color],2000);
    api->set_status(ok?"2-second LED pulse sent. Native colours return automatically.":"LED request unavailable.");
}
#define PULSE(n) static void p##n(void){pulse(n);}
PULSE(0) PULSE(1) PULSE(2) PULSE(3) PULSE(4) PULSE(5) PULSE(6) PULSE(7)
static void stop(void){api->led_clear();}
static void enabled(int v){on=v;if(!on)stop();}
/* Stable action IDs, with the current bank's measured physical names. */
static void tick(void){
    static unsigned previous=99;unsigned selected=(unsigned)api->setting_get("bank",0);if(selected>3)selected=0;
    bank=selected;if(bank==previous)return;previous=bank;
    for(unsigned i=0;i<8;i++){
        api->menu_action_label(i,labels[bank][i]);
    }
    api->set_status("Pulse duration: 2 seconds. Mono LEDs ignore colour.\nNo audio, GPIO or unmapped controller fields are changed.");
}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->version!=OQMOD_API_VERSION||a->size<offsetof(struct oqmod_api,menu_action_label)+sizeof a->menu_action_label||!a->menu_action||!a->menu_action_label||!a->led_set||!a->led_clear)return 1;
    api=a;a->menu_order(1910);
    a->choice_buttons("side","Player",sides,3,0);a->choice_buttons("color","Pulse colour",colors,8,0);
    a->choice_buttons("bank","LED group",banks,4,0);
    static void (*const callbacks[])(void)={p0,p1,p2,p3,p4,p5,p6,p7};
    static const char *const actions[]={"Test LED 1","Test LED 2","Test LED 3","Test LED 4","Test LED 5","Test LED 6","Test LED 7","Test LED 8"};
    for(unsigned i=0;i<8;i++)if(!a->menu_action(actions[i],callbacks[i]))return 1;
    if(!a->menu_action("Stop / restore native LEDs",stop))return 1;
    *info=(struct oqmod_info){.api_version=OQMOD_API_VERSION,.name="LED Diagnostics",
        .description="Select a player, colour and LED group; then pulse a test LED.\nEach pulse lasts 2 seconds. Switch this mod off to restore native LEDs.",
        .version="1.0",.tick=tick,.enabled=enabled};return 0;
}

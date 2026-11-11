/* SPDX-License-Identifier: MIT */
#include "oqmod.h"
#include <stddef.h>
static const struct oqmod_api *api;
static int on;static uint64_t next;
static void save(void){
    if(api->diagnostic_save())api->set_status("Saving runtime report to USB...");
    else api->set_status("Enable diagnostics and connect a writable USB.");
    next=0;
}
static void enabled(int value){on=value;next=0;}
static void tick(void){
    uint64_t now=api->now_ms();if(!on||now<next)return;next=now+1000;
    char text[1024];if(api->diagnostic_status(text,sizeof text))api->set_status(text);
}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->version!=OQMOD_API_VERSION||a->size<offsetof(struct oqmod_api,diagnostic_status)+sizeof a->diagnostic_status||
       !a->diagnostic_status||!a->diagnostic_save||!a->menu_action)return 1;
    api=a;a->menu_order(1900);a->default_enabled(1);
    if(!a->menu_action("Save runtime diagnostics to USB",save))return 1;
    *info=(struct oqmod_info){.api_version=OQMOD_API_VERSION,.name="Runtime Diagnostics",
        .description="System information and performance readings.\nSave a runtime report to any writable connected USB.",
        .version="1.0",.tick=tick,.enabled=enabled};return 0;
}

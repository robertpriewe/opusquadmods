#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/diagnostics/mod.c"
static unsigned saves,reads;static void (*save_action)(void);
static uint64_t clock_ms(void){return 5000;}
static int add(const char *s,void (*cb)(void)){assert(s);save_action=cb;return 1;}
static int save_request(void){saves++;return 1;}
static int snapshot(char *s,uint32_t n){assert(n==1024);snprintf(s,n,"System info\nSaved report");reads++;return 1;}
static void status(const char *s){assert(s);}
static void order(int v){assert(v==1900);}
static void default_on(int on){assert(on==1);}
int main(void){
    struct oqmod_api a={.version=1,.size=sizeof a,.now_ms=clock_ms,.menu_action=add,
        .diagnostic_save=save_request,.diagnostic_status=snapshot,.set_status=status,.menu_order=order,.default_enabled=default_on};
    struct oqmod_info info={0};assert(!oqmod_init(&a,&info)&&save_action);
    info.enabled(1);info.tick();info.tick();assert(reads==1);
    save_action();info.tick();assert(saves==1&&reads==2);
    info.enabled(0);info.tick();assert(reads==2);
    a.size=offsetof(struct oqmod_api,diagnostic_status);assert(oqmod_init(&a,&info));
    puts("PASS: external runtime diagnostics, asynchronous save, bounded snapshot and old-host guard");
}

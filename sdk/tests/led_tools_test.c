#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/led-diagnostics/mod.c"
static int group,player,colour;static unsigned calls,clears;
static void (*actions[9])(void);static unsigned count;
static int get(const char *k,int fallback){return !strcmp(k,"bank")?group:!strcmp(k,"side")?player:!strcmp(k,"color")?colour:fallback;}
static int action_add(const char *s,void (*cb)(void)){assert(s&&count<9);actions[count++]=cb;return 1;}
static int rename_action(unsigned n,const char *s){assert(n<8&&s);return 1;}
static int led(unsigned side,unsigned id,uint32_t rgb,uint32_t ms){assert(side==1&&id==12&&rgb==0xff0000&&ms==2000);calls++;return 1;}
static void clear(void){clears++;}
static void status(const char *s){assert(s);}
static void order(int v){assert(v==1910);}
static int choices(const char *k,const char *l,const char *const *v,unsigned n,int f){assert(k&&l&&v&&n>=3);return f;}
int main(void){
    struct oqmod_api a={.version=1,.size=sizeof a,.setting_get=get,.menu_action=action_add,
        .menu_action_label=rename_action,.led_set=led,.led_clear=clear,.set_status=status,.menu_order=order,.choice_buttons=choices};
    struct oqmod_info info={0};assert(!oqmod_init(&a,&info)&&count==9);
    group=1;player=1;colour=1;info.enabled(1);info.tick();actions[4]();assert(calls==1);
    info.enabled(0);actions[4]();assert(calls==1&&clears==1);
    info.enabled(1);actions[8]();assert(clears==2);
    group=3;actions[7]();assert(calls==1); /* unmapped controller bytes never probed */
    a.size=offsetof(struct oqmod_api,menu_action_label);assert(oqmod_init(&a,&info));
    puts("PASS: external LED tools, mapped-only 2s pulses, side/RGB, stop/OFF and old-host guard");
}

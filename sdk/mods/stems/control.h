#ifndef OQ_STEMS_CONTROL_H
#define OQ_STEMS_CONTROL_H
/* Stems: the control side of oqstem.so, the OPUS port of AZ-MODS azstem.
 *
 * oqstem and opusmod talk through AZ's own files in /home/root/settings, so the
 * engine runs unchanged:
 *   azstem.stems{,2,3,4}     written here: bit 7 engaged | bits 0-3 VOCAL INST BASS DRUMS
 *   azstem.avail{,2,3,4}     written by oqstem: "1" while the deck's track has stems
 *   azstem.curtrack{,2,3,4}  written by oqstem: the deck's track name
 * As azmod does: a deck is engaged whenever Stems is on and its track has stems,
 * and a new track starts with all four stems on. The touch strip toggles them.
 * Runs on the settings poll thread; files are only written when a value changes. */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef OQS_DIR
#define OQS_DIR "/home/root/settings"
#endif
#define OQS_DECKS 4
#define OQS_ALL 0xfu

static unsigned oqs_on[OQS_DECKS]={OQS_ALL,OQS_ALL,OQS_ALL,OQS_ALL};
static int oqs_avail[OQS_DECKS];
static char oqs_track[OQS_DECKS][256];
static unsigned oqs_written[OQS_DECKS]={~0u,~0u,~0u,~0u};
static unsigned oqs_writes,oqs_failures,oqs_resets;

static void oqs_path(char *out,size_t n,const char *kind,int deck) {
    static const char *const suffix[OQS_DECKS]={"","2","3","4"};
    snprintf(out,n,OQS_DIR "/azstem.%s%s",kind,suffix[deck]);
}
/* First line of a small file, without the newline; "" when absent. */
static void oqs_read(const char *path,char *out,size_t n) {
    out[0]=0;
    FILE *f=fopen(path,"re");
    if(!f)return;
    if(!fgets(out,(int)n,f))out[0]=0;
    fclose(f);
    out[strcspn(out,"\r\n")]=0;
}
/* Whole-file replace (write .new, rename), as azmod's settings_write. */
static int oqs_write(const char *path,unsigned value) {
    char tmp[200];
    if(snprintf(tmp,sizeof tmp,"%s.new",path)>=(int)sizeof tmp)return -1;
    FILE *f=fopen(tmp,"we");
    if(!f)return -1;
    fprintf(f,"%u\n",value);
    if(fclose(f)!=0||rename(tmp,path)!=0) {unlink(tmp);return -1;}
    return 0;
}
/* enabled: the Stems setting. toggles: bit deck*4+stem, from the touch strip. */
static void oqs_poll(int enabled,unsigned toggles) {
    for(int d=0;d<OQS_DECKS;d++) {
        char path[160],line[256];
        oqs_path(path,sizeof path,"curtrack",d);oqs_read(path,line,sizeof line);
        if(strcmp(line,oqs_track[d])) {   /* a new track (or eject): all stems on */
            snprintf(oqs_track[d],sizeof oqs_track[d],"%s",line);
            oqs_on[d]=OQS_ALL;oqs_resets++;
        }
        oqs_path(path,sizeof path,"avail",d);oqs_read(path,line,sizeof line);
        oqs_avail[d]=line[0]=='1';
        oqs_on[d]^=(toggles>>(4*d))&OQS_ALL;
        unsigned value=enabled&&oqs_avail[d]?0x80u|oqs_on[d]:0;
        if(value!=oqs_written[d]) {
            oqs_path(path,sizeof path,"stems",d);
            if(oqs_write(path,value)==0) {oqs_written[d]=value;oqs_writes++;}
            else oqs_failures++;
        }
    }
}
/* oqstem.so is always preloaded (boot wrapper, exact sha). The Stems switch drives its
 * master file azstem.enable live: "0" = no track matched or decoded, every deck plays
 * normally; "1" = on. Returns 1 when the file matches. The R85 preload flag is removed. */
#define OQS_MASTER OQS_DIR "/azstem.enable"
#define OQS_LEGACY_FLAG OQS_DIR "/oqstem.enable"
static int oqs_engine_flag(int want) {
    static int written=-1;
    if(written<0&&access(OQS_LEGACY_FLAG,F_OK)==0)unlink(OQS_LEGACY_FLAG);
    want=!!want;
    if(written==want)return 1;
    char tmp[]=OQS_MASTER ".new";FILE *f=fopen(tmp,"we");
    if(!f)return 0;
    fputs(want?"1\n":"0\n",f);
    if(fclose(f)!=0||rename(tmp,OQS_MASTER)!=0){unlink(tmp);return 0;}
    written=want;return 1;
}
/* oqstem.so is in the process (it exports AZ's azstem_deck_snapshot). */
static int oqs_engine(void) {return stem_engine_published!=0;}
static void oqs_report(FILE *out) {
    fprintf(out,"stems_control engine=%d writes=%u failures=%u resets=%u",oqs_engine(),oqs_writes,oqs_failures,oqs_resets);
    for(int d=0;d<OQS_DECKS;d++)fprintf(out," d%d=%s/%x/0x%x",d+1,oqs_avail[d]?"avail":"none",oqs_on[d],oqs_written[d]);
    fputc('\n',out);
    /* oqstem's own logs: build identity, which hooks went in, deck/track mapping. */
    static const char *const logs[]={"/tmp/azstem-build.log","/tmp/azstem.log","/tmp/azstem.probestat"};
    for(unsigned i=0;i<sizeof logs/sizeof *logs;i++) {
        FILE *f=fopen(logs[i],"re");if(!f)continue;
        if(fseek(f,0,SEEK_END)==0) {long n=ftell(f);fseek(f,n>12000?n-12000:0,SEEK_SET);}
        fprintf(out,"---- %s (tail)\n",logs[i]);
        char line[400];while(fgets(line,sizeof line,f))fputs(line,out);
        fclose(f);
    }
}
#endif

/* USB-owned legacy view state, metadata scheduling and sliding waveform window. */
#include "oqmod_native_feature.h"
#include <string.h>
#include "deck_view_wire.h"
static const char *const values0[]={"Normal", "Wide", "Close", "Tight"};
static const struct oq_native_option oq_feature_options[]={
    {"zoom","Zoom",values0,4,0,0},
};
static const struct oq_native_feature oq_feature={"side-now-playing-legacy","Side Screen Now Playing (Legacy)","Legacy side-screen now-playing view.\nRequires Side Screen Browser. TYTO is the preferred view.", 190};
#define OQ_NATIVE_CUSTOM_ENTRY
#define OQ_NATIVE_FEATURE_IMPLEMENTATION
#include "oqmod_native_feature.h"
static unsigned legacy_on,legacy_epoch,legacy_seen_epoch;
static unsigned legacy_frame[2],legacy_dirty[4],legacy_seen[4];
static uint32_t legacy_generation[4],legacy_revision[4];
static int32_t legacy_sweep[4],legacy_last_center[4];
static uint32_t legacy_position(const struct oqmod_analysis_snapshot *a,uint64_t now,int *playing){
    *playing=now-a->changed_ms<150;
    if(!*playing)return a->anchor_ms;
    uint64_t dt=now-a->anchor_at;if(dt>250)dt=250;
    int64_t rate=10000+a->tempo_x100;if(rate<1000)rate=1000;
    return a->anchor_ms+(uint32_t)((int64_t)dt*rate/10000);
}
static void legacy_state(unsigned deck,const struct oqmod_analysis_snapshot *a,uint64_t now,struct dv_state *s,int *columns_ok){
    *s=(struct dv_state){0};*columns_ok=0;s->color=a->color;
    if(!a->valid||now-a->at_ms>2000)return;
    int playing;uint32_t pos=legacy_position(a,now,&playing),total=a->total_ms;
    s->flags=(uint8_t)((total||a->bpm_x100?DV_FLAG_LOADED:0)|(playing?DV_FLAG_PLAYING:0)|DV_FLAG_POSITION);
    s->position_ms=pos;s->length_ms=total?total:a->length_ms;
    if(a->loop_a&&a->loop_b>a->loop_a){
        uint32_t in=0,out=0,raw=a->elapsed_ms;
        if(a->loop_a<=raw+100&&raw<=a->loop_b+100){in=a->loop_a;out=a->loop_b;}
        else{uint64_t ia=(uint64_t)a->loop_a*1000/150,ib=(uint64_t)a->loop_b*1000/150;
            if(ia<=raw+100&&raw<=ib+100&&ib<1800000){in=(uint32_t)ia;out=(uint32_t)ib;}}
        if(out>in&&out-in<=600000){s->flags|=DV_FLAG_LOOP;s->loop_in_ms=in;s->loop_out_ms=out;}
    }
    s->bpm_x100=a->bpm_x100;s->tempo_x100=a->tempo_x100;s->zoom=(uint8_t)a->zoom;s->theme=(uint8_t)a->theme;
    if(!a->columns||!a->generation)return;
    uint32_t len=a->length_ms;if(total&&(total>len+3000||len>total+3000))return;
    s->generation=a->generation;*columns_ok=1;
    struct oqmod_analysis_beats pair;
    if(a->beats>1&&oq_feature_api->analysis_beats(deck,a->generation,pos>5000?pos-5000:0,&pair)){
        uint32_t interval=pair.time_ms[1]-pair.time_ms[0];
        if(interval>=150&&interval<=2000){s->flags|=DV_FLAG_GRID;s->beat_ms=pair.time_ms[0];s->beat_us=interval*1000u;s->beat=pair.beat[0];}
    }
}
static int legacy_record(unsigned side,uint64_t now,unsigned char p[64]){
    if(side>1||!__atomic_load_n(&legacy_on,__ATOMIC_ACQUIRE))return 0;
    unsigned epoch=__atomic_load_n(&legacy_epoch,__ATOMIC_ACQUIRE);
    if(epoch!=legacy_seen_epoch){
        memset(legacy_frame,0,sizeof legacy_frame);memset(legacy_dirty,0,sizeof legacy_dirty);memset(legacy_seen,0,sizeof legacy_seen);
        memset(legacy_sweep,0,sizeof legacy_sweep);memset(legacy_last_center,0,sizeof legacy_last_center);legacy_seen_epoch=epoch;
    }
    unsigned f=legacy_frame[side]++,deck=side+1+(f&1)*2,k=f>>1,d=deck-1;
    struct oqmod_analysis_snapshot a={.size=sizeof a};if(!oq_feature_api->analysis_snapshot(deck,&a))return 0;
    if(!legacy_seen[d]||legacy_generation[d]!=a.generation||legacy_revision[d]!=a.cue_revision){
        legacy_seen[d]=1;legacy_generation[d]=a.generation;legacy_revision[d]=a.cue_revision;legacy_dirty[d]=7;
    }
    struct dv_state s;int columns_ok;legacy_state(deck,&a,now,&s,&columns_ok);
    if(!(k&1)){dv_encode_state(p,deck,&s);return 1;}
    if(!columns_ok)return 0;
    unsigned slot=(k>>1)%64;
    if(legacy_dirty[d]||slot==16||slot==32||slot==48){
        unsigned dirty=legacy_dirty[d],what=dirty&1?1:dirty&2?2:dirty&4?4:slot==32?1:slot==16?2:4;
        legacy_dirty[d]&=~what;
        if(what==2){dv_header(p,deck,'M',a.generation);for(unsigned i=0;i<DV_MEMORY_CUES;i++)dv_put32(p+20+4*i,a.memory_ms[i]);}
        else if(what==4){dv_header(p,deck,'L',a.generation);for(unsigned i=0;i<DV_LOOP_COUNT;i++){dv_put32(p+20+8*i,i<a.loops?a.loop_ms[i][0]:0);dv_put32(p+24+8*i,i<a.loops?a.loop_ms[i][1]:0);}}
        else{dv_header(p,deck,'C',a.generation);for(unsigned i=0;i<DV_CUES;i++)dv_put32(p+20+4*i,a.cue_ms[i]);memcpy(p+52,a.cue_color,DV_CUES);}
    }else if(slot==8){
        dv_header(p,deck,'N',a.generation);
        if(!a.title_hidden&&a.text_until_ms<=now)for(unsigned i=0;i<DV_ARTIST_LEN&&a.artist[i];i++)p[20+i]=(unsigned char)a.artist[i];
    }else if(slot==0){
        dv_header(p,deck,'T',a.generation);for(unsigned i=0;i<DV_KEY&&a.key[i];i++)p[20+i]=dv_key_byte(a.key[i]);
        if(a.text_until_ms>now)memcpy(p+24,a.text_override,DV_TITLE);
        else if(!a.title_hidden)memcpy(p+24,a.title,DV_TITLE);
    }else{
        int32_t per_px=s.zoom==1?2:1,back=200*per_px+100,ahead=280*per_px+300;
        if(back<300)back=300;
        if(ahead<700)ahead=700;
        int32_t center=(int32_t)(s.position_ms/DV_COLUMN_MS),lo=center-back,hi=center+ahead,*sweep=&legacy_sweep[d],jump=center-legacy_last_center[d];
        if(*sweep<lo||*sweep>=hi||jump>150||jump< -150)*sweep=center-200*per_px;
        legacy_last_center[d]=center;if(*sweep<0)*sweep=0;
        if((uint32_t)*sweep>=a.columns)*sweep=lo>0?lo:0;
        if((uint32_t)*sweep>=a.columns)return 0;
        uint32_t first=(uint32_t)*sweep,count=a.columns-first;uint16_t columns[DV_COLUMNS_PER_RECORD];if(count>DV_COLUMNS_PER_RECORD)count=DV_COLUMNS_PER_RECORD;
        if(oq_feature_api->analysis_columns(deck,a.generation,first,count,columns)!=(int)count)return 0;
        dv_header(p,deck,'W',a.generation);p[10]=(unsigned char)count;dv_put32(p+20,first);
        for(uint32_t i=0;i<count;i++){p[24+2*i]=(unsigned char)columns[i];p[25+2*i]=(unsigned char)(columns[i]>>8);}
        *sweep+=(int32_t)count;if(*sweep>=hi)*sweep=lo;
    }
    return 1;
}
static void legacy_enabled(int on){
    __atomic_store_n(&legacy_on,0,__ATOMIC_RELEASE);__atomic_fetch_add(&legacy_epoch,1,__ATOMIC_RELEASE);
    oq_feature_enabled(on);__atomic_store_n(&legacy_on,!!on,__ATOMIC_RELEASE);
}
int oqmod_init(const struct oqmod_api *a,struct oqmod_info *info){
    if(!a||a->size<sizeof *a||!a->analysis_snapshot||!a->analysis_columns||!a->analysis_beats||!a->side_record_provider||!a->native_get||!a->native_engine)return 1;
    if(oq_feature_init(a,info)||!a->native_get("engine_external",0)||!a->native_engine())return 1;
    struct oqmod_side_record_provider provider={sizeof provider,legacy_record};
    if(!a->side_record_provider(OQMOD_SIDE_RECORD_LEGACY,&provider))return 1;
    info->enabled=legacy_enabled;info->version="2.0";return 0;
}

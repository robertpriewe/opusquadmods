/* 1.93 (research, hardware 2026-11-15): deck 1 kept playing (waveform moving) but channel 1's
 * meter stayed dark for good while channels 2/3 played. Either EP136 stopped sending deck 1's
 * audio to the mixer, or the mixer stopped taking channel 1. The probe tells them apart:
 *  - the snd_pcm_writei GOT hook (xp_writei, installed at start-up now) keeps the loudest sample
 *    of each deck pair of the 8-channel deck output;
 *  - once a second: a playing deck whose pair stays silent (< -90 dB) for 2 s is logged with its
 *    channel input and fader, and again when it comes back;
 *  - every change of a mixer channel's input (DECK / LINE / ...) is logged;
 *  - the PCM devices the hook saw (channels / format), once. */
#ifndef PRODJLINK_AUDIOPROBE_H
#define PRODJLINK_AUDIOPROBE_H
#define AU_SILENT 0x10000u   /* of 2^31: about -90 dB */
static void au_tick(const struct oqmod_api *api) {
    static int installed;static uint64_t last;
    if(!installed){installed=1;int ok=xp_mute_install();lp_log("audio probe: writei hook %s",ok?"on":"NOT installed");}
    uint64_t now=lk_now_ms();
    if(now-last<1000)return;
    last=now;
    static unsigned pcm_logged;
    for(unsigned i=0;i<4;i++)if(xp_fmts[i].pcm&&!(pcm_logged>>i&1u)){pcm_logged|=1u<<i;
        lp_log("audio probe: pcm %p ok %d channels %d format %d (deck output = 8 ch format 10)",xp_fmts[i].pcm,xp_fmts[i].ok,xp_fmts[i].channels,xp_fmts[i].format);}
    static unsigned calls_logged;
    if(!calls_logged&&now>60000){calls_logged=1;lp_log("audio probe: %u writei calls, %u on the deck output",xp_probe_calls,xp_probe_deck_calls);}
    /* channel inputs */
    static int last_in[4]={-2,-2,-2,-2};
    for(unsigned c=1;c<=4;c++){int in=ch_input(c);if(in!=last_in[c-1]){
        if(last_in[c-1]!=-2||in>=0)lp_log("audio probe: channel %u input %d -> %d",c,last_in[c-1],in);
        last_in[c-1]=in;}}
    struct oqmod_mixer m;memset(&m,0,sizeof m);m.size=sizeof m;int have_mixer=api->mixer&&api->mixer(&m)&&m.valid;
    static uint64_t silent_since[4],last_summary;static unsigned reported[4];
    unsigned pks[4],play_mask=0;
    for(unsigned d=1;d<=4;d++){
        unsigned pk=pks[d-1]=__atomic_exchange_n(&xp_peak[d-1],0u,__ATOMIC_RELAXED);
        struct oqmod_deck info;memset(&info,0,sizeof info);int playing=api->deck(d,&info)&&info.loaded&&info.playing;
        if(playing)play_mask|=1u<<(d-1);
        if(!xp_probe_deck_calls){silent_since[d-1]=0;continue;}
        if(playing&&pk<AU_SILENT){
            if(!silent_since[d-1])silent_since[d-1]=now;
            if(!reported[d-1]&&now-silent_since[d-1]>=2000){reported[d-1]=1;
                lp_log("audio probe: deck %u PLAYING but its output is SILENT for %llu ms (peak %u) at %u of %u ms '%.40s'; channel %u input %d fader %u",
                    d,(unsigned long long)(now-silent_since[d-1]),pk,info.elapsed_ms,info.length_ms,info.title,d,ch_input(d),have_mixer?m.fader[d-1]:9999u);}
        } else {
            if(reported[d-1]&&pk>=AU_SILENT)lp_log("audio probe: deck %u output back (peak %u) after %llu ms",d,pk,(unsigned long long)(now-silent_since[d-1]));
            if(pk>=AU_SILENT||!playing){reported[d-1]=0;silent_since[d-1]=0;}
        }
    }
    /* every 10 s: which pair carries which deck (the pair-to-deck order is assumed, not proven) */
    if(xp_probe_deck_calls&&now-last_summary>=10000){last_summary=now;
        lp_log("audio probe: pair peaks %u %u %u %u, decks playing %x, faders %u %u %u %u",pks[0],pks[1],pks[2],pks[3],play_mask,
            have_mixer?m.fader[0]:0u,have_mixer?m.fader[1]:0u,have_mixer?m.fader[2]:0u,have_mixer?m.fader[3]:0u);}
}
#endif

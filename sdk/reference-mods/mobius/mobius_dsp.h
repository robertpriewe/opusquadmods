/* Original oscillator prototype, not Pioneer/AZ DSP code or an exact emulation.
 * Hann-windowed octave stack creates a continuous rising/falling pitch illusion.
 * Initialize tables off the audio thread. Rendering: fixed memory, no locks/I/O,
 * allocation or transcendentals; independent state per future audio route. */
#ifndef OQ_MOBIUS_DSP_H
#define OQ_MOBIUS_DSP_H
#include <stdint.h>
#include <math.h>
#include <string.h>
#define MB_TABLE 4096u
#define MB_VOICES 8u
/* wave[w][l]: w 0 sine, 1 triangle, 2 saw (the XDJ-AZ manual: X-PAD = oscillator
 * waveform); l = band-limit level: 0 up to 12 harmonics, 1 up to 4, 2 the fundamental. */
#define MB_WAVES 3u
struct mb_tables {float sine[MB_TABLE+1],frequency[MB_TABLE+1],window[MB_TABLE+1],wave[MB_WAVES][3][MB_TABLE+1];};
struct mb_osc {double phase[MB_VOICES],sweep;float envelope;};
static void mb_tables_init(struct mb_tables *t){
    for(unsigned i=0;i<=MB_TABLE;i++){
        double p=(double)i/MB_TABLE;
        t->sine[i]=(float)sin(6.283185307179586*p);
        t->frequency[i]=(float)(30.0*exp2(8.0*p));
        t->window[i]=(float)(0.5-0.5*cos(6.283185307179586*p));
    }
    static const unsigned harmonics[3]={12,4,1};
    for(unsigned w=0;w<MB_WAVES;w++)for(unsigned l=0;l<3;l++){
        float *x=t->wave[w][l];double peak=0;
        for(unsigned i=0;i<=MB_TABLE;i++){
            double p=(double)i/MB_TABLE,s=0;
            for(unsigned n=1;n<=(w?harmonics[l]:1u);n++){
                if(w==1&&!(n&1))continue;                                   /* triangle: odd, 1/n^2, alternating */
                double a=w==1?((((n-1)/2)&1)?-1.0:1.0)/((double)n*n):1.0/n;  /* saw: all, 1/n */
                s+=a*sin(6.283185307179586*n*p);
            }
            x[i]=(float)s;if(fabs(s)>peak)peak=fabs(s);
        }
        for(unsigned i=0;i<=MB_TABLE;i++)x[i]=(float)(x[i]/peak);
    }
}
static float mb_lookup(const float *table,double phase){
    double x=phase*MB_TABLE;unsigned i=(unsigned)x;
    if(i>=MB_TABLE)return table[MB_TABLE];
    return table[i]+(table[i+1]-table[i])*(float)(x-i);
}
static void mb_osc_reset(struct mb_osc *s){memset(s,0,sizeof *s);}
/* cycle_seconds: seconds per octave, negative descends. gain capped at 0.2.
 * on/off uses an at-most-10 ms ramp; caller supplies a distinct output buffer. */
static void mb_render_wave(const struct mb_tables *t,struct mb_osc *s,float *out,unsigned n,
                      double rate,double cycle_seconds,float gain,int on,unsigned wave){
    if(!out)return;
    if(!t||!s||!isfinite(rate)||rate<8000||rate>192000||!isfinite(cycle_seconds)||fabs(cycle_seconds)<0.01||fabs(cycle_seconds)>32||!isfinite(gain)){
        memset(out,0,n*sizeof *out);return;
    }
    if(gain<0)gain=0;
    if(gain>0.2f)gain=0.2f;
    float target=on?gain:0,step=(float)(0.2/(rate*0.01));
    double sweep_step=1.0/(cycle_seconds*rate);
    for(unsigned i=0;i<n;i++){
        float sum=0;
        for(unsigned v=0;v<MB_VOICES;v++){
            double p=((double)v+s->sweep)/MB_VOICES;if(p>=1)p-=1;
            double hz=mb_lookup(t->frequency,p);
            /* Band-limit by omitting voices above Nyquist, including low rates. */
            if(hz<rate*0.45){
                float fade=hz>rate*0.4?(float)((rate*0.45-hz)/(rate*0.05)):1.0f;
                /* fewer harmonics as the voice climbs, so nothing folds back above Nyquist */
                unsigned level=hz*12<rate*0.45?0u:hz*4<rate*0.45?1u:2u;
                const float *table=wave<MB_WAVES&&wave?t->wave[wave][level]:t->sine;
                sum+=mb_lookup(table,s->phase[v])*mb_lookup(t->window,p)*fade;
            }
            s->phase[v]+=hz/rate;if(s->phase[v]>=1)s->phase[v]-=floor(s->phase[v]);
        }
        if(s->envelope<target){s->envelope+=step;if(s->envelope>target)s->envelope=target;}
        else if(s->envelope>target){s->envelope-=step;if(s->envelope<target)s->envelope=target;}
        out[i]=sum*(2.0f/MB_VOICES)*s->envelope;
        s->sweep+=sweep_step;
        /* A full octave step shifts every voice by one octave. Keep a full
         * eight-octave cycle so individual oscillator phases stay continuous. */
        if(s->sweep>=MB_VOICES)s->sweep-=MB_VOICES;
        if(s->sweep<0)s->sweep+=MB_VOICES;
    }
}
static void mb_render(const struct mb_tables *t,struct mb_osc *s,float *out,unsigned n,
                      double rate,double cycle_seconds,float gain,int on){
    mb_render_wave(t,s,out,n,rate,cycle_seconds,gain,on,0);
}
#endif

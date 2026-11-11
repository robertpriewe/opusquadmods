#ifndef OQ_DECK_VIEW_WIRE_H
#define OQ_DECK_VIEW_WIRE_H
/* R79 deck-view protocol: host -> side controller, one 64-byte private record
 * per free deck-frame slot (frame+0x78), the same carrier as OQBP/OQPLST.
 * Shared verbatim by the host module and the controller; pure and bounded.
 *
 *   OQDP0001 state  : flags, track generation, playhead/length (ms), BPM x100,
 *                     tempo x100, one local beat (ms, interval us, beat in bar),
 *                     zoom (p[13]: 0 normal, 1 wide, 2 close, 3 tight),
 *                     theme (p[14]: 0 dark, 1 light), active loop in/out ms
 *                     (p[40..47], with DV_FLAG_LOOP), deck colour RGB565 p[48..49]
 *                     (R102: SHORTCUT > Deck Color; 0 = not known yet)
 *   OQDT0001 text   : generation, key (4 chars), title (40 chars)
 *   OQDC0001 cues   : generation, hot cues A..H (ms or 0xffffffff), colours
 *   OQDM0001 memory : generation, up to 10 memory cues (ms or 0xffffffff)
 *   OQDL0001 loops  : generation, up to 5 saved loops (start, end ms)
 *   OQDW0001 columns: generation, first column, up to 20 waveform columns
 *   OQDB0001 browse  : rich side-browser page header (R85): p[10] rows, p[20] selected,
 *                      p[21] 1 = track list, total (p[24]) and first row (p[28]) as in
 *                      the OQBP page, header text p[32..63]; generation = page generation
 *   OQDR0001 row     : p[10] row 0..4, title p[20..59]
 *   OQDA0001 row info: p[10] row, BPM x100 p[20..21], key p[22..25], colour RGB
 *                      p[26..28], artist p[29..63]
 * The browse records only add detail to the OQBP page they match; without them, or on
 * any mismatch, the controller draws the plain page as before.
 *
 * A waveform column covers 20 ms (50 per second, PWV7 decimated by 3) and is
 * low | mid<<4 | high<<8 (4 bits each) | DV_COLUMN_PRESENT. Every record names
 * its deck twice (p[0]/16 and p[11]) and carries the host's per-deck track
 * generation, so a stale record for a previous track is ignored. */
#include <stdint.h>
enum {
    DV_FLAG_LOADED=1, DV_FLAG_PLAYING=2, DV_FLAG_POSITION=4, DV_FLAG_GRID=8, DV_FLAG_LOOP=16,
    DV_COLUMN_MS=20, DV_COLUMNS_PER_RECORD=20, DV_CUES=8, DV_TITLE=40, DV_KEY=4,
    DV_COLUMN_PRESENT=0x8000
};
enum { DV_NONE=0, DV_STATE=1, DV_TEXT=2, DV_CUE=3, DV_WAVE=4, DV_MEMORY=5, DV_LOOPS=6, DV_ARTIST=7,
       DV_BROWSE=8, DV_BROWSE_ROW=9, DV_BROWSE_INFO=10 };   /* kinds >= DV_BROWSE are the side browser's */
enum { DV_ARTIST_LEN=44 };   /* R93 'N': the deck's artist, p[20..63], for the deck view */
enum { DV_BROWSE_ROWS=5, DV_BROWSE_TITLE=40, DV_BROWSE_ARTIST=35, DV_BROWSE_HEADER=32 };
enum { DV_MEMORY_CUES=10, DV_LOOP_COUNT=5 };
/* R100: key text (Am, Ebm) keeps its small minor 'm' and flat 'b' as bytes 1 and 2: the
 * controller draws everything else in capitals. */
enum { DV_KEY_MINOR=1, DV_KEY_FLAT=2 };
static inline unsigned char dv_key_byte(char c) {
    return c=='m'?DV_KEY_MINOR:c=='b'?DV_KEY_FLAT:c>=32&&c<127?(unsigned char)c:'?';
}
struct dv_state {
    uint32_t generation,position_ms,length_ms,beat_ms,beat_us,loop_in_ms,loop_out_ms;
    uint16_t bpm_x100,color;int16_t tempo_x100;uint8_t flags,beat,zoom,theme;
};
static inline uint32_t dv_get32(const unsigned char *p) {return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static inline void dv_put32(unsigned char *p,uint32_t v) {p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);}
static inline void dv_zero(unsigned char *p,unsigned n) {for(unsigned i=0;i<n;i++)p[i]=0;}
static inline int dv_kind(const unsigned char *p) {
    static const unsigned char head[6]={'O','Q','D','?','0','0'};
    for(unsigned i=0;i<6;i++)if(i!=3&&p[2+i]!=head[i])return DV_NONE;
    if(p[8]!='0'||p[9]!='1')return DV_NONE;
    switch(p[5]) {case 'P':return DV_STATE;case 'T':return DV_TEXT;case 'C':return DV_CUE;case 'W':return DV_WAVE;
                  case 'M':return DV_MEMORY;case 'L':return DV_LOOPS;
                  case 'B':return DV_BROWSE;case 'R':return DV_BROWSE_ROW;case 'A':return DV_BROWSE_INFO;case 'N':return DV_ARTIST;}
    return DV_NONE;
}
static inline void dv_header(unsigned char p[64],unsigned deck,char kind,uint32_t generation) {
    dv_zero(p,64);p[0]=(unsigned char)(deck*16);p[1]=0x33;
    p[2]='O';p[3]='Q';p[4]='D';p[5]=(unsigned char)kind;p[6]='0';p[7]='0';p[8]='0';p[9]='1';
    p[11]=(unsigned char)deck;dv_put32(p+16,generation);
}
static inline void dv_encode_state(unsigned char p[64],unsigned deck,const struct dv_state *s) {
    dv_header(p,deck,'P',s->generation);p[10]=s->flags;p[12]=s->beat;p[13]=(unsigned char)(s->zoom&3);p[14]=(unsigned char)(s->theme&1);
    dv_put32(p+20,s->position_ms);dv_put32(p+24,s->length_ms);
    dv_put32(p+28,s->beat_ms);dv_put32(p+32,s->beat_us);
    p[36]=(unsigned char)s->bpm_x100;p[37]=(unsigned char)(s->bpm_x100>>8);
    p[38]=(unsigned char)(uint16_t)s->tempo_x100;p[39]=(unsigned char)((uint16_t)s->tempo_x100>>8);
    if(s->flags&DV_FLAG_LOOP) {dv_put32(p+40,s->loop_in_ms);dv_put32(p+44,s->loop_out_ms);}
    p[48]=(unsigned char)s->color;p[49]=(unsigned char)(s->color>>8);
}
/* Validates the common envelope for the controller's own deck (1..4). */
static inline int dv_valid(const unsigned char *p,unsigned deck) {
    if(deck<1||deck>4||p[0]!=deck*16||p[1]!=0x33||p[11]!=deck)return 0;
    int kind=dv_kind(p);
    if(kind==DV_STATE) {
        if(p[13]>3||p[14]>1||p[15])return 0;
        for(unsigned i=(p[10]&DV_FLAG_LOOP)?50:40;i<64;i++)if(p[i]&&(i>>1)!=24)return 0;
    }
    if(kind==DV_WAVE&&(p[10]<1||p[10]>DV_COLUMNS_PER_RECORD))return 0;
    if(kind==DV_BROWSE&&(p[10]>DV_BROWSE_ROWS||p[20]>=(p[10]?p[10]:1)||p[21]>1||!dv_get32(p+16)))return 0;
    if((kind==DV_BROWSE_ROW||kind==DV_BROWSE_INFO)&&(p[10]>=DV_BROWSE_ROWS||!dv_get32(p+16)))return 0;
    if(kind>=DV_TEXT)if(p[12]||p[13]||p[14]||p[15])return 0;
    return kind;
}
static inline void dv_decode_state(const unsigned char *p,struct dv_state *s) {
    s->generation=dv_get32(p+16);s->flags=p[10];s->beat=p[12];s->zoom=(uint8_t)(p[13]&3);s->theme=(uint8_t)(p[14]&1);
    s->position_ms=dv_get32(p+20);s->length_ms=dv_get32(p+24);
    s->beat_ms=dv_get32(p+28);s->beat_us=dv_get32(p+32);
    s->bpm_x100=(uint16_t)(p[36]|(p[37]<<8));s->color=(uint16_t)(p[48]|(p[49]<<8));s->tempo_x100=(int16_t)(uint16_t)(p[38]|(p[39]<<8));
    s->loop_in_ms=(s->flags&DV_FLAG_LOOP)?dv_get32(p+40):0;s->loop_out_ms=(s->flags&DV_FLAG_LOOP)?dv_get32(p+44):0;
}
#endif

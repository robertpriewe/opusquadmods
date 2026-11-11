#ifndef OQ_BROWSER_WIRE_H
#define OQ_BROWSER_WIRE_H
/* Pure bounded transport for a 256-byte text page; never a raw framebuffer.
 * The outer stock deck frame supplies its normal CRC and sequence as before.
 * A separate page CRC plus generation prevents torn/stale page presentation. */
#include <stdint.h>
static inline void bw_copy(unsigned char *d,const unsigned char *s,unsigned n) {for(unsigned i=0;i<n;i++)d[i]=s[i];}
static inline void bw_fill(unsigned char *d,unsigned n) {for(unsigned i=0;i<n;i++)d[i]=0;}
static inline int bw_same(const unsigned char *a,const unsigned char *b,unsigned n) {for(unsigned i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;}
enum { BW_PAGE=256,BW_PART=40,BW_PARTS=7 };
struct bw_rx {uint32_t generation,crc,seen;unsigned closed;unsigned char page[BW_PAGE];};
static inline uint32_t bw_word(const unsigned char *p) {return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static inline void bw_put(unsigned char *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(i*8));}
static inline uint32_t bw_crc(const unsigned char *p,unsigned size) {
    uint32_t crc=~0u;
    for(unsigned i=0;i<size;i++) {crc^=p[i];for(unsigned j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}
    return ~crc;
}
static inline int bw_marker(const unsigned char *p) {return bw_same(p+2,(const unsigned char *)"OQBP0001",8);}
static inline int bw_packet(unsigned char p[64],unsigned deck,uint32_t generation,const unsigned char *page,unsigned part) {
    if(deck<1||deck>4||!generation||(page&&part>=BW_PARTS))return 0;
    bw_fill(p,64);p[0]=(unsigned char)(deck*16);p[1]=0x33;
    bw_copy(p+2,(const unsigned char *)"OQBP0001",8);p[10]=page?1:2;p[11]=(unsigned char)deck;
    bw_put(p+16,generation);
    if(page) {
        unsigned n=part==6?16:BW_PART;p[12]=(unsigned char)part;p[13]=BW_PARTS;p[14]=(unsigned char)n;
        bw_put(p+20,bw_crc(page,BW_PAGE));bw_copy(p+24,page+part*BW_PART,n);
    }
    return 1;
}
/* -1 invalid private packet, 0 incomplete/duplicate/stale, 1 page, 2 close.
 * One original controller RX thread owns the assembly; GUI publication separate. */
static inline int bw_receive(struct bw_rx *r,const unsigned char p[64],unsigned deck) {
    if(!bw_marker(p)||deck<1||deck>4||p[0]!=deck*16||p[1]!=0x33||p[11]!=deck||p[15])return -1;
    uint32_t generation=bw_word(p+16);if(!generation)return -1;
    if(r->generation&&generation!=r->generation&&(int32_t)(generation-r->generation)<=0)return 0;
    if(p[10]==2) {
        for(unsigned i=12;i<64;i++)if((i<16||i>=20)&&p[i])return -1;
        if(generation==r->generation&&r->closed)return 0;
        r->generation=generation;r->seen=0;r->closed=1;return 2;
    }
    if(p[10]!=1||p[12]>=BW_PARTS||p[13]!=BW_PARTS||p[14]!=(p[12]==6?16:BW_PART))return -1;
    for(unsigned i=24+p[14];i<64;i++)if(p[i])return -1;
    if(generation==r->generation&&r->closed)return 0;
    uint32_t crc=bw_word(p+20);
    if(generation!=r->generation) {r->generation=generation;r->seen=0;r->closed=0;r->crc=crc;}
    if(crc!=r->crc)return -1;
    unsigned bit=1u<<p[12];
    if(r->seen&bit) {
        if(!bw_same(r->page+p[12]*BW_PART,p+24,p[14])) {r->seen=0;return -1;}
        return 0;
    }
    bw_copy(r->page+p[12]*BW_PART,p+24,p[14]);r->seen|=bit;
    if(r->seen!=127)return 0;
    if(bw_crc(r->page,BW_PAGE)!=r->crc) {r->seen=0;return -1;}
    return 1;
}
static inline int bw_emit(unsigned char *frame,unsigned length,uint16_t *sequence,unsigned deck,uint32_t generation,const unsigned char *page,unsigned part) {
    unsigned char p[64];
    if(!frame||!sequence||length!=192||((frame[1]>>4)&1)!=((deck-1)&1)||(frame[0x74]&1)||!bw_packet(p,deck,generation,page,part))return 0;
    bw_fill(frame+0x74,68);frame[0x74]=1;*sequence=(uint16_t)(*sequence+1);
    frame[0x76]=(unsigned char)*sequence;frame[0x77]=(unsigned char)(*sequence>>8);
    bw_copy(frame+0x78,p,64);return 1;
}
#endif

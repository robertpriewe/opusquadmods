#ifndef OQ_TITLE_BLANK_H
#define OQ_TITLE_BLANK_H
/* Hide Track Names on the stock side screen: blank a track's title wherever it appears
 * in an outgoing text chunk, without knowing the chunk's layout. The title is searched
 * as UTF-16LE and as UTF-8; every run of at least TB_MIN characters that matches part
 * of the title (a chunk may hold the start, middle or end of a long one) becomes spaces.
 * Shorter titles must match whole. Returns the number of characters blanked. */
#include <stddef.h>
#include <stdint.h>
#define TB_MIN 6   /* shorter runs hit other text: "ong " of "Song" matches "Long " */
#define TB_UNITS 256

/* UTF-8 -> UTF-16 code units (BMP plus surrogate pairs); stops at bad input. */
static size_t tb_utf16(const char *s,uint16_t *out,size_t cap) {
    const unsigned char *p=(const unsigned char *)s;size_t n=0;
    while(*p&&n<cap) {
        uint32_t c;unsigned k;
        if(*p<0x80){c=*p;k=1;}
        else if((*p&0xe0)==0xc0){c=*p&0x1f;k=2;}
        else if((*p&0xf0)==0xe0){c=*p&0x0f;k=3;}
        else if((*p&0xf8)==0xf0){c=*p&0x07;k=4;}
        else break;
        for(unsigned i=1;i<k;i++){if((p[i]&0xc0)!=0x80)return n;c=c<<6|(p[i]&0x3f);}
        p+=k;
        if(c>=0x10000){if(n+2>cap)break;c-=0x10000;out[n++]=(uint16_t)(0xd800|c>>10);out[n++]=(uint16_t)(0xdc00|(c&0x3ff));}
        else out[n++]=(uint16_t)c;
    }
    return n;
}
/* Longest m such that buf[at..at+m) equals some run of `title` (unit bytes each). */
static size_t tb_longest(const unsigned char *buf,size_t n,size_t at,const unsigned char *title,size_t tn,size_t unit) {
    size_t best=0;
    for(size_t q=0;q+unit<=tn;q+=unit) {
        size_t m=0;
        while(at+m+unit<=n&&q+m+unit<=tn&&!memcmp(buf+at+m,title+q+m,unit))m+=unit;
        if(m>best)best=m;
    }
    return best;
}
static unsigned tb_scan(unsigned char *buf,size_t n,const unsigned char *title,size_t tn,size_t unit) {
    if(!tn)return 0;
    size_t need=(tn/unit<TB_MIN?tn/unit:TB_MIN)*unit;
    unsigned blanked=0;
    for(size_t at=0;at+need<=n;) {
        size_t m=tb_longest(buf,n,at,title,tn,unit);
        if(m>=need) {
            for(size_t i=0;i<m;i+=unit){buf[at+i]=' ';if(unit==2)buf[at+i+1]=0;}
            blanked+=(unsigned)(m/unit);at+=m;
        } else at++;
    }
    return blanked;
}
static unsigned tb_blank(unsigned char *buf,size_t n,const char *title) {
    if(!buf||!title||!*title)return 0;
    uint16_t units[TB_UNITS];size_t u=tb_utf16(title,units,TB_UNITS);
    unsigned char wide[2*TB_UNITS];
    for(size_t i=0;i<u;i++){wide[2*i]=(unsigned char)units[i];wide[2*i+1]=(unsigned char)(units[i]>>8);}
    size_t len=strlen(title);if(len>TB_UNITS)len=TB_UNITS;
    return tb_scan(buf,n,wide,2*u,2)+tb_scan(buf,n,(const unsigned char *)title,len,1);
}
#endif

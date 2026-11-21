#ifndef OQ_PRODJLINK_NAMES_H
#define OQ_PRODJLINK_NAMES_H
/* Research: which device-name builder EP136 uses for a mounted link USB, and its MediaID.
 * gui::device_name::SourceSelectDefaultDeviceNameProvider vtable 0x28f94e0 (slots 2-17,
 * one per device kind; slot 7 0x17c99e8 = "USB%d" from MediaID byte 4, slot 6 0x17c97e8 =
 * "No Name (USB%d)"). Each call: slot, 8 MediaID bytes, the two result strings
 * (juce::String = char* at +0 and +8 of the returned pair). x8 holds the result pair. */
#define NM_SLOTS 16
void *nm_orig[NM_SLOTS];static unsigned nm_lines,nm_installed;
static void nm_note(unsigned k,uint64_t res,uint64_t media) {
    if(nm_lines>=80)return;
    unsigned char m[8]={0};nt_peek(media,m,8);
    uint64_t s0=0,s1=0;char t0[48]="",t1[48]="";
    if(nt_peek(res,(unsigned char *)&s0,8)&&s0)text_or_empty(s0,t0,sizeof t0);
    if(nt_peek(res+8,(unsigned char *)&s1,8)&&s1)text_or_empty(s1,t1,sizeof t1);
    nm_lines++;
    lp_log("devname slot %u media %02x %02x %02x %02x %02x %02x %02x %02x -> '%s' / '%s'",k+2,m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],t0,t1);
}
/* Each wrapper: keep x8 (result pair) and x2 (MediaID) in callee-saved registers, call
 * the original with all argument registers untouched, then nm_note(k, x8, x2). */
void nm_note_c(unsigned k,uint64_t res,uint64_t media){nm_note(k,res,media);}
#define NM_STUB(k) \
    __asm__(".text\n.p2align 2\n.globl nm_w" #k "\n.type nm_w" #k ",%function\nnm_w" #k ":\n" \
            "stp x29,x30,[sp,#-48]!\nmov x29,sp\nstp x19,x20,[sp,#16]\nstr x21,[sp,#32]\n" \
            "mov x19,x8\nmov x20,x2\nadrp x16,nm_orig\nadd x16,x16,:lo12:nm_orig\nldr x16,[x16,#" #k "*8]\nblr x16\n" \
            "mov x21,x0\nmov w0," #k "\nmov x1,x19\nmov x2,x20\nbl nm_note_c\nmov x0,x21\n" \
            "ldr x21,[sp,#32]\nldp x19,x20,[sp,#16]\nldp x29,x30,[sp],#48\nret\n.size nm_w" #k ",.-nm_w" #k "\n");
NM_STUB(0)
NM_STUB(1)
NM_STUB(2)
NM_STUB(3)
NM_STUB(4)
NM_STUB(5)
NM_STUB(6)
NM_STUB(7)
NM_STUB(8)
NM_STUB(9)
NM_STUB(10)
NM_STUB(11)
NM_STUB(12)
NM_STUB(13)
NM_STUB(14)
NM_STUB(15)
extern char nm_w0[],nm_w1[],nm_w2[],nm_w3[],nm_w4[],nm_w5[],nm_w6[],nm_w7[],nm_w8[],nm_w9[],nm_w10[],nm_w11[],nm_w12[],nm_w13[],nm_w14[],nm_w15[];
static void *const nm_wrappers[NM_SLOTS]={nm_w0,nm_w1,nm_w2,nm_w3,nm_w4,nm_w5,nm_w6,nm_w7,nm_w8,nm_w9,nm_w10,nm_w11,nm_w12,nm_w13,nm_w14,nm_w15};
static void nm_install(const struct oqmod_api *api) {
    if(nm_installed||!api->ep136_ok||!api->ep136_ok())return;
    nm_installed=1;unsigned ok=0;
    const uintptr_t vt=0x28f94e0;
    for(unsigned k=0;k<NM_SLOTS;k++){uintptr_t slot=vt+8*(k+2);if(api->patch(slot,*(const uintptr_t *)slot,nm_wrappers[k],&nm_orig[k])==1)ok++;}
    lp_log("devname trace: %u of %u slots",ok,NM_SLOTS);
}
#endif

#ifndef PSXHLE_EMU_IFC_REGS_H
#define PSXHLE_EMU_IFC_REGS_H

// the regs as macros cause conflicts with std::map includes and things,
// so keep them in thier own header so we can include it as late as possible.
// (todo: convert these to c++ constexpr references or somehting...)
// 
//#define zr (GPR_ARRAY[0])
#define at (GPR_ARRAY[1])
#define v0 (GPR_ARRAY[2])
#define v1 (GPR_ARRAY[3])
#define a0 (GPR_ARRAY[4])
#define a1 (GPR_ARRAY[5])
#define a2 (GPR_ARRAY[6])
#define a3 (GPR_ARRAY[7])
#define t0 (GPR_ARRAY[8])
#define t1 (GPR_ARRAY[9])
#define t2 (GPR_ARRAY[10])
#define t3 (GPR_ARRAY[11])
#define t4 (GPR_ARRAY[12])
#define t5 (GPR_ARRAY[13])
#define t6 (GPR_ARRAY[14])
#define t7 (GPR_ARRAY[15])
#define t8 (GPR_ARRAY[16])
#define t9 (GPR_ARRAY[17])
#define s0 (GPR_ARRAY[18])
#define s1 (GPR_ARRAY[19])
#define s2 (GPR_ARRAY[20])
#define s3 (GPR_ARRAY[21])
#define s4 (GPR_ARRAY[22])
#define s5 (GPR_ARRAY[23])
#define s6 (GPR_ARRAY[24])
#define s7 (GPR_ARRAY[25])
#define k0 (GPR_ARRAY[26])
#define k1 (GPR_ARRAY[27])
#define gp (GPR_ARRAY[28])
#define sp (GPR_ARRAY[29])
#define fp (GPR_ARRAY[30])
#define ra (GPR_ARRAY[31])

#if HLE_PCSX_IFC
// to help verify the behavior of these implementations on PCSX, undef the macros and re-implement using
// cross-platform versions. This also makes it easy to disable this on PCSX and test this specific code for regressions.
#   undef PSXM
#   undef psxMu32ref
#   undef psxMu32
#endif

static u8* PSXM(u32 unmasked) {
    auto masked = unmasked & 0x1fff'ffff;

    if (masked < PS1_RamMirrorSize) {
        return PSX_RAM_START + (masked & (PS1_RamPhysicalSize - 1));
    }
    else if (masked >= 0x1f800000 && masked < (0x1f800000 + PS1_FASTRAMSIZE)) {
        return PSX_SPR_START + (masked-0x1f800000);
    }
    else if (masked >= 0x1fc00000 && masked < (0x1fc00000 + PS1_BIOSSIZE)) {
        return PSX_RAM_START + (masked-0x1fc00000);
    }
    else {
        dbg_check(false);
    }
    //else if (auto* entry = ioHandlers_[HASH_IOADDR(addr)]) {
    //	return entry->ioRead32(addr);
    //}

    return PSX_RAM_START + masked;
}

static u32& psxMu32ref(u32 addr) {
    return (u32&)*PSXM(addr);
}

static u32 psxMu32(u32 addr) {
    return *(u32*)PSXM(addr);
}

#define Ra0 ((char *)PSXM(a0))
#define Ra1 ((char *)PSXM(a1))
#define Ra2 ((char *)PSXM(a2))
#define Ra3 ((char *)PSXM(a3))
#define Rv0 ((char *)PSXM(v0))
#define Rsp ((char *)PSXM(sp))

#endif

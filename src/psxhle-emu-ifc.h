// For internal use by libPsxHleBios only.

#ifndef PSXHLE_EMU_IFC_H
#define PSXHLE_EMU_IFC_H

#include "libpsxbios.h"
#include "icy_assert.h"

#if HLE_PCSX_IFC
#   include "psxcommon.h"
#else
    // this is until we get code updated to use just cstdint types.
    // (once bios is stable, refactoring of typenames can be done)
    using u32  = uint32_t;
    using s32  = int32_t;
    using u16  = uint16_t;
    using s16  = int16_t;
    using u8   = uint8_t;
    using s8   = int8_t;
    using uptr = uintptr_t;
    using sptr = intptr_t;
#endif

#if HLE_PCSX_IFC
#   include "psxhw.h"
#   include "gpu.h"
#   include "sio.h"
#endif

#if HLE_MEDNAFEN_IFC
#   include "mednafen/psx/dis.h"
#   include "mednafen/psx/psx.h"
#   include "mednafen/psx/timer.h"
#endif

#if HLE_DUCKSTATION_IFC
#   include "core/cpu_core.h"
#   include "core/cpu_core_private.h"
#   include "core/bus.h"
#   include "core/gpu.h"
#   include "core/dma.h"
#   include "core/timers.h"
#   include "core/interrupt_controller.h"
#   include "core/pad.h"
#   include "core/cpu_code_cache.h"
#endif

#include <cstdint>

#if _MSC_VER
#   pragma warning(disable : 4244)      // this one is just silly.
#   pragma warning(disable : 4245)      // this is for 64-bit pointer cast to 32 bit int. It would be nice to enable but all the stdlib stuff depends on it for now.
#   pragma warning(disable : 4389)      // '==': signed/unsigned mismatch
#   pragma warning(disable : 4505)      // unref'd function removed.
#endif

#define HLE_FULL                1       // enables full ROM-less HLE support

// HLE exception handler depends on full HLE (everything in the list has to be 1)
#define HLE_ENABLE_EXCEPTION    (HLE_FULL && 1)

// Dev notes:
//  * Tekken 2/3 do not use threads
//  * Tekken 2/3 do not use root counters (rcnt)
//  * Tekken 2/3 do not use the Event system (DeliverEvent, etc)
//  * Tekken 2/3 do not use GPU APIs

#define HLE_ENABLE_HEAP         (HLE_FULL || 1)
#define HLE_ENABLE_GPU			(HLE_FULL || 1)
#define HLE_ENABLE_RCNT			(HLE_FULL || 1)

// Rest of these are not useful due to interdependence on exception handler and full hle.

#define HLE_ENABLE_THREAD       (HLE_FULL || 1)

#ifndef HLE_ENABLE_MCD
#define HLE_ENABLE_MCD			(HLE_FULL || 1)
#endif

#define HLE_ENABLE_EVENT        (HLE_FULL || 1)
#define HLE_ENABLE_LOADEXEC		(HLE_FULL || 1)       // depends on ISO9660 filesystem API

#define HLE_ENABLE_PAD			(HLE_PCSX_IFC || (HLE_FULL && 1))
#define HLE_ENABLE_ENTRYINT     (HLE_PCSX_IFC || (HLE_FULL && 1))

// qsort needs to be rewritten before it can be enabled. And once rewritten, probably can remove
// the conditional build for it.. no good reason to disable it except right now it doesn't build --jstine
#define HLE_ENABLE_QSORT        0

// Controls yield behavior, whether the emulator runs recursively into an interpreter or attempts to
// yield out instead.
#if !defined(HLE_ENABLE_YIELD)
#   define HLE_ENABLE_YIELD     0
#endif

#if !defined(PSXBIOS_LOG)
#   define PSXBIOS_LOG(...) (printf("[HLEBIOS] " __VA_ARGS__), fflush(nullptr))
//#   define PSXBIOS_LOG(...) (void(0))
#endif

// new psxbios with signature matching PSXBIOS_LOG_SPAM.
#if !defined(PSXBIOS_LOG_NEW)
#   define PSXBIOS_LOG_NEW(func, ...) (printf("[HLEBIOS] " func " " __VA_ARGS__), fflush(nullptr))
#endif

#if !defined(PSXBIOS_LOG_SPAM)
#   define PSXBIOS_LOG_SPAM(func, ...) ( !is_suppressed(func) && (printf("[HLEBIOS] " func " " __VA_ARGS__), fflush(nullptr), 1))
#endif

#if HLE_PCSX_IFC
extern char McdDisable[2];

#define PSX_RAM_START ((u8*)psxM)
#define PSX_ROM_START ((u8*)psxR)
#define PSX_SPR_START ((u8*)psxH)

#define GPR_ARRAY (psxRegs.GPR.r)
#define pc0 (psxRegs.pc)
#define lo  (psxRegs.GPR.n.lo)
#define hi  (psxRegs.GPR.n.hi)

#define CP0_EPC      (psxRegs.CP0.n.EPC	  )
#define CP0_CAUSE    (psxRegs.CP0.n.Cause   )
#define CP0_STATUS   (psxRegs.CP0.n.Status  )

#define RCNT_SetCount(rid, val)     psxRcntWcount (rid, val)
#define RCNT_SetMode(rid, val)      psxRcntWmode  (rid, val)
#define RCNT_SetTarget(rid, val)    psxRcntWtarget(rid, val)
#define RCNT_GetCount(rid)          psxRcntRcount (rid)
#define RCNT_GetMode(rid)           psxRcntRmode  (rid)
#define RCNT_GetTarget(rid)         psxRcntRtarget(rid)

static void Write_ISTAT(u32 val)    { psxHwWrite32(0x1f801070, val); }
static void Write_IMASK(u32 val)    { psxHwWrite32(0x1f801074, val); }
static void Write_MEMCTRL2(u32 val) { psxHwWrite32(0x1f801060, val); }
static u32 Read_ISTAT()    { return psxHu32(0x1070); }
static u32 Read_IMASK()    { return psxHu32(0x1074); }
static u32 Read_MEMCTRL2() { return psxHu32(0x1060); }

static void SetPC(uint32_t newpc) {
    psxRegs.pc = newpc;
}

static void psxCpuClear(u32 startPC, int size_in_words)
{
    psxCpu->Clear(startPC, size_in_words);
}
#endif

#if HLE_MEDNAFEN_IFC
#define GPR_ARRAY (PSX_CPU->GPR)
#define pc0 (PSX_CPU->BACKED_PC)
#define lo  (PSX_CPU->LO)
#define hi  (PSX_CPU->HI)

#define CP0_EPC      (PSX_CPU->CP0.EPC	  )
#define CP0_CAUSE    (PSX_CPU->CP0.CAUSE  )
#define CP0_STATUS   (PSX_CPU->CP0.SR     )

#define PSX_RAM_START (MainRAM->data8)
#define PSX_ROM_START (BIOSROM->data8)
#define PSX_SPR_START (ScratchRAM->data8)

#define RCNT_SetCount(rid, val)    TIMER_Write(0, ((rid) << 4) | 0x00, val)
#define RCNT_SetMode(rid, val)     TIMER_Write(0, ((rid) << 4) | 0x04, val)
#define RCNT_SetTarget(rid, val)   TIMER_Write(0, ((rid) << 4) | 0x08, val)
#define RCNT_GetCount(rid)         TIMER_Read (0, ((rid) << 4) | 0x00)
#define RCNT_GetMode(rid)          TIMER_Read (0, ((rid) << 4) | 0x04)
#define RCNT_GetTarget(rid)        TIMER_Read (0, ((rid) << 4) | 0x08)

//  Weird APIs by Mednafen here... They take an address input, but only care about the 4 LSBs.
//  They are meant for accessing 0x1070 (ISTAT) and 0x1074 (IMASK) in the hardware register map.
//  I like to search on 1070 and 1074 in PSX emulators since it's a common pattern when
//  looking for ISTAT and IMASK, so I used those addresses in the function call helpers.. --jstine

// BUGGED? note that IRQ_Write and IRQ_Read as implemented by Mednafen are dodgy.
//   IRQ_Write is missing masking operations on MASK.
//   IRQ_Read is injecting random garbage on writes to unaligned addresses (1071, 1072, etc).
//     (fortunately writes to those addresses are rare or impossible, real HW ignored them --jstine).

static void Write_ISTAT(u32 val) { IRQ_Write(0x1070, val); }
static void Write_IMASK(u32 val) { IRQ_Write(0x1074, val); }
static void Write_MEMCTRL2(u32 val) { /* NOP */; }
static u32 Read_ISTAT() { return IRQ_Read(0x1070); }
static u32 Read_IMASK() { return IRQ_Read(0x1074); }
static u32 Read_MEMCTRL2() { return 0; }

static void SetPC(uint32_t newpc) {
    PSX_CPU->BACKED_PC = newpc;
    PSX_CPU->BACKED_new_PC = PSX_CPU->BACKED_PC + 4;
}

static void psxCpuClear(u32 startPC, int size_in_words)
{
    PSX_CPU->Clear(startPC, size_in_words);
}
#endif

#if HLE_DUCKSTATION_IFC
#define GPR_ARRAY (CPU::g_state.regs.r)
#define pc0       (CPU::g_state.regs.pc)
#define lo        (CPU::g_state.regs.lo)
#define hi        (CPU::g_state.regs.hi)

#define CP0_EPC      (CPU::g_state.cop0_regs.EPC         )
#define CP0_CAUSE    (CPU::g_state.cop0_regs.cause.bits  )
#define CP0_STATUS   (CPU::g_state.cop0_regs.sr   .bits  )

#define PSX_RAM_START (Bus::g_ram)
#define PSX_ROM_START (Bus::g_bios)
#define PSX_SPR_START (CPU::g_state.dcache.data())

#define RCNT_SetCount(rid, val)     g_timers.WriteRegister(((rid) << 4) | 0x00, val)
#define RCNT_SetMode(rid, val)      g_timers.WriteRegister(((rid) << 4) | 0x04, val)
#define RCNT_SetTarget(rid, val)    g_timers.WriteRegister(((rid) << 4) | 0x08, val)
#define RCNT_GetCount(rid)          g_timers.ReadRegister (((rid) << 4) | 0x00)
#define RCNT_GetMode(rid)           g_timers.ReadRegister (((rid) << 4) | 0x04)
#define RCNT_GetTarget(rid)         g_timers.ReadRegister (((rid) << 4) | 0x08)

static void Write_ISTAT(u32 val) { g_interrupt_controller.WriteRegister(0, val); }  // 1070
static void Write_IMASK(u32 val) { g_interrupt_controller.WriteRegister(4, val); }  // 1074
static u32 Read_ISTAT()   { return g_interrupt_controller.ReadRegister(0); }  // 1070
static u32 Read_IMASK()   { return g_interrupt_controller.ReadRegister(4); }  // 1074

namespace Bus {
    extern void HleWriteMEMCTRL2(u32 val);
    extern u32 HleReadMEMCTRL2();
}


static void Write_MEMCTRL2(u32 val) { Bus::HleWriteMEMCTRL2(val); }
static u32 Read_MEMCTRL2() { return Bus::HleReadMEMCTRL2(); }  // 1060

static void SetPC(uint32_t newpc) {
    CPU::SetPC(newpc);
}

static void psxCpuClear(u32 startPC, int size_in_words)
{
    // may need this, tho Duckstation's self-checking should cover all the bases for now.
    //psxCpu->Clear(startPC, size_in_words);
}

#endif

static const uint32_t PS1_ICacheSize		= 0x00001000; // 4KB	(instruction cache)
static const uint32_t PS1_RamPhysicalSize	= 0x00200000; // 2MB	(physical)
static const uint32_t PS1_RamMirrorSize		= 0x00800000; // 8MB	(addressable, mirrored)
static const uint32_t PS1_FASTRAMSIZE		= 0x00000400; // 1KB
static const uint32_t PS1_BIOSSIZE			= 0x00080000; // 512KB
static const uint32_t PS1_BIOSRAMSIZE		= 0x00010000; // 512KB
static const uint32_t PS1_SegmentAddrMask	= 0x1fffffff; // masks away all segment information, useful since most emu operations don't need to care

static const uint32_t PS1_FastRamStart		= 0x1f800000;
static const uint32_t PS1_FastRamEnd		= 0x1f800000 + PS1_FASTRAMSIZE;
static const uint32_t PS1_BiosRomStart		= 0x1fc00000;
static const uint32_t PS1_BiosRomEnd		= 0x1fc00000 + PS1_BIOSSIZE;

static uint32_t& at = (GPR_ARRAY[1]);
static uint32_t& v0 = (GPR_ARRAY[2]);
static uint32_t& v1 = (GPR_ARRAY[3]);
static uint32_t& a0 = (GPR_ARRAY[4]);
static uint32_t& a1 = (GPR_ARRAY[5]);
static uint32_t& a2 = (GPR_ARRAY[6]);
static uint32_t& a3 = (GPR_ARRAY[7]);
static uint32_t& t0 = (GPR_ARRAY[8]);
static uint32_t& t1 = (GPR_ARRAY[9]);
static uint32_t& t2 = (GPR_ARRAY[10]);
static uint32_t& t3 = (GPR_ARRAY[11]);
static uint32_t& t4 = (GPR_ARRAY[12]);
static uint32_t& t5 = (GPR_ARRAY[13]);
static uint32_t& t6 = (GPR_ARRAY[14]);
static uint32_t& t7 = (GPR_ARRAY[15]);
static uint32_t& s0 = (GPR_ARRAY[16]);
static uint32_t& s1 = (GPR_ARRAY[17]);
static uint32_t& s2 = (GPR_ARRAY[18]);
static uint32_t& s3 = (GPR_ARRAY[19]);
static uint32_t& s4 = (GPR_ARRAY[20]);
static uint32_t& s5 = (GPR_ARRAY[21]);
static uint32_t& s6 = (GPR_ARRAY[22]);
static uint32_t& s7 = (GPR_ARRAY[23]);
static uint32_t& t8 = (GPR_ARRAY[24]);
static uint32_t& t9 = (GPR_ARRAY[25]);
static uint32_t& k0 = (GPR_ARRAY[26]);
static uint32_t& k1 = (GPR_ARRAY[27]);
static uint32_t& gp = (GPR_ARRAY[28]);
static uint32_t& sp = (GPR_ARRAY[29]);
static uint32_t& fp = (GPR_ARRAY[30]);
static uint32_t& ra = (GPR_ARRAY[31]);

#if HLE_PCSX_IFC

// to help verify the behavior of these implementations on PCSX, undef the macros and re-implement using
// cross-platform versions. This also makes it easy to disable this on PCSX and test this specific code for regressions.
#   undef PSXM
#   undef psxMu32ref
#   undef psxMu32
#endif

static uint8_t* PSXM(uint32_t unmasked) {
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

static uint32_t& psxMu32ref(uint32_t addr) { return  (uint32_t&)*PSXM(addr); }
static uint32_t  psxMu32   (uint32_t addr) { return  (uint32_t&)*PSXM(addr); }

#define Ra0 ((char *)PSXM(a0))
#define Ra1 ((char *)PSXM(a1))
#define Ra2 ((char *)PSXM(a2))
#define Ra3 ((char *)PSXM(a3))
#define Rv0 ((char *)PSXM(v0))
#define Rsp ((char *)PSXM(sp))

#if HLE_PCSX_IFC

static void HleExecuteRecursive(u32 startPC, u32 returnPC) {
    pc0 = startPC;
    ra = returnPC;

    hleSoftCall = TRUE;
    while (pc0 != 0x80001000) psxCpu->ExecuteBlock();
    hleSoftCall = FALSE;
}
#endif

#if HLE_MEDNAFEN_IFC
static void HleExecuteRecursive(u32 startPC, u32 returnPC) {
    pc0 = startPC;
    ra = returnPC;

    // FIXME: add recursive interpreter execution support to mednafen
    hleSoftCall = TRUE;
    while (pc0 != 0x80001000) PSX_CPU->ExecuteBlock();
    hleSoftCall = FALSE;
}
#endif

#if HLE_DUCKSTATION_IFC
namespace CPU
{
namespace CodeCache
{
    extern void HleExecuteRecursive(u32 startPC, u32 exitPC);
}
}

static void HleExecuteRecursive(u32 startPC, u32 returnPC) {
    CPU::CodeCache::HleExecuteRecursive(startPC, returnPC);
}

static void ClearAllCaches() {
    CPU::ClearICache();
    CPU::CodeCache::Flush();
}
#endif


// HleYieldUid is a combination of the following traits:
//  - the BIOS call table thunk address (A0/B0/C0 are standard BIOS thunks), max value 0xffff
//  - the BIOS call ID (the thunk uses this to look up the callsite), max value 0xff
//  - the yield state of the HLE call being invoked (0 = first entry), max value 0xff
//
// The original BIOS has thunks at A0/B0/C0. A custom HLE could add pseudo-addresses.
// The yield state is determined by the yield system API, which auto-increments the yield state at each
// call to a yield site.
using HleYieldUid = uint32_t;

HleYieldUid MakeYieldCallId(uint32_t biosCallPage, uint32_t biosCallId);

// Pick an unmapped area of PSX memory to treat as soft call return address.
static const u32 kSoftCallBaseRetAddr = 0x8100'0000;

static bool IsHlePC(u32 pc) {
    return ((pc & 0xff00'0000) == kSoftCallBaseRetAddr);
}

// tableAddress - eg. A0, B0, C0
static HleYieldUid HleMakeYieldUid(u32 thunkAddr, u32 callIdx, u32 yieldIdx) {
    dbg_check(thunkAddr <= 0xffff);
    dbg_check(yieldIdx   <= 0xff);
    dbg_check(callIdx    <= 0xff);

    return ((thunkAddr << 16) | (callIdx << 8) | yieldIdx);
}

static HleYieldUid HleGetCallId(u32 pc) {
    return (HleYieldUid)(pc & ~kSoftCallBaseRetAddr);
}

#endif

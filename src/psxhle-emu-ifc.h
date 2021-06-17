// For internal use by libPsxHleBios only.

#ifndef PSXHLE_EMU_IFC_H
#define PSXHLE_EMU_IFC_H

#include "libpsxbios.h"
#include "icy_assert.h"

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
#   include "core/bus.h"
#   include "core/gpu.h"
#   include "core/dma.h"
#   include "core/timers.h"
#   include "core/interrupt_controller.h"
#endif

#include <cstdint>

#if _MSC_VER
#   pragma warning(disable : 4244)      // this one is just silly.
#   pragma warning(disable : 4245)      // this is for 64-bit pointer cast to 32 bit int. It would be nice to enable but all the stdlib stuff depends on it for now.
#   pragma warning(disable : 4505)      // unref'd function removed.
#endif

#if !HLE_PCSX_IFC
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
#define HLE_ENABLE_MCD			(HLE_FULL || 1) && !HLE_DUCKSTATION_IFC
#define HLE_ENABLE_EVENT        (HLE_FULL || 1)
#define HLE_ENABLE_LOADEXEC		(HLE_FULL || 1)       // depends on ISO9660 filesystem API

#define HLE_ENABLE_FILEIO		(HLE_PCSX_IFC || (HLE_FULL && 1))       // fileio depends on HLE memcard ?
#define HLE_ENABLE_PAD			(HLE_PCSX_IFC || (HLE_FULL && 1))
#define HLE_ENABLE_ENTRYINT     (HLE_PCSX_IFC || (HLE_FULL && 1))

#define HLE_ENABLE_FINDFILE     (0            || (HLE_FULL && 0))
#define HLE_ENABLE_FORMAT       (0            || (HLE_FULL && 0))

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

static void Write_ISTAT(u32 val) { psxHwWrite32(0x1f801070, val); }
static void Write_IMASK(u32 val) { psxHwWrite32(0x1f801074, val); }
static u32 Read_ISTAT() { return psxHu32(0x1070); }
static u32 Read_IMASK() { return psxHu32(0x1074); }

static void SetPC(uint32_t newpc) {
    psxRegs.pc = newpc;
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
static u32 Read_ISTAT() { return IRQ_Read(0x1070); }
static u32 Read_IMASK() { return IRQ_Read(0x1074); }

static void SetPC(uint32_t newpc) {
    PSX_CPU->BACKED_PC = newpc;
    PSX_CPU->BACKED_new_PC = PSX_CPU->BACKED_PC + 4;
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

static void SetPC(uint32_t newpc) {
    CPU::SetPC(newpc);
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

// HleYieldUid is the A0/B0/C0 value plus the original call ID.
// This is used as the values come from the games themselves and are therefore fixed and unique
// for any bios function, and will save us a lot of paperwork for maintaining savestate compat
// or inventing some new sequence of values.
using HleYieldUid = uint32_t;

HleYieldUid MakeYieldCallId(uint32_t biosCallPage, uint32_t biosCallId);

#endif

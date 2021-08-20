/***************************************************************************
 *   Copyright (C) 2019 Ryan Schultz, PCSX-df Team, PCSX team, gameblabla, *
 *   dmitrysmagin, senquack                                                *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02111-1307 USA.           *
 ***************************************************************************/

/* Gameblabla 2018-2019 :
 * Numerous changes to bios calls as well as improvements in order to conform to nocash's findings
 * for the PSX bios calls. Thanks senquack for helping out with some of the changes
 * and helping to spot issues and refine my patches.
 * */

/*
 * Internal simulated HLE BIOS.
 */

// TODO: implement all system calls, count the exact CPU cycles of system calls.

#include "libpsxbios.h"
#include "psxhle-emu-ifc.h"

#include "psxhle-filesystem.h"
#include "psdisc-types.h"
#include "psdisc-endian.h"
#include "jfmt.h"

#include "StringTokenizer.h"
#include "StringUtil.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <map>

#if !defined(HAS_ZLIB)
#   define HAS_ZLIB         1
#endif

#if HAS_ZLIB
#   include <zlib.h>
#endif

// Magic value to match the PSX "ABI"
const u32 TCB_THREAD_FREE     = 0x1000;
const u32 TCB_THREAD_RESERVED = 0x4000;
const u32 SIZEOF_TCB = 0xC0;
// The real size of the TCB is set in system;cnf (or by a call to table:0xA id:0x9C)
u32 TCB_MAX_THREADS = 8;

const u32 USEG_MASK   = 0x1FFF'FFFF;
const u32 KSEG        = 0x8000'0000;
const u32 G_PROCESS   = 0x0108;
const u32 G_THREADS   = 0x0110;
const u32 TABLE_A0    = 0x0200; // vector for call a0
const u32 TABLE_B0    = 0x0874; // vector for call b0
const u32 TABLE_C0    = 0x0674; // vector for call c0
// Allocate some data at the end of the kernel space
const u32 KERNEL_PCB  = 0xE000;                      // store process control block info here
const u32 KERNEL_TCB  = KERNEL_PCB + 4;              // store thread control block info here
const u32 KERNEL_HEAP = KERNEL_TCB + SIZEOF_TCB * 8; // Start address of remaining space

template<typename T, typename T2>
void StoreToLE(T& dest, const T2& src) {
    static_assert(std::is_fundamental<T>::value && std::is_fundamental<T2>::value);
    static_assert(sizeof(T) == sizeof(T2));
    dest = LoadFromLE((T)src);
}

template<typename T, typename T2>
void StoreToBE(T& dest, const T& src) {
    static_assert(std::is_fundamental<T>::value && std::is_fundamental<T2>::value);
    static_assert(sizeof(T) == sizeof(T2));
    dest = LoadFromBE((T)src);
}


// Keep trace of the event status to only print change
static u8 s_debug_ev[256*256];

static bool s_suppress_spam = 0;
static std::map<std::string, int> s_repeat_supress;

static bool is_suppressed(const char* check) {
    return s_suppress_spam && (++s_repeat_supress[check] > 2);
};


static bool hle_config_get_bool(std::string opt) {
    return 1;
    auto woo = "PSX_HLE_CONFIG_" + opt;
    if (const char* const env = getenv(woo.c_str())) {
        auto walk = env;
        while(isspace(int(walk[0]))) ++walk;
        char bval = walk[0];
        while(isspace(int(walk[0]))) ++walk;

        if (!env[0] || env[1] || !isdigit(int(bval))) {
            fprintf(stderr, "env parse error at %s=%s\nValid boolean expressions are 0 or 1\n", woo.c_str(), env);
        }

        if (bval == '0') return 0;
        return 1;       // any non-zero value is boolean true
    }
    return 0;
}

static bool hle_config_env_rcnt		() { return hle_config_get_bool("RCNT"      ); }
static bool hle_config_env_pad		() { return hle_config_get_bool("PAD"       ); }
static bool hle_config_env_fileio	() { return hle_config_get_bool("FILEIO"    ); }
static bool hle_config_env_mcd		() { return hle_config_get_bool("MCD"       ); }
static bool hle_config_env_loadexec	() { return hle_config_get_bool("LOADEXEC"  ); }
static bool hle_config_env_gpu		() { return hle_config_get_bool("GPU"       ); }
static bool hle_config_env_thread   () { return hle_config_get_bool("THREAD"    ); }
static bool hle_config_env_entryint () { return hle_config_get_bool("ENTRYINT"  ); }
static bool hle_config_env_heap     () { return hle_config_get_bool("HEAP"      ); }
static bool hle_config_env_event    () { return hle_config_get_bool("EVENT"     ); }
static bool hle_config_env_full     () { return hle_config_get_bool("FULL"      ); }

#undef SysPrintf
#define SysPrintf(fmt, ...) (printf(fmt, ##__VA_ARGS__), fflush(stdout))

const char * const biosA0n[256] = {
// 0x00
    "open",		"lseek",	"read",		"write",
    "close",	"ioctl",	"exit",		"sys_a0_07",
    "getc",		"putc",		"todigit",	"atof",
    "strtoul",	"strtol",	"abs",		"labs",
// 0x10
    "atoi",		"atol",		"atob",		"setjmp",
    "longjmp",	"strcat",	"strncat",	"strcmp",
    "strncmp",	"strcpy",	"strncpy",	"strlen",
    "index",	"rindex",	"strchr",	"strrchr",
// 0x20
    "strpbrk",	"strspn",	"strcspn",	"strtok",
    "strstr",	"toupper",	"tolower",	"bcopy",
    "bzero",	"bcmp",		"memcpy",	"memset",
    "memmove",	"memcmp",	"memchr",	"rand",
// 0x30
    "srand",	"qsort",	"strtod",	"malloc",
    "free",		"lsearch",	"bsearch",	"calloc",
    "realloc",	"InitHeap",	"_exit",	"getchar",
    "putchar",	"gets",		"puts",		"printf",
// 0x40
    "sys_a0_40",		"LoadTest",					"Load",		"Exec",
    "FlushCache",		"InstallInterruptHandler",	"GPU_dw",	"mem2vram",
    "SendGPUStatus",	"GPU_cw",					"GPU_cwb",	"SendPackets",
    "sys_a0_4c",		"GetGPUStatus",				"GPU_sync",	"sys_a0_4f",
// 0x50
    "sys_a0_50",		"LoadExec",				"GetSysSp",		"sys_a0_53",
    "_96_init()",		"_bu_init()",			"_96_remove()",	"sys_a0_57",
    "sys_a0_58",		"sys_a0_59",			"sys_a0_5a",	"dev_tty_init",
    "dev_tty_open",		"sys_a0_5d",			"dev_tty_ioctl","dev_cd_open",
// 0x60
    "dev_cd_read",		"dev_cd_close",			"dev_cd_firstfile",	"dev_cd_nextfile",
    "dev_cd_chdir",		"dev_card_open",		"dev_card_read",	"dev_card_write",
    "dev_card_close",	"dev_card_firstfile",	"dev_card_nextfile","dev_card_erase",
    "dev_card_undelete","dev_card_format",		"dev_card_rename",	"dev_card_6f",
// 0x70
    "_bu_init",			"_96_init",		"_96_remove",		"sys_a0_73",
    "sys_a0_74",		"sys_a0_75",	"sys_a0_76",		"sys_a0_77",
    "_96_CdSeekL",		"sys_a0_79",	"sys_a0_7a",		"sys_a0_7b",
    "_96_CdGetStatus",	"sys_a0_7d",	"_96_CdRead",		"sys_a0_7f",
// 0x80
    "sys_a0_80",		"sys_a0_81",	"sys_a0_82",		"sys_a0_83",
    "sys_a0_84",		"_96_CdStop",	"sys_a0_86",		"sys_a0_87",
    "sys_a0_88",		"sys_a0_89",	"sys_a0_8a",		"sys_a0_8b",
    "sys_a0_8c",		"sys_a0_8d",	"sys_a0_8e",		"sys_a0_8f",
// 0x90
    "sys_a0_90",		"sys_a0_91",	"sys_a0_92",		"sys_a0_93",
    "sys_a0_94",		"sys_a0_95",	"AddCDROMDevice",	"AddMemCardDevide",
    "DisableKernelIORedirection",		"EnableKernelIORedirection", "sys_a0_9a", "sys_a0_9b",
    "SetConf",			"GetConf",		"sys_a0_9e",		"SetMem",
// 0xa0
    "_boot",			"SystemError",	"EnqueueCdIntr",	"DequeueCdIntr",
    "sys_a0_a4",		"ReadSector",	"get_cd_status",	"bufs_cb_0",
    "bufs_cb_1",		"bufs_cb_2",	"bufs_cb_3",		"_card_info",
    "_card_load",		"_card_auto",	"bufs_cd_4",		"sys_a0_af",
// 0xb0
    "sys_a0_b0",		"sys_a0_b1",	"do_a_long_jmp",	"sys_a0_b3",
    "?? sub_function",
};

const char * const biosB0n[256] = {
// 0x00
    "SysMalloc",		"sys_b0_01",	"sys_b0_02",	"sys_b0_03",
    "sys_b0_04",		"sys_b0_05",	"sys_b0_06",	"DeliverEvent",
    "OpenEvent",		"CloseEvent",	"WaitEvent",	"TestEvent",
    "EnableEvent",		"DisableEvent",	"OpenTh",		"CloseTh",
// 0x10
    "ChangeTh",			"sys_b0_11",	"InitPAD",		"StartPAD",
    "StopPAD",			"PAD_init",		"PAD_dr",		"ReturnFromExecption",
    "ResetEntryInt",	"HookEntryInt",	"sys_b0_1a",	"sys_b0_1b",
    "sys_b0_1c",		"sys_b0_1d",	"sys_b0_1e",	"sys_b0_1f",
// 0x20
    "UnDeliverEvent",	"sys_b0_21",	"sys_b0_22",	"sys_b0_23",
    "sys_b0_24",		"sys_b0_25",	"sys_b0_26",	"sys_b0_27",
    "sys_b0_28",		"sys_b0_29",	"sys_b0_2a",	"sys_b0_2b",
    "sys_b0_2c",		"sys_b0_2d",	"sys_b0_2e",	"sys_b0_2f",
// 0x30
    "sys_b0_30",		"sys_b0_31",	"open",			"lseek",
    "read",				"write",		"close",		"ioctl",
    "exit",				"sys_b0_39",	"getc",			"putc",
    "getchar",			"putchar",		"gets",			"puts",
// 0x40
    "cd",				"format",		"firstfile",	"nextfile",
    "rename",			"delete",		"undelete",		"AddDevice",
    "RemoteDevice",		"PrintInstalledDevices", "InitCARD", "StartCARD",
    "StopCARD",			"sys_b0_4d",	"_card_write",	"_card_read",
// 0x50
    "_new_card",		"Krom2RawAdd",	"sys_b0_52",	"sys_b0_53",
    "_get_errno",		"_get_error",	"GetC0Table",	"GetB0Table",
    "_card_chan",		"sys_b0_59",	"sys_b0_5a",	"ChangeClearPAD",
    "_card_status",		"_card_wait",
};

const char * const biosC0n[256] = {
// 0x00
    "InitRCnt",			  "InitException",		"SysEnqIntRP",		"SysDeqIntRP",
    "get_free_EvCB_slot", "get_free_TCB_slot",	"ExceptionHandler",	"InstallExeptionHandler",
    "SysInitMemory",	  "SysInitKMem",		"ChangeClearRCnt",	"SystemError",
    "InitDefInt",		  "sys_c0_0d",			"sys_c0_0e",		"sys_c0_0f",
// 0x10
    "sys_c0_10",		  "sys_c0_11",			"InstallDevices",	"FlushStfInOutPut",
    "sys_c0_14",		  "_cdevinput",			"_cdevscan",		"_circgetc",
    "_circputc",		  "ioabort",			"sys_c0_1a",		"KernelRedirect",
    "PatchAOTable",
};

#if HLE_PCSX_IFC
void VmcReadNV(int port, int slot, void* dest, int offset, int size) {
    dbg_check((u32)port < 2);
}

bool VmcEnabled(int port, int slot = 0) {
    dbg_check((u32)port < 2);
    return !McdDisable[port];
}

char* VmcGet(int port) {
    dbg_check((u32)port < 2);
    auto* dest = port ? Mcd2Data : Mcd1Data;
    return dest;
}

void VmcCreate(int port) {
    dbg_check((u32)port < 2);
    if (port == 0) {
        CreateMcd(Config.Mcd1);
        LoadMcd(1, Config.Mcd1);
    } else {
        CreateMcd(Config.Mcd2);
        LoadMcd(2, Config.Mcd2);
    }
}

void VmcDirty(int port) {
    dbg_check((u32)port < 2);
    // Flush the full mcard, don't bother with partial write
    SaveMcd(port ? Config.Mcd2 : Config.Mcd1, VmcGet(port), 0, MCD_SIZE);
}

void VmcWriteNV(int port, int dst_offset, const void* src, int size) {
    dbg_check((u32)port < 2);
    auto dst = VmcGet(port);
    if (dst == nullptr) return;

    memcpy(dst + dst_offset, src, size);
    VmcDirty(port);
}
#endif

#if HLE_MEDNAFEN_IFC && HLE_ENABLE_MCD
#include "mednafen/psx/frontio.h"
extern FrontIO *PSX_FIO;        // defined by libretro. dunno why this isn't baked into the PSX core for mednafen. --jstine
void VmcWriteNV(int port, int slot, const void* src, int offset, int size) {
    dbg_check((u32)port < 2);
    if (auto mcd = PSX_FIO->GetMemcardDevice(port))
        mcd->WriteNV((const uint8_t*)src, offset, size);
}

void VmcReadNV(int port, int slot, void* dest, int offset, int size) {
    dbg_check((u32)port < 2);
    if (auto mcd = PSX_FIO->GetMemcardDevice(port))
        mcd->ReadNV((uint8_t*)dest, offset, size);
}

bool VmcEnabled(int port, int slot = 0) {
    dbg_check((u32)port < 2);
    if (auto mcd = PSX_FIO->GetMemcardDevice(port)) {
        return 1;
    }

    return 0;
}

char* VmcGet(int port) {
    dbg_check((u32)port < 2);
    return PSX_FIO->GetMemcardDevice(port);
}

void VmcCreate(int port) {
    dbg_check((u32)port < 2);
}
#endif

#if HLE_DUCKSTATION_IFC && HLE_ENABLE_MCD
void VmcDirty(int port) {
    dbg_check((u32)port < 2);
    // FIXME need to set m_changed of the memory card in DS to flush the memory
    // card to the disk
}

char* VmcGet(int port) {
    dbg_check((u32)port < 2);
    auto card = g_pad.GetMemoryCard(port);
    auto& data = card->GetData();
    return (char*)data.data();
}

void VmcWriteNV(int port, int dst_offset, const void* src, int size) {
    dbg_check((u32)port < 2);
    auto dst = VmcGet(port);
    if (dst == nullptr) return;

    memcpy(dst + dst_offset, src, size);
    VmcDirty(port);
}

void VmcReadNV(int port, int slot, void* dest, int offset, int size) {
    dbg_check((u32)port < 2);
}

bool VmcEnabled(int port, int slot = 0) {
    dbg_check((u32)port < 2);
    auto card = g_pad.GetMemoryCard(port);
    return card != nullptr;
}

void VmcCreate(int port) {
}
#endif

typedef struct {
    u32 desc;
    s32 status;
    s32 mode;
    u32 fhandler;
} EvCB[32];

const int EVENT_STATUS_FREE     = 0x0000;
const int EVENT_STATUS_DISABLED = 0x1000;
const int EVENT_STATUS_ENABLED  = 0x2000; // AKA 'busy'
const int EVENT_STATUS_PENDING  = 0x4000; // AKA 'ready'

const int EVENT_MODE_CALLBACK    = 0x1000;
const int EVENT_MODE_NO_CALLBACK = 0x2000;

/*
typedef struct {
    s32 next;
    s32 func1;
    s32 func2;
    s32 pad;
} SysRPst;
*/

typedef struct {
    u32 status;
    u32 _pad0;
    u32 reg[32];
    u32 func;
    u32 gpr_hi;
    u32 gpr_lo;
    u32 SR;
    u32 cause;
    u32 _pad1[9];
} TCB;
static_assert(sizeof(TCB) == SIZEOF_TCB);

struct DIRENTRY {
    char name[20];
    s32 attr;
    s32 size;
    u32 next;
    s32 head;
    char system[4];
};

typedef struct {
    char name[32];
    u32  mode;
    u32  offset;
    u32  size;
    u32  mcfile;
} FileDesc;

#if HLE_ENABLE_ENTRYINT
static u32 jmp_int = 0;     // mips address
static u32 SysIntRP[8];
#endif

#if HLE_ENABLE_PAD || HLE_FULL
static int *pad_buf = NULL;
static u8 *pad_buf1 = NULL, *pad_buf2 = NULL;
static int pad_buf1len, pad_buf2len;
static int pad_stopped = 1;
#endif

#if HLE_ENABLE_MCD
static int CardState = -1;
static u32 card_active_chan;
#endif

static u32 regs[36];

#if HLE_ENABLE_EVENT
static EvCB *EventCB;
static EvCB *HwEV; // 0xf0
static EvCB *EvEV; // 0xf1
static EvCB *RcEV; // 0xf2
static EvCB *UeEV; // 0xf3
static EvCB *SwEV; // 0xf4
static EvCB *ThEV; // 0xff
#endif

#if HLE_ENABLE_HEAP
static u32 heap_size = 0;
static u32 *heap_addr = NULL;
static u32 *heap_end = NULL;
#endif

static FileDesc FDesc[32];

// oh silly PCSX. they did the classic VM nono and just recursively called the interpreter
// in order to emulate softCalls. A miracle this ever worked.
// This hleSoftCall hack works around the common case failure of one re-entrant call, but fails
// if more than one re-entrant call occurs. (it can also fail in some other edge-casy ways in 
// PCSX depending on some timing and HW state things which aren't being tracked in a re-entrant 
// friendly fashion)

uint8_t hleSoftCall = 0;

// its really helpful to be able to change the call signature of all these functions at once.
#define HLE_BIOS_CALL_ARGS HleYieldUid huid
#define HLE_BIOS_INVOKE_ARGS huid

static inline void softCall(u32 pc) {
    HleExecuteRecursive(pc, 0x80001000);
}

static inline void softCall2(u32 pc) {
    u32 sra = ra;
    HleExecuteRecursive(pc, 0x80001000);
    ra = sra;
}

static inline void CP0_RFE() {
    CP0_STATUS = (CP0_STATUS & 0xfffffff0) | ((CP0_STATUS & 0x3c) >> 2);
}

static inline void CP0_EXCEPTION() {
    // Opposite of CP0_RFE
    CP0_STATUS = (CP0_STATUS & 0xfffffff0) | ((CP0_STATUS & 0x0f) << 2);
}

static inline void DeliverEvent(u32 ev, u32 spec) {
    PSXBIOS_LOG("DeliverEvent %d;%d (%d)\n", ev, spec, EventCB[ev][spec].status);

    if (EventCB[ev][spec].status != EVENT_STATUS_ENABLED) return;

    // EventCB[ev][spec].status = EVENT_STATUS_PENDING;
    if (EventCB[ev][spec].mode == EVENT_MODE_CALLBACK) {
        softCall2(EventCB[ev][spec].fhandler);
    } else
        EventCB[ev][spec].status = EVENT_STATUS_PENDING;
}

void psxBios_todigit(HLE_BIOS_CALL_ARGS) // 0x0a
{
    int c = a0;

    PSXBIOS_LOG("psxBios_%s\n", biosA0n[0x0a]);

    c &= 0xFF;
    if (c >= 0x30 && c < 0x3A) {
        c -= 0x30;
    }
    else if (c > 0x60 && c < 0x7B) {
        c -= 0x20;
    }
    else if (c > 0x40 && c < 0x5B) {
        c = c - 0x41 + 10;
    }
    else if (c >= 0x80) {
        c = -1;
    }
    else
    {
        c = 0x0098967F;
    }
    v0 = c;
    pc0 = ra;
}

void psxBios_abs(HLE_BIOS_CALL_ARGS) { // 0x0e
    if ((s32)a0 < 0) v0 = -(s32)a0;
    else v0 = a0;
    pc0 = ra;
}

void psxBios_labs(HLE_BIOS_CALL_ARGS) { // 0x0f
    psxBios_abs(HLE_BIOS_INVOKE_ARGS);
}

void psxBios_atoi(HLE_BIOS_CALL_ARGS) { // 0x10
    s32 n = 0, f = 0;
    char *p = (char *)Ra0;

    for (;;p++) {
        switch (*p) {
            case ' ': case '\t': continue;
            case '-': f++;
            case '+': p++;
        }
        break;
    }

    while (*p >= '0' && *p <= '9') {
        n = n * 10 + *p++ - '0';
    }

    v0 = (f ? -n : n);
    pc0 = ra;
}

void psxBios_atol(HLE_BIOS_CALL_ARGS) { // 0x11
    psxBios_atoi(HLE_BIOS_INVOKE_ARGS);
}

void psxBios_setjmp(HLE_BIOS_CALL_ARGS) { // 0x13
    u32 *jmp_buf = (u32 *)Ra0;
    int i;

    PSXBIOS_LOG("psxBios_%s\n", biosA0n[0x13]);

    jmp_buf[0] = ra;
    jmp_buf[1] = sp;
    jmp_buf[2] = fp;
    for (i = 0; i < 8; i++) // s0-s7
        jmp_buf[3 + i] = GPR_ARRAY[16 + i];
    jmp_buf[11] = gp;

    v0 = 0; pc0 = ra;
}

void psxBios_longjmp(HLE_BIOS_CALL_ARGS) { // 0x14
    u32 *jmp_buf = (u32 *)Ra0;
    int i;

    PSXBIOS_LOG("psxBios_%s\n", biosA0n[0x14]);

    ra = jmp_buf[0]; /* ra */
    sp = jmp_buf[1]; /* sp */
    fp = jmp_buf[2]; /* fp */
    for (i = 0; i < 8; i++) // s0-s7
        GPR_ARRAY[16 + i] = jmp_buf[3 + i];
    gp = jmp_buf[11]; /* gp */

    v0 = a1; pc0 = ra;
}

void psxBios_strcat(HLE_BIOS_CALL_ARGS) { // 0x15
    char *p1 = (char *)Ra0, *p2 = (char *)Ra1;

    PSXBIOS_LOG("psxBios_%s: %s, %s\n", biosA0n[0x15], Ra0, Ra1);

    if (!a0 || !a1) {
        v0 = 0;
        pc0 = ra;
        return;
    }

    while (*p1++);
    --p1;
    while ((*p1++ = *p2++) != '\0');

    v0 = a0; pc0 = ra;
}

void psxBios_strncat(HLE_BIOS_CALL_ARGS) { // 0x16
    char *p1 = (char *)Ra0, *p2 = (char *)Ra1;
    s32 n = a2;

    PSXBIOS_LOG("psxBios_%s: %s (%x), %s (%x), %d\n", biosA0n[0x16], Ra0, a0, Ra1, a1, a2);

    if (!a0 || !a1) {
        v0 = 0;
        pc0 = ra;
        return;
    }

    while (*p1++);
    --p1;
    while ((*p1++ = *p2++) != '\0') {
        if (--n < 0) {
            *--p1 = '\0';
            break;
        }
    }

    v0 = a0; pc0 = ra;
}

void psxBios_strcmp(HLE_BIOS_CALL_ARGS) { // 0x17
    char *p1 = (char *)Ra0, *p2 = (char *)Ra1;

    PSXBIOS_LOG("psxBios_%s: %s (%x), %s (%x)\n", biosA0n[0x17], Ra0, a0, Ra1, a1);

    if (a0 == 0 && a1 == 0)
    {
        v0 = 0;
        pc0 = ra;
        return;
    }
    else if (a0 == 0 && a1 != 0)
    {
        v0 = -1;
        pc0 = ra;
        return;
    }
    else if (a0 != 0 && a1 == 0)
    {
        v0 = 1;
        pc0 = ra;
        return;
    }

    s32 n=0;
    while (*p1 == *p2++) {
        n++;
        if (*p1++ == '\0') {
            v1=n-1;
            a0+=n;
            a1+=n;
            v0 = 0;
            pc0 = ra;
            return;
        }
    }

    v0  = (*p1 - *--p2);
    v1  = n;
    a0 += n;
    a1 += n;
    pc0 = ra;
}

void psxBios_strncmp(HLE_BIOS_CALL_ARGS) { // 0x18

    PSXBIOS_LOG("psxBios_%s: %s (%x), %s (%x), %d\n", biosA0n[0x18], Ra0, a0, Ra1, a1, a2);

    if (a0 == 0 && a1 == 0)
    {
        v0 = 0;
        pc0 = ra;
        return;
    }
    else if (a0 == 0 && a1 != 0)
    {
        v0 = -1;
        pc0 = ra;
        return;
    }
    else if (a0 != 0 && a1 == 0)
    {
        v0 = 1;
        pc0 = ra;
        return;
    }

    char *p1 = (char *)Ra0, *p2 = (char *)Ra1;
    s32 n = a2;
    while (--n >= 0 && *p1 == *p2++) {
        if (*p1++ == '\0') {
            v0 = 0;
            pc0 = ra;
            v1 = a2 - ((a2-n) - 1);
            a0 += (a2-n) - 1;
            a1 += (a2-n) - 1;
            a2 = n;
            return;
        }
    }

    v0 = (n < 0 ? 0 : *p1 - *--p2);
    v1 = a2 - ((a2-n) - 1);
    a0 += (a2-n) - 1;
    a1 += (a2-n) - 1;
    a2 = n;

    pc0 = ra;
}

void psxBios_strcpy(HLE_BIOS_CALL_ARGS) { // 0x19
    if (!a0 || !a1) {
        v0 = 0;
        pc0 = ra;
        return;
    }

    char *p1 = (char *)Ra0, *p2 = (char *)Ra1;
    while ((*p1++ = *p2++) != '\0');

    v0 = a0; pc0 = ra;
}

void psxBios_strncpy(HLE_BIOS_CALL_ARGS) { // 0x1a
    if (!a0 || !a1) {
        v0 = 0;
        pc0 = ra;
        return;
    }

    char *p1 = (char *)Ra0, *p2 = (char *)Ra1;
    s32 n = a2, i;

    for (i = 0; i < n; i++) {
        if ((*p1++ = *p2++) == '\0') {
            while (++i < n) {
                *p1++ = '\0';
            }
            v0 = a0; pc0 = ra;
            return;
        }
    }

    v0 = a0; pc0 = ra;
}

void psxBios_strlen(HLE_BIOS_CALL_ARGS) { // 0x1b
    //PSXBIOS_LOG("psxBios_%s: %s (%x)\n", biosA0n[0x1b], Ra0, a0);

    if (a0) {
        char *p = Ra0;
        v0 = 0;
        while (*p++) v0++;
    }
    pc0 = ra;
}

void psxBios_index(HLE_BIOS_CALL_ARGS) { // 0x1c
    if (a0) {
        char *p = Ra0;

        do {
            if (*p == a1) {
                v0 = a0 + (p - Ra0);
                pc0 = ra;
                return;
            }
        } while (*p++ != '\0');
    }

    v0 = 0; pc0 = ra;
}

void psxBios_rindex(HLE_BIOS_CALL_ARGS) { // 0x1d
    if (a0) {
        char *p = (char *)Ra0;

        v0 = 0;

        do {
            if (*p == a1)
                v0 = a0 + (p - (char *)Ra0);
        } while (*p++ != '\0');
    }

    pc0 = ra;
}

void psxBios_strchr(HLE_BIOS_CALL_ARGS) { // 0x1e
    psxBios_index(HLE_BIOS_INVOKE_ARGS);
}

void psxBios_strrchr(HLE_BIOS_CALL_ARGS) { // 0x1f
    psxBios_rindex(HLE_BIOS_INVOKE_ARGS);
}

void psxBios_strpbrk(HLE_BIOS_CALL_ARGS) { // 0x20
    char *p1 = (char *)Ra0, *p2 = (char *)Ra1, *scanp, c, sc;

    while ((c = *p1++) != '\0') {
        for (scanp = p2; (sc = *scanp++) != '\0';) {
            if (sc == c) {
                v0 = a0 + (p1 - 1 - (char *)Ra0);
                pc0 = ra;
                return;
            }
        }
    }

    // BUG: return a0 instead of NULL if not found
    v0 = a0; pc0 = ra;
}

void psxBios_strspn(HLE_BIOS_CALL_ARGS) { // 0x21
    char *p1, *p2;

    for (p1 = (char *)Ra0; *p1 != '\0'; p1++) {
        for (p2 = (char *)Ra1; *p2 != '\0' && *p2 != *p1; p2++);
        if (*p2 == '\0') break;
    }

    v0 = p1 - (char *)Ra0; pc0 = ra;
}

void psxBios_strcspn(HLE_BIOS_CALL_ARGS) { // 0x22
    char *p1, *p2;

    for (p1 = (char *)Ra0; *p1 != '\0'; p1++) {
        for (p2 = (char *)Ra1; *p2 != '\0' && *p2 != *p1; p2++);
        if (*p2 != '\0') break;
    }

    v0 = p1 - (char *)Ra0; pc0 = ra;
}

void psxBios_strtok(HLE_BIOS_CALL_ARGS) { // 0x23
    char *pcA0 = (char *)Ra0;
    char *pcRet = strtok(pcA0, (char *)Ra1);
    if (pcRet)
        v0 = a0 + pcRet - pcA0;
    else
        v0 = 0;
    pc0 = ra;
}

void psxBios_strstr(HLE_BIOS_CALL_ARGS) { // 0x24
    char *p = (char *)Ra0, *p1, *p2;

    while (*p != '\0') {
        p1 = p;
        p2 = (char *)Ra1;

        while (*p1 != '\0' && *p2 != '\0' && *p1 == *p2) {
            p1++; p2++;
        }

        if (*p2 == '\0') {
            v0 = a0 + (p - (char *)Ra0);
            pc0 = ra;
            return;
        }

        p++;
    }

    v0 = 0; pc0 = ra;
}

void psxBios_toupper(HLE_BIOS_CALL_ARGS) { // 0x25
    v0 = (s8)(a0 & 0xff);
    if (v0 >= 'a' && v0 <= 'z') v0 -= 'a' - 'A';
    pc0 = ra;
}

void psxBios_tolower(HLE_BIOS_CALL_ARGS) { // 0x26
    v0 = (s8)(a0 & 0xff);
    if (v0 >= 'A' && v0 <= 'Z') v0 += 'a' - 'A';
    pc0 = ra;
}

void psxBios_bcopy(HLE_BIOS_CALL_ARGS) { // 0x27
    char *p1 = (char *)Ra1, *p2 = (char *)Ra0;
    v0 = a0;
    if (a0 == 0 || a2 > 0x7FFFFFFF)
    {
        pc0 = ra;
        return;
    }
    while ((s32)a2-- > 0) *p1++ = *p2++;
    a2 = 0;
    pc0 = ra;
}

void psxBios_bzero(HLE_BIOS_CALL_ARGS) { // 0x28
    //PSXBIOS_LOG("psxBios_%s: %x %x\n", biosA0n[0x28], a0, a1);

    char *p = (char *)Ra0;
    v0 = a0;
    /* Same as memset here (See memset below) */
    if (a1 > 0x7FFFFFFF || a1 == 0)
    {
        v0 = 0;
        pc0 = ra;
        return;
    }
    else if (a0 == 0)
    {
        pc0 = ra;
        return;
    }
    while ((s32)a1-- > 0) *p++ = '\0';
    a1 = 0;

    pc0 = ra;
}

void psxBios_bcmp(HLE_BIOS_CALL_ARGS) { // 0x29
    char *p1 = (char *)Ra0, *p2 = (char *)Ra1;

    if (a0 == 0 || a1 == 0) { v0 = 0; pc0 = ra; return; }

    while ((s32)a2-- > 0) {
        if (*p1++ != *p2++) {
            v0 = *p1 - *p2; // BUG: compare the NEXT byte
            pc0 = ra;
            return;
        }
    }

    v0 = 0; pc0 = ra;
}

void psxBios_memcpy(HLE_BIOS_CALL_ARGS) { // 0x2a
    char *p1 = (char *)Ra0, *p2 = (char *)Ra1;
    v0 = a0;
    if (a0 == 0 || a2 > 0x7FFFFFFF)
    {
        pc0 = ra;
        return;
    }
    while ((s32)a2-- > 0) {
        *p1++ = *p2++;
    }
    a2 = 0;
    pc0 = ra;
}

void psxBios_memset(HLE_BIOS_CALL_ARGS) { // 0x2b
    v0 = a0;
    if (a2 > 0x7FFFFFFF || a2 == 0)
    {
        v0 = 0;
        pc0 = ra;
        return;
    }
    if (a0 == 0)
    {
        pc0 = ra;
        return;
    }

    char *p = (char *)Ra0;
    while ((s32)a2-- > 0) *p++ = (char)a1;

    a2 = 0;
    v0 = a0; pc0 = ra;
}

void psxBios_memmove(HLE_BIOS_CALL_ARGS) { // 0x2c
    char *p1 = (char *)Ra0, *p2 = (char *)Ra1;

    v0 = a0;
    if (a0 == 0 || a2 > 0x7FFFFFFF)
    {
        pc0 = ra;
        return;
    }

    if (p2 <= p1 && p2 + a2 > p1) {
        a2++; // BUG: copy one more byte here
        p1 += a2;
        p2 += a2;
        while ((s32)a2-- > 0) *--p1 = *--p2;
    } else {
        while ((s32)a2-- > 0) *p1++ = *p2++;
    }

    pc0 = ra;
}

void psxBios_memcmp(HLE_BIOS_CALL_ARGS) { // 0x2d
    psxBios_bcmp(HLE_BIOS_INVOKE_ARGS);
}

void psxBios_memchr(HLE_BIOS_CALL_ARGS) { // 0x2e
    char *p = (char *)Ra0;

    if (a0 == 0 || a2 > 0x7FFFFFFF)
    {
        pc0 = ra;
        return;
    }

    while ((s32)a2-- > 0) {
        if (*p++ != (s8)a1) continue;
        v0 = a0 + (p - (char *)Ra0 - 1);
        pc0 = ra;
        return;
    }

    v0 = 0; pc0 = ra;
}

void psxBios_rand(HLE_BIOS_CALL_ARGS) { // 0x2f
    u32 s = psxMu32(0x9010) * 1103515245 + 12345;
    v0 = (s >> 16) & 0x7fff;
    StoreToLE(psxMu32ref(0x9010), s);
    pc0 = ra;
}

void psxBios_srand(HLE_BIOS_CALL_ARGS) { // 0x30
    StoreToLE(psxMu32ref(0x9010), a0);
    pc0 = ra;
}

static u32 qscmpfunc, qswidth;

static inline int qscmp(char *a, char *b) {
    u32 sa0 = a0;

    a0 = sa0 + (a - (char *)PSXM(sa0));
    a1 = sa0 + (b - (char *)PSXM(sa0));

    softCall2(qscmpfunc);

    a0 = sa0;
    return (s32)v0;
}

static inline void qexchange(char *i, char *j) {
    char t;
    int n = qswidth;

    do {
        t = *i;
        *i++ = *j;
        *j++ = t;
    } while (--n);
}

static inline void q3exchange(char *i, char *j, char *k) {
    char t;
    int n = qswidth;

    do {
        t = *i;
        *i++ = *k;
        *k++ = *j;
        *j++ = t;
    } while (--n);
}

static void qsort_main(char *a, char *l) {
    char *i, *j, *lp, *hp;
    int c;
    unsigned int n;

start:
    if ((n = l - a) <= qswidth)
        return;
    n = qswidth * (n / (2 * qswidth));
    hp = lp = a + n;
    i = a;
    j = l - qswidth;
    while (1) {
        if (i < lp) {
            if ((c = qscmp(i, lp)) == 0) {
                qexchange(i, lp -= qswidth);
                continue;
            }
            if (c < 0) {
                i += qswidth;
                continue;
            }
        }

loop:
        if (j > hp) {
            if ((c = qscmp(hp, j)) == 0) {
                qexchange(hp += qswidth, j);
                goto loop;
            }
            if (c > 0) {
                if (i == lp) {
                    q3exchange(i, hp += qswidth, j);
                    i = lp += qswidth;
                    goto loop;
                }
                qexchange(i, j);
                j -= qswidth;
                i += qswidth;
                continue;
            }
            j -= qswidth;
            goto loop;
        }

        if (i == lp) {
            if (lp - a >= l - hp) {
                qsort_main(hp + qswidth, l);
                l = lp;
            } else {
                qsort_main(a, lp);
                a = hp + qswidth;
            }
            goto start;
        }

        q3exchange(j, lp -= qswidth, i);
        j = hp -= qswidth;
    }
}

void psxBios_qsort(HLE_BIOS_CALL_ARGS) { // 0x31
    qswidth = a2;
    qscmpfunc = a3;
    qsort_main((char *)Ra0, (char *)Ra0 + a1 * a2);

    pc0 = ra;
}

#if HLE_ENABLE_HEAP
void psxBios_malloc(HLE_BIOS_CALL_ARGS) { // 0x33
    unsigned int *chunk, *newchunk = NULL;
    unsigned int dsize = 0, csize, cstat;
    int colflag;
    PSXBIOS_LOG("psxBios_%s\n", biosA0n[0x33]);

    if (!a0 || (!heap_size || !heap_addr)) {
        v0 = 0;
        pc0 = ra;
        return;
    }

    // scan through heap and combine free chunks of space
    chunk = heap_addr;
    colflag = 0;
    while(chunk < heap_end) {
        // get size and status of actual chunk
        csize = ((u32)*chunk) & 0xfffffffc;
        cstat = ((u32)*chunk) & 1;

        // most probably broken heap descriptor
        // this fixes Burning Road
        if (*chunk == 0) {
            newchunk = chunk;
            dsize = ((sptr)heap_end - (sptr)chunk) - 4;
            colflag = 1;
            break;
        }

        // it's a free chunk
        if(cstat == 1) {
            if(colflag == 0) {
                newchunk = chunk;
                dsize = csize;
                colflag = 1;			// let's begin a new collection of free memory
            }
            else dsize += (csize+4);	// add the new size including header
        }
        // not a free chunk: did we start a collection ?
        else {
            if(colflag == 1) {			// collection is over
                colflag = 0;
                StoreToLE(*newchunk, dsize | 1);
            }
        }

        // next chunk
        chunk = (u32*)((uptr)chunk + csize + 4);
    }
    // if neccessary free memory on end of heap
    if (colflag == 1)
        StoreToLE(*newchunk, dsize | 1);

    chunk = heap_addr;
    csize = ((u32)*chunk) & 0xfffffffc;
    cstat = ((u32)*chunk) & 1;
    dsize = (a0 + 3) & 0xfffffffc;

    // exit on uninitialized heap
    if (chunk == NULL) {
        printf("malloc %x,%x: Uninitialized Heap!\n", v0, a0);
        v0 = 0;
        pc0 = ra;
        return;
    }

    // search an unused chunk that is big enough until the end of the heap
    while ((dsize > csize || cstat==0) && chunk < heap_end ) {
        chunk = (u32*)((sptr)chunk + csize + 4);

            // catch out of memory
            if(chunk >= heap_end) {
                printf("malloc %x,%x: Out of memory error!\n",
                    v0, a0);
                v0 = 0; pc0 = ra;
                return;
            }

        csize = ((u32)*chunk) & 0xfffffffc;
        cstat = ((u32)*chunk) & 1;
    }

    // allocate memory
    if(dsize == csize) {
        // chunk has same size
        *chunk &= 0xfffffffc;
    } else if (dsize > csize) {
        v0 = 0; pc0 = ra;
        return;
    } else {
        // split free chunk
        StoreToLE(*chunk, dsize);
        newchunk = (u32*)((sptr)chunk + dsize + 4);
        StoreToLE(*newchunk, ((csize - dsize - 4) & 0xfffffffc) | 1);
    }

    // return pointer to allocated memory
    v0 = ((sptr)chunk - (sptr)PSX_RAM_START) + 4;
    v0|= 0x80000000;
    SysPrintf("malloc %x, size %x\n", v0, a0);
    pc0 = ra;
}

void psxBios_free(HLE_BIOS_CALL_ARGS) { // 0x34

    PSXBIOS_LOG("psxBios_%s\n", biosA0n[0x34]);

    SysPrintf("free %x: %x bytes\n", a0, *(u32*)(Ra0-4));

    if (a0) {
        *(u32*)(Ra0-4) |= 1;	// set chunk to free
    }
    pc0 = ra;
}

void psxBios_calloc(HLE_BIOS_CALL_ARGS) { // 0x37
    void *pv0;
    PSXBIOS_LOG("psxBios_%s\n", biosA0n[0x37]);

    a0 = a0 * a1;
    psxBios_malloc(HLE_BIOS_INVOKE_ARGS);
    pv0 = Rv0;
    if (pv0)
        memset(pv0, 0, a0);
}

void psxBios_realloc(HLE_BIOS_CALL_ARGS) { // 0x38
    u32 block = a0;
    u32 size = a1;

    PSXBIOS_LOG("psxBios_%s\n", biosA0n[0x38]);

    a0 = block;
    /* If "old_buf" is zero, executes malloc(new_size), and returns r2=new_buf (or 0=failed). */
    if (block == 0)
    {
        psxBios_malloc(HLE_BIOS_INVOKE_ARGS);
    }
    /* Else, if "new_size" is zero, executes free(old_buf), and returns r2=garbage. */
    else if (size == 0)
    {
        psxBios_free(HLE_BIOS_INVOKE_ARGS);
    }
    /* Else, executes malloc(new_size), bcopy(old_buf,new_buf,new_size), and free(old_buf), and returns r2=new_buf (or 0=failed). */
    /* Note that it is not quite implemented this way here. */
    else
    {
        psxBios_free(HLE_BIOS_INVOKE_ARGS);
        a0 = size;
        psxBios_malloc(HLE_BIOS_INVOKE_ARGS);
    }
}


/* InitHeap(void *block , int n) */
void psxBios_InitHeap(HLE_BIOS_CALL_ARGS) { // 0x39
    unsigned int size;

    PSXBIOS_LOG("psxBios_%s\n", biosA0n[0x39]);

    if (((a0 & 0x1fffff) + a1)>= 0x200000) size = 0x1ffffc - (a0 & 0x1fffff);
    else size = a1;

    size &= 0xfffffffc;

    heap_addr = (u32 *)Ra0;
    heap_size = size;
    heap_end = (u32 *)((u8 *)heap_addr + heap_size);
    /* HACKFIX: Commenting out this line fixes GTA2 crash */
    //StoreToLE(*heap_addr, size | 1);

    SysPrintf("InitHeap %x,%x : %x %x\n",a0,a1, (int)((uptr)heap_addr-(uptr)PSX_RAM_START), size);

    pc0 = ra;
}
#endif

// stdoutbuf is not strictly necessary - we chould use native putc instead.
// But it can be helpful as a rule, especially if the emulator becomes threaded/asyncronous later.
// It keeps PSX stdout from corrupting logging coming from other threads.
//
// currently not handled by savesatate but also only user-facing (won't affect state determininism)
// Recommended savestate behavior is to simply ensure this is initialized to 0.
static std::string stdoutbuf;

static void raw_putc(char c) {
    if (c == '\n') {
        if (!stdoutbuf.empty()) {
            SysPrintf("STDOUT: %s\n", stdoutbuf.c_str());
        }

        stdoutbuf.clear();
    }
    else {
        if (c != '\r') {
            stdoutbuf += c;
        }
    }
}

static void raw_puts(const char* cstr) {
    if (!cstr) return;

    if (!cstr[0]) {
        // game putting out an intentionally-blank line
        SysPrintf("STDOUT: \n");
        return;
    }
    for(const char* eol = cstr; eol[0]; ++eol) {
        if (eol[0] == '\r') {
            // ignore 'em
        }
        else if (eol[0] == '\n') {
            SysPrintf("STDOUT: %s\n", stdoutbuf.c_str());
            stdoutbuf.clear();
        }
        else {
            stdoutbuf += eol[0];
        }
    }
}

void psxBios_getchar(HLE_BIOS_CALL_ARGS) { //0x3b
    PSXBIOS_LOG_SPAM("getchar");
    v0 = getchar();
    pc0 = ra;
}

void psxBios_putchar(HLE_BIOS_CALL_ARGS) { // 3d
    PSXBIOS_LOG_SPAM("putchar");
    raw_putc(a0);
    pc0 = ra;
}

void psxBios_puts(HLE_BIOS_CALL_ARGS) { // 3e/3f
    PSXBIOS_LOG_SPAM("puts");
    raw_puts(Ra0);
    pc0 = ra;
}

void psxBios_printf(HLE_BIOS_CALL_ARGS) { // 0x3f
    PSXBIOS_LOG_NEW("printf");
    const int t2len = 64;
    char tmp2[t2len];
    char ptmp[512];      // FIXME: remove this and replace with StringUtila nd format directly into std string.
    u32 save[4];
    int n=1, i=0, j;
    void *psp;

    psp = PSXM(sp);
    if (psp) {
        memcpy(save, psp, 4 * 4);
        StoreToLE(psxMu32ref(sp + 0 ), a0);
        StoreToLE(psxMu32ref(sp + 4 ), a1);
        StoreToLE(psxMu32ref(sp + 8 ), a2);
        StoreToLE(psxMu32ref(sp + 12), a3);
    }

    const char* fmt = Ra0;

    while (fmt[i]) {
        if (fmt[i] == '%') {
            if (fmt[i+1] == '%') {
                raw_putc('%');
                ++i; continue;
            }

            j = 0;
            tmp2[j++] = '%';
_start:
            // safeguiard - if things run past the end of the buffer, give up.
            if (j > t2len-2) {
                tmp2[j] = 0;
                //raw_puts(tmp2);
                fprintf(stderr, "Funky printf formatting at 0x%06x, msg=%s\n", a0 & USEG_MASK, fmt);
                continue;   // resume processing.
            }

            switch (fmt[++i]) {
                case '.':
                case 'l': {
                    tmp2[j++] = fmt[i];
                } goto _start;

                default: {
                    if (fmt[i] >= '0' && fmt[i] <= '9') {
                        tmp2[j++] = fmt[i];
                        goto _start;
                    }
                } break;
            }
            tmp2[j++] = fmt[i];
            tmp2[j] = 0;

            switch (fmt[i]) {
                case 'f': case 'F':
                    snprintf(ptmp, sizeof(ptmp), tmp2, (float)psxMu32(sp + n * 4)); n++; 
                    raw_puts(ptmp);
                break;

                case 'a': case 'A':
                case 'e': case 'E':
                case 'g': case 'G':
                    snprintf(ptmp, sizeof(ptmp), tmp2, (double)psxMu32(sp + n * 4)); n++;
                    raw_puts(ptmp);
                break;

                case 'p':
                case 'i': case 'u':
                case 'd': case 'D':
                case 'o': case 'O':
                case 'x': case 'X':
                    snprintf(ptmp, sizeof(ptmp), tmp2, (unsigned int)psxMu32(sp + n * 4)); n++;
                    raw_puts(ptmp);
                break;

                case 'c':
                    raw_putc(psxMu32(sp + n * 4)); n++;
                break;

                case 's':
                    raw_puts((char*)PSXM(psxMu32(sp + n * 4))); n++;
                break;

                case '%':
                    fprintf(stderr, "Funky printf formatting at 0x%06x, msg=%s\n", a0 & USEG_MASK, fmt);
                break;
            }
            i++;
        }
        else {
            raw_putc(fmt[i++]);
        }
    }

    if (psp)
        memcpy(psp, save, 4 * 4);

    pc0 = ra;
}

#if HLE_ENABLE_MCD
void psxBios_format(HLE_BIOS_CALL_ARGS) { // 0x41
    PSXBIOS_LOG_NEW("format");
    // TO BE tested on PCSX
    if (strcmp(Ra0, "bu00:") == 0 && /*Config.Mcd1[0] != '\0'*/ VmcEnabled(0))
    {
        VmcCreate(0);
        v0 = 1;
    }
    else if (strcmp(Ra0, "bu10:") == 0 && /*Config.Mcd2[0] != '\0'*/ VmcEnabled(1))
    {
        VmcCreate(1);
        v0 = 1;
    }
    else
    {
        v0 = 0;
    }
    pc0 = ra;
}
#endif

/*
 *	int Exec(struct EXEC *header , int argc , char **argv);
 */

void psxBios_Exec(HLE_BIOS_CALL_ARGS) { // 43
    auto header = (EXEC_DESCRIPTOR*)Ra0;
    u32 tmp;

    PSXBIOS_LOG("psxBios_%s: %x, %x, %x\n", biosA0n[0x43], a0, a1, a2);

    header->SavedSP = sp;
    header->SavedFP = fp;
    header->SavedSP = sp;
    header->SavedGP = gp;
    header->SavedRA = ra;
    header->SavedS0 = s0;

    if (header->s_addr != 0) {
        tmp = header->s_addr + header->s_size;
        sp = tmp;
        fp = sp;
    }

    gp = header->_gp;

    s0 = a0;

    a0 = a1;
    a1 = a2;

    ra = 0x8000;
    pc0 = header->_pc;
}

// It isn't a bios call but executable might exit and replaced with another one
static void psxBios_ExecRet() {
    auto header = (EXEC_DESCRIPTOR*)PSXM(s0);

    PSXBIOS_LOG("psxBios_ExecRet %x: %x\n", s0, header->SavedRA);

    ra = header->SavedRA;
    sp = header->SavedSP;
    fp = header->SavedFP;
    gp = header->SavedGP;
    s0 = header->SavedS0;

    v0 = 1;
    pc0 = ra;
}

#if HLE_ENABLE_LOADEXEC
extern void         psxFs_CacheFilesystem();
extern bool         psxFs_LoadExecutableHeader(const char* path, PSX_EXE_HEADER& dest);
extern psdisc_sec_t psxFs_GetFileSector(const char* path);
extern intmax_t     psxFs_GetFileSize(const char* path);
extern bool         psxFs_ReadSectorData2048(void* dest,  psdisc_sec_t sector, int nSectors=1);

void psxBios_Load(HLE_BIOS_CALL_ARGS) { // 0x42
    PSXBIOS_LOG("psxBios_%s: %s, %x\n", biosA0n[0x42], Ra0, a1);

    static_assert((sizeof(EXEC_DESCRIPTOR) + sizeof(PSX_EXE_HEADER)) == 76);

    if (auto sector = psxFs_GetFileSector(Ra0)) {
        uint8_t buf[2048];
        psxFs_ReadSectorData2048(buf, sector);
        memcpy(Ra0, buf, 76);
        EXEC_DESCRIPTOR tdesc = *(EXEC_DESCRIPTOR*)(Ra0 + sizeof(PSX_EXE_HEADER));

        if (auto* pa1 = (EXEC_DESCRIPTOR*)Ra1) {
            *pa1 = tdesc;
        }

        for(size_t i=0; i<sizeof(tdesc) / 4; ++i) {
            auto* val = (int32_t*)&tdesc + i;
            StoreToLE(*val, *val);
        }

        intmax_t text_addr = tdesc.t_addr;
        intmax_t text_size = tdesc.t_size;
        psxFs_ReadSectorData2048(PSXM(text_addr), sector+1, (text_size + 2047) / 2048);

        v0 = 1;
    }
    else {
        v0 = 0;
    }

    pc0 = ra;
}

void psxBios_LoadExec(HLE_BIOS_CALL_ARGS) { // 51
    auto header = (EXEC_DESCRIPTOR*)PSXM(0xf000);
    u32 s_addr, s_size;

    PSXBIOS_LOG("psxBios_%s: %s: %x,%x\n", biosA0n[0x51], Ra0, a1, a2);
    s_addr = a1; s_size = a2;

    a1 = 0xf000;
    psxBios_Load(HLE_BIOS_INVOKE_ARGS);

    header->s_addr = s_addr;
    header->s_size = s_size;

    a0 = 0xf000; a1 = 0; a2 = 0;
    psxBios_Exec(HLE_BIOS_INVOKE_ARGS);
}
#endif

void psxBios_FlushCache(HLE_BIOS_CALL_ARGS) { // 44
    PSXBIOS_LOG("psxBios_%s\n", biosA0n[0x44]);

    pc0 = ra;
}


#if HLE_PCSX_IFC
#define GPU_W_DATA(dat)     GPU_writeData  (dat)
#define GPU_W_STATUS(dat)   GPU_writeStatus(dat)
#define GPU_R_STATUS()      GPU_readStatus ()
#define DMA_W(addr, val)    psxHwWrite32(addr, val)
#define DMA_R(addr)         psxHwRead32 (addr)
#endif

#if HLE_MEDNAFEN_IFC
#define GPU_W_DATA(dat)     GPU_Write(0, 0, dat)
#define GPU_W_STATUS(dat)   GPU_Write(0, 4, dat)
#define GPU_R_STATUS()      GPU_Read(0, 4)
#define DMA_W(addr, val)    DMA_Write(0, addr, val)
#define DMA_R(addr)         DMA_Read (0, addr)
#endif

#if HLE_DUCKSTATION_IFC
#define GPU_W_DATA(dat)     (g_gpu->WriteRegister(0, dat))
#define GPU_W_STATUS(dat)   (g_gpu->WriteRegister(4, dat))
#define GPU_R_STATUS()      (g_gpu->ReadRegister (4))
#define DMA_W(addr, val)    (g_dma.WriteRegister((addr) & Bus::DMA_MASK, val))
#define DMA_R(addr)         (g_dma.ReadRegister ((addr) & Bus::DMA_MASK))
#endif

#if HLE_ENABLE_GPU
void psxBios_GPU_dw(HLE_BIOS_CALL_ARGS) { // 0x46
    int size;
    s32 *ptr;

    PSXBIOS_LOG("psxBios_%s\n", biosA0n[0x46]);

    GPU_W_DATA(0xa0000000);
    GPU_W_DATA((a1<<16)|(a0&0xffff));
    GPU_W_DATA((a3<<16)|(a2&0xffff));
    size = (a2*a3+1)/2;
    ptr = (s32*)PSXM(Rsp[4]);  //that is correct?
    do {
        GPU_W_DATA(LoadFromLE(*ptr));
        ptr++;
    } while(--size);

    pc0 = ra;
}

void psxBios_mem2vram(HLE_BIOS_CALL_ARGS) { // 0x47
    int size;

    GPU_W_DATA(0xa0000000);
    GPU_W_DATA((a1<<16)|(a0&0xffff));
    GPU_W_DATA((a3<<16)|(a2&0xffff));
    size = (a2*a3+1)/2;
    GPU_W_STATUS(0x04000002);

    DMA_W(0x1f8010f4,0);
    DMA_W(0x1f8010f0, DMA_R(0x1f8010f0)|0x800);
    DMA_W(0x1f8010a0, Rsp[4]);//might have a buggy...
    DMA_W(0x1f8010a4, ((size/16)<<16)|16);
    DMA_W(0x1f8010a8, 0x01000201);

    pc0 = ra;
}

void psxBios_SendGPU(HLE_BIOS_CALL_ARGS) { // 0x48
    GPU_W_STATUS(a0);
    //gpuSyncPluginSR();
    pc0 = ra;
}

void psxBios_GPU_cw(HLE_BIOS_CALL_ARGS) { // 0x49
    GPU_W_DATA(a0);
    pc0 = ra;
}

void psxBios_GPU_cwb(HLE_BIOS_CALL_ARGS) { // 0x4a
    s32 *ptr = (s32*)Ra0;
    int size = a1;
    while(size--) {
        GPU_W_DATA(LoadFromLE(*ptr));
        ptr++;
    }

    pc0 = ra;
}

void psxBios_GPU_SendPackets(HLE_BIOS_CALL_ARGS) { // 4b
    GPU_W_STATUS(0x04000002);
    DMA_W(0x1f8010f4, 0);
    DMA_W(0x1f8010f0, DMA_R(0x1f8010f0)|0x800);
    DMA_W(0x1f8010a0, a0);
    DMA_W(0x1f8010a4, 0);
    DMA_W(0x1f8010a8, 0x010000401);
    pc0 = ra;
}

void psxBios_sys_a0_4c(HLE_BIOS_CALL_ARGS) { // 0x4c GPU relate
    DMA_W(0x1f8010a8,0x00000401);
    GPU_W_DATA(0x0400000);
    GPU_W_DATA(0x0200000);
    GPU_W_DATA(0x0100000);
    v0 = 0x1f801814;

    pc0 = ra;
}

void psxBios_GPU_GetGPUStatus(HLE_BIOS_CALL_ARGS) { // 0x4d
    v0 = GPU_R_STATUS();
    pc0 = ra;
}
#endif

/* TODO FIXME : Not compliant. -1 indicates failure but using 1 for now. */
void psxBios_get_cd_status(HLE_BIOS_CALL_ARGS) //a6
{
    v0 = 1;
    pc0 = ra;
}

#if HLE_ENABLE_EVENT
void psxBios__bu_init(HLE_BIOS_CALL_ARGS) { // 70
    PSXBIOS_LOG("psxBios_%s\n", biosA0n[0x70]);

#if HLE_ENABLE_YIELD
    // Impl Note: honestly this would be easier rewritten as MIPS assembly.
    //   It's just two JALs, but in order to maintain stack-engine state info at the HLE 
    //   level, we need to implement a ton of paperwork that the interpreter would simply
    //   handle for us via its own MIPS state machine. --jstine

    int baseState = SCRI_psxBios__bu_init_00;
    DeliverEvent(baseState++, 0x11, 0x2); // 0xf0000011, 0x0004
    DeliverEvent(baseState++, 0x81, 0x2); // 0xf4000001, 0x0004

#else
    DeliverEvent(0x11, 0x2); // 0xf0000011, 0x0004
    DeliverEvent(0x81, 0x2); // 0xf4000001, 0x0004

    pc0 = ra;
#endif
}
#endif

void psxBios__96_init(HLE_BIOS_CALL_ARGS) { // 71
    PSXBIOS_LOG("psxBios_%s\n", biosA0n[0x71]);

    pc0 = ra;
}

void psxBios__96_remove(HLE_BIOS_CALL_ARGS) { // 72
    PSXBIOS_LOG("psxBios_%s\n", biosA0n[0x72]);

    pc0 = ra;
}

void psxBios_SetMem(HLE_BIOS_CALL_ARGS) { // 9f
    u32 nx = Read_MEMCTRL2();

    PSXBIOS_LOG("psxBios_%s: %x, %x\n", biosA0n[0x9f], a0, a1);

    switch(a0) {
        case 2:
            Write_MEMCTRL2(nx);
            psxMu32ref(0x060) = a0;
            SysPrintf("Change effective memory : %d MBytes\n",a0);
            break;

        case 8:
            Write_MEMCTRL2(nx | 0x300);
            psxMu32ref(0x060) = a0;
            SysPrintf("Change effective memory : %d MBytes\n",a0);
    
        default:
            SysPrintf("Effective memory must be 2/8 MBytes\n");
        break;
    }

    pc0 = ra;
}

#if HLE_ENABLE_MCD
void psxBios__card_info(HLE_BIOS_CALL_ARGS) { // ab
    PSXBIOS_LOG("psxBios_%s: %x\n", biosA0n[0xab], a0);

    u32 ret, port;
    card_active_chan = a0;
    port = card_active_chan >> 4;

    switch (port) {
    case 0x0:
    case 0x1:
        ret = 0x2;
        if (!VmcEnabled(port & 1))
            ret = 0x8;
        break;
    default:
        PSXBIOS_LOG("psxBios_%s: UNKNOWN PORT 0x%x\n", biosA0n[0xab], card_active_chan);
        ret = 0x11;
        break;
    }

    if (!VmcEnabled(0) && !VmcEnabled(1))
        ret = 0x8;

    DeliverEvent(0x11, 0x2); // 0xf0000011, 0x0004
//	DeliverEvent(0x81, 0x2); // 0xf4000001, 0x0004
    DeliverEvent(0x81, ret); // 0xf4000001, 0x0004
    v0 = 1; pc0 = ra;
}

void psxBios__card_load(HLE_BIOS_CALL_ARGS) { // ac
    PSXBIOS_LOG("psxBios_%s: %x\n", biosA0n[0xac], a0);

    card_active_chan = a0;

//	DeliverEvent(0x11, 0x2); // 0xf0000011, 0x0004
    DeliverEvent(0x81, 0x2); // 0xf4000001, 0x0004

    v0 = 1; pc0 = ra;
}

void psxBios__card_auto(HLE_BIOS_CALL_ARGS) { // 0xAD
    PSXBIOS_LOG("psxBios_%s\n", biosA0n[0xAD]);

    pc0 = ra;
}
#endif

/* System calls B0 */

#if HLE_ENABLE_RCNT
void psxBios_SetRCnt(HLE_BIOS_CALL_ARGS) { // 02
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x02]);

    a0&= 0x3;
    if (a0 != 3) {
        u32 mode=0;

        if (a2&0x1000) mode|= 0x050; // Interrupt Mode
        if (a2&0x0100) mode|= 0x008; // Count to 0xffff
        if (a2&0x0010) mode|= 0x001; // Timer stop mode
        if (a0 == 2) { if (a2&0x0001) mode|= 0x200; } // System Clock mode
        else         { if (a2&0x0001) mode|= 0x100; } // System Clock mode

        RCNT_SetMode (a0, mode);
        RCNT_SetCount(a0, a1);
    }
    pc0 = ra;
}

void psxBios_GetRCnt(HLE_BIOS_CALL_ARGS) { // 03
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x03]);

    a0&= 0x3;
    v0 = 0;
    if (a0 < 3) v0 = RCNT_GetCount(a0);
    pc0 = ra;
}

void psxBios_StartRCnt(HLE_BIOS_CALL_ARGS) { // 04
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x04]);

    a0&= 0x3;

    auto imask = Read_IMASK();
    if (a0 != 3) { imask |= LoadFromLE<u32>((1<<(a0+4))); }
    else         { imask |= LoadFromLE<u32>(0x1); }
    Write_IMASK(imask);

    v0 = 1;
    pc0 = ra;
}

void psxBios_StopRCnt(HLE_BIOS_CALL_ARGS) { // 05
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x05]);

    a0&= 0x3;

    auto imask = Read_IMASK();
    if (a0 != 3) { imask &= ~LoadFromLE<u32>((1<<(a0+4))); }
    else         { imask &= ~LoadFromLE<u32>(0x1); }
    Write_IMASK(imask);

    pc0 = ra;
}

void psxBios_ResetRCnt(HLE_BIOS_CALL_ARGS) { // 06
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x06]);

    a0&= 0x3;
    if (a0 != 3) {
        RCNT_SetCount (a0, 0);
        RCNT_SetMode  (a0, 0);
        RCNT_SetTarget(a0, 0);
    }
    pc0 = ra;
}

void psxBios_ChangeClearRCnt(HLE_BIOS_CALL_ARGS) { // 0a
    u32 *ptr;

    PSXBIOS_LOG("psxBios_%s: %x, %x\n", biosC0n[0x0a], a0, a1);

    ptr = (u32*)PSXM((a0 << 2) + 0x8600);
    v0 = *ptr;
    *ptr = a1;

//	psxRegs.CP0.n.Status|= 0x404;
    pc0 = ra;
}
#endif

#if HLE_ENABLE_EVENT
/* gets ev for use with Event */
#define GetEv() \
    ev = (a0 >> 24) & 0xf; \
    if (ev == 0xf) ev = 0x5; \
    ev*= 32; \
    ev+= a0&0x1f;

/* gets spec for use with EventCB */
#define GetSpec() \
    spec = 0; \
    switch (a1) { \
        case 0x0301: spec = 16; break; \
        case 0x0302: spec = 17; break; \
        default: \
            for (i=0; i<16; i++) if (a1 & (1 << i)) { spec = i; break; } \
            break; \
    }

void psxBios_DeliverEvent(HLE_BIOS_CALL_ARGS) { // 07
    int ev, spec;
    int i;

    GetEv();
    GetSpec();

    PSXBIOS_LOG("psxBios_%s %x,%x\n", biosB0n[0x07], ev, spec);

    DeliverEvent(ev, spec);

    pc0 = ra;
}

void psxBios_OpenEvent(HLE_BIOS_CALL_ARGS) { // 08
    int ev, spec;
    int i;

    GetEv();
    GetSpec();

    PSXBIOS_LOG("psxBios_%s %x,%x (class:%x, spec:%x, mode:%x, func:%x)\n", biosB0n[0x08], ev, spec, a0, a1, a2, a3);

    EventCB[ev][spec].status = EVENT_STATUS_DISABLED;
    EventCB[ev][spec].mode = a2;
    EventCB[ev][spec].fhandler = a3;

    v0 = ev | (spec << 8);
    pc0 = ra;
}

void psxBios_CloseEvent(HLE_BIOS_CALL_ARGS) { // 09
    int ev, spec;

    ev   = a0 & 0xff;
    spec = (a0 >> 8) & 0xff;

    PSXBIOS_LOG("psxBios_%s %x,%x\n", biosB0n[0x09], ev, spec);

    EventCB[ev][spec].status = EVENT_STATUS_FREE;

    v0 = 1; pc0 = ra;
}

void psxBios_WaitEvent(HLE_BIOS_CALL_ARGS) { // 0a
    int ev, spec;

    ev   = a0 & 0xff;
    spec = (a0 >> 8) & 0xff;

    PSXBIOS_LOG("psxBios_%s %x,%x\n", biosB0n[0x0a], ev, spec);

    dbg_check(spec < 32);

    if (EventCB[ev][spec].status == EVENT_STATUS_FREE)
    {
        v0 = 0;
        pc0 = ra;
        return;
    }

    if (EventCB[ev][spec].status == EVENT_STATUS_PENDING)
    {
        /* Callback events (mode=EVENT_MODE_CALLBACK) do never set the ready flag (and thus WaitEvent would hang forever). */
        if (!(EventCB[ev][spec].mode == EVENT_MODE_CALLBACK)) {
            EventCB[ev][spec].status = EVENT_STATUS_ENABLED;
        }
        v0 = 1;
        pc0 = ra;
        return;
    }

    v0 = 0;
    pc0 = ra;
}

void psxBios_TestEvent(HLE_BIOS_CALL_ARGS) { // 0b
    int ev, spec;

    ev   = a0 & 0xff;
    spec = (a0 >> 8) & 0xff;

    dbg_check(spec < 32);

    if (EventCB[ev][spec].status == EVENT_STATUS_PENDING) {
        if (!(EventCB[ev][spec].mode == EVENT_MODE_CALLBACK)) {
            EventCB[ev][spec].status = EVENT_STATUS_ENABLED;
        }
        v0 = 1;
    }
    else {
        v0 = 0;
    }

    // Print only TestEvent change. The spamy part is the polling of the result
    //PSXBIOS_LOG_SPAM("TestEvent", "psxBios_%s %x,%x: result=%x\n", biosB0n[0x0b], ev, spec, v0);
    int key = a0 & 0xFFFF;
    if (s_debug_ev[key] != v0) {
        s_debug_ev[key] = v0;
        PSXBIOS_LOG("psxBios_%s %x,%x: result=%x\n", biosB0n[0x0b], ev, spec, v0);
    }

    pc0 = ra;
}

void psxBios_EnableEvent(HLE_BIOS_CALL_ARGS) { // 0c
    int ev, spec;

    ev   = a0 & 0xff;
    spec = (a0 >> 8) & 0xff;

    PSXBIOS_LOG("psxBios_%s %x,%x\n", biosB0n[0x0c], ev, spec);

    EventCB[ev][spec].status = EVENT_STATUS_ENABLED;

    v0 = 1; pc0 = ra;
}

void psxBios_DisableEvent(HLE_BIOS_CALL_ARGS) { // 0d
    int ev, spec;

    ev   = a0 & 0xff;
    spec = (a0 >> 8) & 0xff;

    PSXBIOS_LOG("psxBios_%s %x,%x\n", biosB0n[0x0d], ev, spec);

    EventCB[ev][spec].status = EVENT_STATUS_DISABLED;

    v0 = 1; pc0 = ra;
}
#endif

#if HLE_ENABLE_THREAD
static void initThread() {
    // hardcore implementation of threads. It matches PSX ABI to please "KKND Krossfire"
    // that got the wonderful idea to update directly kernel global variable to switch
    // between thread

    // Setup Global pointer to process and thread blocks
    StoreToLE(psxMu32ref(G_PROCESS), KERNEL_PCB | KSEG);
    StoreToLE(psxMu32ref(G_THREADS), KERNEL_TCB | KSEG);

    // Fill the process control block. Basically a pointer to current thread (so TCB slot 0)
    StoreToLE(psxMu32ref(KERNEL_PCB), KERNEL_TCB | KSEG); // store pointer to process control block
    // Fill the thread control block. Basically set RESERVED/FREE on threads
    memset(PSXM(KERNEL_TCB), 0, SIZEOF_TCB * TCB_MAX_THREADS);
    StoreToLE(psxMu32ref(KERNEL_TCB), TCB_THREAD_RESERVED);
    for (auto i = 1u; i < TCB_MAX_THREADS; i++) {
        StoreToLE(psxMu32ref(KERNEL_TCB + i * SIZEOF_TCB), TCB_THREAD_FREE);
    }
}

// It would be doable to save registers into a C structure, however
// using the PSX memory yield 2 advantages
// 1/ Compatible with games that access register. Typically KNND writes the CP0_STATUS...
// 2/ It is directly compatible with savestates as ram is savestat-ed
static void saveContext(u32 tcb) {
    auto context = (u32*)PSXM(tcb & USEG_MASK);
    for (auto i = 0u; i < 32; i++) {
        StoreToLE(context[i + 2], GPR_ARRAY[i]);
    }
    StoreToLE(context[2 + 32 + 0], ra);
    StoreToLE(context[2 + 32 + 1], hi);
    StoreToLE(context[2 + 32 + 2], lo);
    // NOTE: thread switch shall be done in a syscall to be safe. We don't need the syscall
    // for HLE. But we do need to update the CP0_STATUS bits accordingly
    CP0_EXCEPTION();
    StoreToLE(context[2 + 32 + 3], CP0_STATUS);
    StoreToLE(context[2 + 32 + 4], CP0_CAUSE);
}

static void restoreContext(u32 tcb) {
    auto context = (u32*)PSXM(tcb & USEG_MASK);
    for (auto i = 0u; i < 32; i++) {
        GPR_ARRAY[i] = LoadFromLE(context[i + 2]);
    }
    pc0 = LoadFromLE(context[2 + 32 + 0]);
    hi = LoadFromLE(context[2 + 32 + 1]);
    lo = LoadFromLE(context[2 + 32 + 2]);
    CP0_STATUS = LoadFromLE(context[2 + 32 + 3]);
    // NOTE: thread switch shall be done in a syscall to be safe. We don't need the syscall
    // for HLE. But we do need to update the CP0_STATUS bits accordingly
    // Test: "KKND Krossfire" will write the TCB CP0 sr reg manually.
    CP0_RFE();
    CP0_CAUSE = LoadFromLE(context[2 + 32 + 4]);
}

/*
 *	long OpenTh(long (*func)(), unsigned long sp, unsigned long gp);
 */

void psxBios_OpenTh(HLE_BIOS_CALL_ARGS) { // 0e
    // Get pointer of the TCB
    u32 tcb = LoadFromLE(psxMu32ref(G_THREADS));
    // Search an available thread
    u32 th = 0;
    for (; th < TCB_MAX_THREADS; th++, tcb += SIZEOF_TCB) {
        if (LoadFromLE(psxMu32ref(tcb)) != TCB_THREAD_FREE) break;
    }

    if (th == TCB_MAX_THREADS) {
        // Feb 2019 - Added out-of-bounds fix caught by cppcheck:
        // When no free TCB is found, return 0xffffffff according to Nocash doc.
        PSXBIOS_LOG("psxBios_OpenTh() WARNING! No Free TCBs found!\n");
        v0 = 0xffffffff;
        pc0 = ra;
        return;
    }
    PSXBIOS_LOG("psxBios_%s: 0x%x (func:0x%x)\n", biosB0n[0x0e], tcb, a0);

    // Reserve the thread
    auto context = (u32*)PSXM(tcb & USEG_MASK);
    StoreToLE(context[0], TCB_THREAD_RESERVED);
    // Update register info of the thread
    StoreToLE(context[2 + 28], a2);     // GP
    StoreToLE(context[2 + 29], a1);     // SP
    StoreToLE(context[2 + 30], a1);     // FP
    StoreToLE(context[2 + 32 + 0], a0); // PC

    v0 = th | 0xFF00'0000;
    pc0 = ra;
}

/*
 *	int CloseTh(long thread);
 */

void psxBios_CloseTh(HLE_BIOS_CALL_ARGS) { // 0f
    int th = a0 & 0xff;
    dbg_check((u32)th < TCB_MAX_THREADS);

    // Get pointer of the TCB
    u32 tcb = LoadFromLE(psxMu32ref(G_THREADS)) + th * SIZEOF_TCB;
    // And mark current thread as free
    StoreToLE(psxMu32ref(tcb), TCB_THREAD_FREE);

    PSXBIOS_LOG("psxBios_%s: 0x%x\n", biosB0n[0x0f], tcb);

    /* The return value is always 1 (even if the handle was already closed). */
    v0 = 1;
    pc0 = ra;
}

/*
 *	int ChangeTh(long thread);
 */

void psxBios_ChangeTh(HLE_BIOS_CALL_ARGS) { // 10
    int th = a0 & 0xff;
    dbg_check((u32)th < TCB_MAX_THREADS);

    /* The return value is always 1. */
    v0 = 1;

    // Get current thread to save context
    u32 pcb = LoadFromLE(psxMu32ref(G_PROCESS));
    u32 tcb_current = LoadFromLE(psxMu32ref(pcb));
    saveContext(tcb_current);

    // Get to the new thread (will set pc0)
    u32 tcb = LoadFromLE(psxMu32ref(G_THREADS)) + th * SIZEOF_TCB;
    restoreContext(tcb);

    // Save the new current thread
    psxMu32ref(pcb) = tcb | KSEG;

    PSXBIOS_LOG("psxBios_%s: from %x to %x\n", biosB0n[0x10], tcb_current, tcb);
}
#endif

#if HLE_ENABLE_PAD
void psxBios_InitPAD(HLE_BIOS_CALL_ARGS) { // 0x12
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x12]);

    pad_buf1 = (u8*)Ra0;
    pad_buf1len = a1;
    pad_buf2 = (u8*)Ra2;
    pad_buf2len = a3;

    v0 = 1; pc0 = ra;
}

void psxBios_StartPAD(HLE_BIOS_CALL_ARGS) { // 13
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x13]);

    pad_stopped = 0;
    Write_IMASK((unsigned short)(Read_IMASK() | 0x1));
    CP0_STATUS |= 0x401;
    pc0 = ra;
}

void psxBios_StopPAD(HLE_BIOS_CALL_ARGS) { // 14
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x14]);

    pad_stopped = 1;
    pad_buf1 = NULL;
    pad_buf2 = NULL;
    pc0 = ra;
}

void psxBios_PAD_init(HLE_BIOS_CALL_ARGS) { // 15
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x15]);

    if (!(a0 == 0x20000000 || a0 == 0x20000001))
    {
        v0 = 0;
        pc0 = ra;
        return;
    }

    Write_IMASK((u16)(Read_IMASK() | 0x1));
    pad_buf = (int *)Ra1;
    *pad_buf = -1;
    CP0_STATUS |= 0x401;
    v0 = 2;
    pc0 = ra;
}

void psxBios_PAD_dr(HLE_BIOS_CALL_ARGS) { // 16
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x16]);

    v0 = -1; pc0 = ra;
}

void psxBios_ChangeClearPad(HLE_BIOS_CALL_ARGS) { // 5b
    PSXBIOS_LOG("psxBios_%s: %x\n", biosB0n[0x5b], a0);

    pc0 = ra;
}
#endif

#if HLE_ENABLE_ENTRYINT
static unsigned interrupt_r26 = 0x8004E8B0;
static bool s_saved = 0;

static inline void SaveRegs() {
    dbg_check(!s_saved);
    s_saved = 1;
    memcpy(regs, GPR_ARRAY, sizeof(GPR_ARRAY));
#if HLE_MEDNAFEN_IFC
    regs[33] = lo;
    regs[34] = hi;
#endif

    regs[35] = pc0;

    interrupt_r26 = CP0_EPC;
}

static inline void LoadRegs() {
    dbg_check(s_saved);
    s_saved = 0;
    memcpy(GPR_ARRAY, regs, sizeof(GPR_ARRAY));
#if HLE_MEDNAFEN_IFC
    lo = regs[33];
    hi = regs[34];
#endif
}

void psxBios_ReturnFromException(HLE_BIOS_CALL_ARGS) { // 17
    PSXBIOS_LOG_SPAM("ReturnFromException", "DSlot=%d EPC=%08x\n", ((CP0_CAUSE >> 31) & 1), CP0_EPC);

    LoadRegs();

    pc0 = CP0_EPC;
    k0 = interrupt_r26;

    CP0_RFE();

#if HLE_MEDNAFEN_IFC
    PSX_CPU->RecalcIPCache();
#endif
}

void psxBios_ResetEntryInt(HLE_BIOS_CALL_ARGS) { // 18
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x18]);

    jmp_int = 0;
    pc0 = ra;
}

void psxBios_HookEntryInt(HLE_BIOS_CALL_ARGS) { // 19
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x19]);

    jmp_int = a0;
    pc0 = ra;
}
#endif

#if HLE_ENABLE_EVENT
void psxBios_UnDeliverEvent(HLE_BIOS_CALL_ARGS) { // 0x20
    int ev, spec;
    int i;

    GetEv();
    GetSpec();

    PSXBIOS_LOG("psxBios_%s %x,%x\n", biosB0n[0x20], ev, spec);

    if (EventCB[ev][spec].status == EVENT_STATUS_PENDING && EventCB[ev][spec].mode == EVENT_MODE_NO_CALLBACK)
        EventCB[ev][spec].status = EVENT_STATUS_ENABLED;

    pc0 = ra;
}
#endif

#if HLE_ENABLE_MCD
void buread(void* ra1, int mcd, int length) {
    auto mcdraw = VmcGet(mcd - 1);
    auto& fd = FDesc[1 + mcd];

    SysPrintf("read %d: %x,%x (%s)\n", fd.mcfile, fd.offset, length, mcdraw + 128 * fd.mcfile + 0xa);
    auto* ptr = mcdraw + 8192 * fd.mcfile + fd.offset;

    memcpy(ra1, ptr, length);

    if (fd.mode & 0x8000) {
        DeliverEvent(0x11, 0x2); /* 0xf0000011, 0x0004 */
        DeliverEvent(0x81, 0x2); /* 0xf4000001, 0x0004 */
        v0 = 0;
    }
    else
        v0 = length;

    fd.offset += v0;
}

void buwrite(void* ra1, int mcd, int length) {
    u32 offset =  + 8192 * FDesc[1 + mcd].mcfile + FDesc[1 + mcd].offset;

    SysPrintf("write %d: %x,%x\n", FDesc[1 + mcd].mcfile, FDesc[1 + mcd].offset, length);

    VmcWriteNV(mcd -1, offset, ra1, length);

    FDesc[1 + mcd].offset += length;

    if (FDesc[1 + mcd].mode & 0x8000) {
        DeliverEvent(0x11, 0x2); /* 0xf0000011, 0x0004 */
        DeliverEvent(0x81, 0x2); /* 0xf4000001, 0x0004 */
        v0 = 0;
    }
    else
        v0 = length;
}

char ffile[64], *pfile;
int nfile;

static void buopen(int mcd)
{
    auto mcdraw = VmcGet(mcd - 1);
    int i;
    char *ptr = (char*)mcdraw;
    char *mcd_data = (char*)mcdraw;

    strcpy(FDesc[1 + mcd].name, Ra0+5);
    FDesc[1 + mcd].offset = 0;
    FDesc[1 + mcd].mode   = a1;

    for (i=1; i<16; i++) {
        const char *fptr = mcd_data + 128 * i;
        if ((*fptr & 0xF0) != 0x50) continue;
        if (strcmp(FDesc[1 + mcd].name, fptr+0xa)) continue;
        FDesc[1 + mcd].mcfile = i;
        SysPrintf("open %s\n", fptr+0xa);
        v0 = 1 + mcd;
        break;
    }
    if (a1 & 0x200 && v0 == -1) { /* FCREAT */
        for (i=1; i<16; i++) {
            int j, xorx, nblk = a1 >> 16;
            char *pptr, *fptr2;
            char *fptr = mcd_data + 128 * i;

            if ((*fptr & 0xF0) != 0xa0) continue;

            FDesc[1 + mcd].mcfile = i;
            fptr[0] = 0x51;
            fptr[4] = 0x00;
            fptr[5] = 0x20 * nblk;
            fptr[6] = 0x00;
            fptr[7] = 0x00;
            strcpy(fptr+0xa, FDesc[1 + mcd].name);
            pptr = fptr2 = fptr;
            for(j=2; j<=nblk; j++) {
                int k;
                for(i++; i<16; i++) {
                    fptr2 += 128;

                    memset(fptr2, 0, 128);
                    fptr2[0] = j < nblk ? 0x52 : 0x53;
                    pptr[8] = i - 1;
                    pptr[9] = 0;
                    for (k=0, xorx=0; k<127; k++) xorx^= pptr[k];
                    pptr[127] = xorx;
                    pptr = fptr2;
                    break;
                }
                /* shouldn't this return ENOSPC if i == 16? */
            }
            pptr[8] = pptr[9] = 0xff;
            for (j=0, xorx=0; j<127; j++) xorx^= pptr[j];
            pptr[127] = xorx;
            SysPrintf("openC %s %d\n", ptr, nblk);
            v0 = 1 + mcd;
            /* just go ahead and resave them all */
            VmcDirty(mcd - 1);
            break;
        }
        /* shouldn't this return ENOSPC if i == 16? */
    }
}

/* Internally redirects to "FileRead(fd,tempbuf,1)".*/
/* For some strange reason, the returned character is sign-expanded; */
/* So if a return value of FFFFFFFFh could mean either character FFh, or error. */
/* TODO FIX ME : Properly implement this behaviour */
void psxBios_getc(HLE_BIOS_CALL_ARGS) // 0x03, 0x35
{
    void *pa1 = Ra1;

    PSXBIOS_LOG("psxBios_%s\n", biosA0n[0x03]);

    v0 = -1;

    if (pa1) {
        switch (a0) {
            case 2: buread(pa1, 1, 1); break;
            case 3: buread(pa1, 2, 1); break;
        }
    }

    pc0 = ra;
}

/* Copy of psxBios_write, except size is 1. */
void psxBios_putc(HLE_BIOS_CALL_ARGS) // 0x09, 0x3B
{
    void *pa1 = Ra1;

    PSXBIOS_LOG("psxBios_%s\n", biosA0n[0x09]);
    
    v0 = -1;
    if (!pa1) {
        pc0 = ra;
        return;
    }

    if (a0 == 1) { // stdout
        char *ptr = (char *)pa1;

        v0 = a2;
        while (a2 > 0) {
            raw_putc(*ptr++); a2--;
        }
        pc0 = ra;
        return;
    }

    switch (a0) {
        case 2: buwrite(pa1, 1, 1); break;
        case 3: buwrite(pa1, 2, 1); break;
    }

    pc0 = ra;
}

/*
 *	int open(char *name , int mode);
 */

void psxBios_open(HLE_BIOS_CALL_ARGS) { // 0x32
    auto *pa0 = (char*)Ra0;

    PSXBIOS_LOG("psxBios_%s: %s,%x\n", biosB0n[0x32], Ra0, a1);

    v0 = -1;

    if (pa0) {
        if (!strncmp(pa0, "bu00", 4)) {
            buopen(1);
        }

        if (!strncmp(pa0, "bu10", 4)) {
            buopen(2);
        }
    }

    pc0 = ra;
}

/*
 *	int lseek(int fd , int offset , int whence);
 */

void psxBios_lseek(HLE_BIOS_CALL_ARGS) { // 0x33
    PSXBIOS_LOG("psxBios_%s: %x, %x, %x\n", biosB0n[0x33], a0, a1, a2);

    switch (a2) {
        case 0: // SEEK_SET
            FDesc[a0].offset = a1;
            v0 = a1;
//			DeliverEvent(0x11, 0x2); // 0xf0000011, 0x0004
//			DeliverEvent(0x81, 0x2); // 0xf4000001, 0x0004
            break;

        case 1: // SEEK_CUR
            FDesc[a0].offset+= a1;
            v0 = FDesc[a0].offset;
            break;
    }

    pc0 = ra;
}

/*
 *	int read(int fd , void *buf , int nbytes);
 */

void psxBios_read(HLE_BIOS_CALL_ARGS) { // 0x34
    void *pa1 = Ra1;

    PSXBIOS_LOG("psxBios_%s: %x, %x, %x\n", biosB0n[0x34], a0, a1, a2);

    v0 = -1;

    if (pa1) {
        switch (a0) {
            case 2: buread(pa1, 1, a2); break;
            case 3: buread(pa1, 2, a2); break;
        }
    }

    pc0 = ra;
}

/*
 *	int write(int fd , void *buf , int nbytes);
 */

void psxBios_write(HLE_BIOS_CALL_ARGS) { // 0x35/0x03
    auto *pa1 = Ra1;

    PSXBIOS_LOG("psxBios_%s: %x,%x,%x\n", biosB0n[0x35], a0, a1, a2);

    v0 = -1;
    if (!pa1) {
        pc0 = ra;
        return;
    }

    if (a0 == 1) { // stdout
        char *ptr = pa1;

        v0 = a2;
        while (a2 > 0) {
            raw_putc(*ptr++); a2--;
        }
        pc0 = ra; return;
    }

    switch (a0) {
        case 2: buwrite(pa1, 1, a2); break;
        case 3: buwrite(pa1, 2, a2); break;
    }

    pc0 = ra;
}

/*
 *	int close(int fd);
 */

void psxBios_close(HLE_BIOS_CALL_ARGS) { // 0x36
    PSXBIOS_LOG("psxBios_%s: %x\n", biosB0n[0x36], a0);

    v0 = a0;
    pc0 = ra;
}

/* To avoid any issues with different behaviour when using the libc's own strlen instead.
 * We want to mimic the PSX's behaviour in this case for bufile. */
static size_t strlen_internal(char* p)
{
    size_t size_of_array = 0;
    while (*p++) size_of_array++;
    return size_of_array;
}

static void bufile(int mcd_port) {
    auto mcdraw = VmcGet(mcd_port - 1);
    u32 _dir = a1;
    struct DIRENTRY *dir = (struct DIRENTRY *)Ra1;

    size_t size_of_name = strlen_internal(dir->name);

    while (nfile < 16) {
        int match=1;
        auto* ptr = mcdraw + 128 * (nfile + 1);

        nfile++;
        if ((*ptr & 0xF0) != 0x50) continue;
        /* Bug link files show up as free block. */
        if (!ptr[0xa]) continue;
        ptr+= 0xa;
        if (pfile[0] == 0) {
            strncpy(dir->name, (char*)ptr, sizeof(dir->name) - 1);
            if (size_of_name < sizeof(dir->name)) dir->name[size_of_name] = '\0';
        } else for (int i=0; i<20; i++) {
            if (pfile[i] == ptr[i]) {
                dir->name[i] = ptr[i]; continue;
            }
            if (pfile[i] == '?') {
                dir->name[i] = ptr[i]; continue;
            }
            if (pfile[i] == '*') {
                strcpy(dir->name+i, (char*)ptr+i); break;
            }
            match = 0; break;
        }
        SysPrintf("%d : %s = %s + %s (match=%d)\n", nfile, dir->name, pfile, ptr, match);
        if (match == 0) { continue; }
        dir->size = 8192;
        v0 = _dir;
        break;
    }
}

/*
 *	struct DIRENTRY* firstfile(char *name,struct DIRENTRY *dir);
 */

void psxBios_firstfile(HLE_BIOS_CALL_ARGS) { // 42
    auto *pa0 = Ra0;

    PSXBIOS_LOG("psxBios_%s: %s\n", biosB0n[0x42], Ra0);

    v0 = 0;

    //auto mcd = PSX_FIO->GetMemcardDevice(0);

    if (pa0) {
        strcpy(ffile, pa0);
        pfile = ffile+5;
        nfile = 0;
        if (!strncmp(pa0, "bu00", 4)) {
            // firstfile() calls _card_read() internally, so deliver it's event
            DeliverEvent(0x11, 0x2);
            bufile(1);
        } else if (!strncmp(pa0, "bu10", 4)) {
            // firstfile() calls _card_read() internally, so deliver it's event
            DeliverEvent(0x11, 0x2);
            bufile(2);
        }
    }

    pc0 = ra;
}

/*
 *	struct DIRENTRY* nextfile(struct DIRENTRY *dir);
 */

void psxBios_nextfile(HLE_BIOS_CALL_ARGS) { // 43
    struct DIRENTRY *dir = (struct DIRENTRY *)Ra0;

    PSXBIOS_LOG("psxBios_%s: %s\n", biosB0n[0x43], dir->name);

    v0 = 0;

    if (!strncmp(ffile, "bu00", 4)) {
        bufile(1);
    }

    if (!strncmp(ffile, "bu10", 4)) {
        bufile(2);
    }

    pc0 = ra;
}

static void burename(int mcd) {
    auto mcdraw = VmcGet(mcd - 1);
    for (int i=1; i<16; i++) {
        char* ptr = (char*)(mcdraw + 128 * i);

        if ((*ptr & 0xF0) != 0x50) continue;
        if (strcmp(Ra0+5, ptr+0xa)) continue;

        int namelen = strlen(Ra1+5);
        memcpy(ptr+0xa, Ra1+5, namelen);
        memset(ptr+0xa+namelen, 0, 0x75-namelen);
        SysPrintf("rename %116s\n", ptr+0xa);

        int xorx = 0;
        for (int j=0; j<127; j++) xorx^= ptr[j];
        ptr[127] = xorx;

        VmcDirty(mcd - 1);

        v0 = 1;
        break;
    }
}

/*
 *	int rename(char *old, char *new);
 */

void psxBios_rename(HLE_BIOS_CALL_ARGS) { // 44
    char *pa0 = Ra0;
    char *pa1 = Ra1;

    PSXBIOS_LOG("psxBios_%s: %s,%s\n", biosB0n[0x44], Ra0, Ra1);

    v0 = 0;

    if (pa0 && pa1) {
        if (!strncmp(pa0, "bu00", 4) && !strncmp(pa1, "bu00", 4)) {
            burename(1);
        }

        if (!strncmp(pa0, "bu10", 4) && !strncmp(pa1, "bu10", 4)) {
            burename(2);
        }
    }

    pc0 = ra;
}

static void budelete(int mcd) {
    auto mcdraw = VmcGet(mcd - 1);
    for (int i=1; i<16; i++) {
        char* ptr = (char*)(mcdraw + 128 * i);

        if ((*ptr & 0xF0) != 0x50) continue;
        if (strcmp(Ra0+5, ptr+0xa)) continue;

        *ptr = (*ptr & 0xf) | 0xA0;
        VmcDirty(mcd - 1);
        SysPrintf("delete %s\n", ptr+0xa);
        v0 = 1;
        break;
    }
}

/*
 *	int delete(char *name);
 */

void psxBios_delete(HLE_BIOS_CALL_ARGS) { // 45
    char *pa0 = Ra0;

    PSXBIOS_LOG("psxBios_%s: %s\n", biosB0n[0x45], Ra0);

    v0 = 0;

    if (pa0) {
        if (!strncmp(pa0, "bu00", 4)) {
            budelete(1);
        }

        if (!strncmp(pa0, "bu10", 4)) {
            budelete(2);
        }
    }

    pc0 = ra;
}

void psxBios_InitCARD(HLE_BIOS_CALL_ARGS) { // 4a
    PSXBIOS_LOG("psxBios_%s: %x\n", biosB0n[0x4a], a0);

    CardState = 0;

    pc0 = ra;
}

void psxBios_StartCARD(HLE_BIOS_CALL_ARGS) { // 4b
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x4b]);

    if (CardState == 0) CardState = 1;

    pc0 = ra;
}

void psxBios_StopCARD(HLE_BIOS_CALL_ARGS) { // 4c
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x4c]);

    if (CardState == 1) CardState = 0;

    pc0 = ra;
}

void psxBios__card_write(HLE_BIOS_CALL_ARGS) { // 0x4e
    void *pa2 = Ra2;
    int port;

    PSXBIOS_LOG("psxBios_%s: %x,%x,%x\n", biosB0n[0x4e], a0, a1, a2);
    /*
    Function also accepts sector 400h (a bug).
    But notaz said we shouldn't allow sector 400h because it can corrupt the emulator.
    */
    if (!(a1 <= 0x3FF))
    {
        /* Invalid sectors */
        v0 = 0; pc0 = ra;
        return;
    }
    card_active_chan = a0;
    port = a0 >> 4;

    if (pa2) {
        VmcWriteNV(port, a1 * 128, pa2, 128);
    }

    DeliverEvent(0x11, 0x2); // 0xf0000011, 0x0004
//	DeliverEvent(0x81, 0x2); // 0xf4000001, 0x0004

    v0 = 1; pc0 = ra;
}

void psxBios__card_read(HLE_BIOS_CALL_ARGS) { // 0x4f
    void *pa2 = Ra2;
    int port;

    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x4f]);

    /*
    Function also accepts sector 400h (a bug).
    But notaz said we shouldn't allow sector 400h because it can corrupt the emulator.
    */
    if (!(a1 <= 0x3FF))
    {
        /* Invalid sectors */
        v0 = 0; pc0 = ra;
        return;
    }
    card_active_chan = a0;
    port = a0 >> 4;

    if (pa2) {
        auto mcdraw = VmcGet(port);
        memcpy(pa2, mcdraw + a1 * 128, 128);
    }

    DeliverEvent(0x11, 0x2); // 0xf0000011, 0x0004
//	DeliverEvent(0x81, 0x2); // 0xf4000001, 0x0004

    v0 = 1; pc0 = ra;
}

void psxBios__new_card(HLE_BIOS_CALL_ARGS) { // 0x50
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x50]);

    pc0 = ra;
}

/* According to a user, this allows Final Fantasy Tactics to save/load properly */
void psxBios__get_error(HLE_BIOS_CALL_ARGS) // 55
{
    v0 = 0;
    pc0 = ra;
}

#endif

void psxBios_Krom2RawAdd(HLE_BIOS_CALL_ARGS) { // 0x51
    int i = 0;

    const u32 table_8140[][2] = {
        {0x8140, 0x0000}, {0x8180, 0x0762}, {0x81ad, 0x0cc6}, {0x81b8, 0x0ca8},
        {0x81c0, 0x0f00}, {0x81c8, 0x0d98}, {0x81cf, 0x10c2}, {0x81da, 0x0e6a},
        {0x81e9, 0x13ce}, {0x81f0, 0x102c}, {0x81f8, 0x1590}, {0x81fc, 0x111c},
        {0x81fd, 0x1626}, {0x824f, 0x113a}, {0x8259, 0x20ee}, {0x8260, 0x1266},
        {0x827a, 0x24cc}, {0x8281, 0x1572}, {0x829b, 0x28aa}, {0x829f, 0x187e},
        {0x82f2, 0x32dc}, {0x8340, 0x2238}, {0x837f, 0x4362}, {0x8380, 0x299a},
        {0x8397, 0x4632}, {0x839f, 0x2c4c}, {0x83b7, 0x49f2}, {0x83bf, 0x2f1c},
        {0x83d7, 0x4db2}, {0x8440, 0x31ec}, {0x8461, 0x5dde}, {0x8470, 0x35ca},
        {0x847f, 0x6162}, {0x8480, 0x378c}, {0x8492, 0x639c}, {0x849f, 0x39a8},
        {0xffff, 0}
    };

    const u32 table_889f[][2] = {
        {0x889f, 0x3d68},  {0x8900, 0x40ec},  {0x897f, 0x4fb0},  {0x8a00, 0x56f4},
        {0x8a7f, 0x65b8},  {0x8b00, 0x6cfc},  {0x8b7f, 0x7bc0},  {0x8c00, 0x8304},
        {0x8c7f, 0x91c8},  {0x8d00, 0x990c},  {0x8d7f, 0xa7d0},  {0x8e00, 0xaf14},
        {0x8e7f, 0xbdd8},  {0x8f00, 0xc51c},  {0x8f7f, 0xd3e0},  {0x9000, 0xdb24},
        {0x907f, 0xe9e8},  {0x9100, 0xf12c},  {0x917f, 0xfff0},  {0x9200, 0x10734},
        {0x927f, 0x115f8}, {0x9300, 0x11d3c}, {0x937f, 0x12c00}, {0x9400, 0x13344},
        {0x947f, 0x14208}, {0x9500, 0x1494c}, {0x957f, 0x15810}, {0x9600, 0x15f54},
        {0x967f, 0x16e18}, {0x9700, 0x1755c}, {0x977f, 0x18420}, {0x9800, 0x18b64},
        {0xffff, 0}
    };

    if (a0 >= 0x8140 && a0 <= 0x84be) {
        while (table_8140[i][0] <= a0) i++;
        a0 -= table_8140[i - 1][0];
        v0 = 0xbfc66000 + (a0 * 0x1e + table_8140[i - 1][1]);
    } else if (a0 >= 0x889f && a0 <= 0x9872) {
        while (table_889f[i][0] <= a0) i++;
        a0 -= table_889f[i - 1][0];
        v0 = 0xbfc66000 + (a0 * 0x1e + table_889f[i - 1][1]);
    } else {
        v0 = 0xffffffff;
    }

    pc0 = ra;
}

void psxBios_GetC0Table(HLE_BIOS_CALL_ARGS) { // 56
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x56]);

    v0 = TABLE_C0;
    pc0 = ra;
}

void psxBios_GetB0Table(HLE_BIOS_CALL_ARGS) { // 57
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x57]);

    v0 = TABLE_B0;
    pc0 = ra;
}

#if HLE_ENABLE_MCD
void psxBios__card_chan(HLE_BIOS_CALL_ARGS) { // 0x58
    PSXBIOS_LOG("psxBios_%s\n", biosB0n[0x58]);

    v0 = card_active_chan;
    pc0 = ra;
}

void psxBios__card_status(HLE_BIOS_CALL_ARGS) { // 5c
    PSXBIOS_LOG("psxBios_%s: %x\n", biosB0n[0x5c], a0);

    v0 = card_active_chan;
    pc0 = ra;
}

void psxBios__card_wait(HLE_BIOS_CALL_ARGS) { // 5d
    PSXBIOS_LOG("psxBios_%s: %x\n", biosB0n[0x5d], a0);

    v0 = 1;
    pc0 = ra;
}
#endif


/* System calls C0 */

#if HLE_ENABLE_ENTRYINT
/*
 * int SysEnqIntRP(int index , long *queue);
 */

void psxBios_SysEnqIntRP(HLE_BIOS_CALL_ARGS) { // 02
    PSXBIOS_LOG("psxBios_%s: %x (0x%x)\n", biosC0n[0x02] ,a0, a1);

    SysIntRP[a0] = a1;

    v0 = 0; pc0 = ra;
}

/*
 * int SysDeqIntRP(int index , long *queue);
 */

void psxBios_SysDeqIntRP(HLE_BIOS_CALL_ARGS) { // 03
    PSXBIOS_LOG("psxBios_%s: %x (0x%x)\n", biosC0n[0x03], a0, a1);

    SysIntRP[a0] = 0;

    v0 = 0; pc0 = ra;
}
#endif

using VoidFnptr = void (*)();
using HleBiosFnptr = void (*)(HLE_BIOS_CALL_ARGS);

using HLE_BIOS_TABLE = HleBiosFnptr[256]; 

HLE_BIOS_TABLE biosA0 = {};
HLE_BIOS_TABLE biosB0 = {};
HLE_BIOS_TABLE biosC0 = {};

#include "sjisfont.h"

void psxBiosResetToNone() {
    for(int i = 0; i < 256; i++) {
        biosA0[i] = nullptr;
        biosB0[i] = nullptr;
        biosC0[i] = nullptr;
    }
}

void psxBiosInit_StdLib() {

    biosA0[0x3e] = psxBios_puts;
    biosA0[0x3f] = psxBios_printf;

    biosB0[0x3d] = psxBios_putchar;
    biosB0[0x3f] = psxBios_puts;

#if HLE_ENABLE_MCD
    biosA0[0x00] = psxBios_open;
    biosA0[0x01] = psxBios_lseek;
    biosA0[0x02] = psxBios_read;
    biosA0[0x03] = psxBios_write;
    biosA0[0x04] = psxBios_close;
#endif

    //biosA0[0x05] = psxBios_ioctl;
    //biosA0[0x06] = psxBios_exit;
    //biosA0[0x07] = psxBios_sys_a0_07;
    biosA0[0x0a] = psxBios_todigit;
    //biosA0[0x0b] = psxBios_atof;
    //biosA0[0x0c] = psxBios_strtoul;
    //biosA0[0x0d] = psxBios_strtol;
    biosA0[0x0e] = psxBios_abs;
    biosA0[0x0f] = psxBios_labs;
    biosA0[0x10] = psxBios_atoi;
    biosA0[0x11] = psxBios_atol;
    //biosA0[0x12] = psxBios_atob;
    biosA0[0x13] = psxBios_setjmp;
    biosA0[0x14] = psxBios_longjmp;
    biosA0[0x15] = psxBios_strcat;
    biosA0[0x16] = psxBios_strncat;
    biosA0[0x17] = psxBios_strcmp;
    biosA0[0x18] = psxBios_strncmp;
    biosA0[0x19] = psxBios_strcpy;
    biosA0[0x1a] = psxBios_strncpy;
    biosA0[0x1b] = psxBios_strlen;
    biosA0[0x1c] = psxBios_index;
    biosA0[0x1d] = psxBios_rindex;
    biosA0[0x1e] = psxBios_strchr;
    biosA0[0x1f] = psxBios_strrchr;
    biosA0[0x20] = psxBios_strpbrk;
    biosA0[0x21] = psxBios_strspn;
    biosA0[0x22] = psxBios_strcspn;
    biosA0[0x23] = psxBios_strtok;
    biosA0[0x24] = psxBios_strstr;
    biosA0[0x25] = psxBios_toupper;
    biosA0[0x26] = psxBios_tolower;
    biosA0[0x27] = psxBios_bcopy;
    biosA0[0x28] = psxBios_bzero;
    biosA0[0x29] = psxBios_bcmp;
    biosA0[0x2a] = psxBios_memcpy;
    biosA0[0x2b] = psxBios_memset;
    biosA0[0x2c] = psxBios_memmove;
    biosA0[0x2d] = psxBios_memcmp;
    biosA0[0x2e] = psxBios_memchr;
    biosA0[0x2f] = psxBios_rand;
    biosA0[0x30] = psxBios_srand;

#if HLE_ENABLE_QSORT
    biosA0[0x31] = psxBios_qsort;
#endif

    //biosA0[0x32] = psxBios_strtod;

#if HLE_ENABLE_HEAP
    if (hle_config_env_heap()) {
        biosA0[0x33] = psxBios_malloc;
        biosA0[0x34] = psxBios_free;
        //biosA0[0x35] = psxBios_lsearch;
        //biosA0[0x36] = psxBios_bsearch;
        biosA0[0x37] = psxBios_calloc;
        biosA0[0x38] = psxBios_realloc;
        biosA0[0x39] = psxBios_InitHeap;
    }
#endif

    //biosA0[0x3a] = psxBios__exit;
    biosA0[0x3b] = psxBios_getchar;
    biosA0[0x3c] = psxBios_putchar;
    //biosA0[0x3d] = psxBios_gets;

    biosB0[0x56] = psxBios_GetC0Table;
    biosB0[0x57] = psxBios_GetB0Table;

    biosA0[0x44] = psxBios_FlushCache;
}

static bool is_hle_full_mode = 0;

void psxBiosInit() {
    PSXBIOS_LOG("psxBiosInit\n");
    psxBiosInitFull();
}

void psxBiosInitOnlyLib() {
    psxBiosInit_StdLib();
    psxBiosInit_Lib();
}

// Intended to be called by the emulator as a basic bios tracing
void psxBiosPrintCall(int table) {
    bool print_all = false;
    bool print_spam = false;
    int call = t1 & 0xff;
    if (table == 0xA0) {
        if (print_all || biosA0[call])
            PSXBIOS_LOG("psxBios traceA: %s (0x%x, 0x%x, 0x%x, 0x%x) (EPC:0x%x, RA:0x%x)\n", biosA0n[call], a0, a1, a2, a3, CP0_EPC, ra);
    } else if (table == 0xB0) {
        if (!print_spam && (call == 0xb || call == 0x17))
            return;
        if (print_all || biosB0[call])
            PSXBIOS_LOG("psxBios traceB: %s (0x%x, 0x%x, 0x%x, 0x%x) (EPC:0x%x, RA:0x%x)\n", biosB0n[call], a0, a1, a2, a3, CP0_EPC, ra);
    } else if (table == 0xC0) {
        if (print_all || biosC0[call])
            PSXBIOS_LOG("psxBios traceC: %s (0x%x, 0x%x, 0x%x, 0x%x) (EPC:0x%x, RA:0x%x)\n", biosC0n[call], a0, a1, a2, a3, CP0_EPC, ra);
    }
}

void psxBiosInit_Lib() {
    //biosA0[0x40] = psxBios_sys_a0_40;
    //biosA0[0x41] = psxBios_LoadTest;

#if HLE_ENABLE_LOADEXEC
    if (hle_config_env_loadexec()) {
        biosA0[0x42] = psxBios_Load;
        biosA0[0x51] = psxBios_LoadExec;
        biosA0[0x43] = psxBios_Exec;
    }
#endif


    //biosA0[0x45] = psxBios_InstallInterruptHandler;

#if HLE_ENABLE_GPU
    biosA0[0x46] = psxBios_GPU_dw;
    biosA0[0x47] = psxBios_mem2vram;
    biosA0[0x48] = psxBios_SendGPU;
    biosA0[0x49] = psxBios_GPU_cw;
    biosA0[0x4a] = psxBios_GPU_cwb;
    biosA0[0x4b] = psxBios_GPU_SendPackets;
    biosA0[0x4c] = psxBios_sys_a0_4c;
    biosA0[0x4d] = psxBios_GPU_GetGPUStatus;
#endif

    //biosA0[0x4e] = psxBios_GPU_sync;	
    //biosA0[0x4f] = psxBios_sys_a0_4f;
    //biosA0[0x50] = psxBios_sys_a0_50;
    //biosA0[0x52] = psxBios_GetSysSp;
    //biosA0[0x53] = psxBios_sys_a0_53;
    //biosA0[0x54] = psxBios__96_init_a54;
    //biosA0[0x55] = psxBios__bu_init_a55;
    //biosA0[0x56] = psxBios__96_remove_a56;
    //biosA0[0x57] = psxBios_sys_a0_57;
    //biosA0[0x58] = psxBios_sys_a0_58;
    //biosA0[0x59] = psxBios_sys_a0_59;
    //biosA0[0x5a] = psxBios_sys_a0_5a;
    //biosA0[0x5b] = psxBios_dev_tty_init;
    //biosA0[0x5c] = psxBios_dev_tty_open;
    //biosA0[0x5d] = psxBios_sys_a0_5d;
    //biosA0[0x5e] = psxBios_dev_tty_ioctl;
    //biosA0[0x5f] = psxBios_dev_cd_open;
    //biosA0[0x60] = psxBios_dev_cd_read;
    //biosA0[0x61] = psxBios_dev_cd_close;
    //biosA0[0x62] = psxBios_dev_cd_firstfile;
    //biosA0[0x63] = psxBios_dev_cd_nextfile;
    //biosA0[0x64] = psxBios_dev_cd_chdir;
    //biosA0[0x65] = psxBios_dev_card_open;
    //biosA0[0x66] = psxBios_dev_card_read;
    //biosA0[0x67] = psxBios_dev_card_write;
    //biosA0[0x68] = psxBios_dev_card_close;
    //biosA0[0x69] = psxBios_dev_card_firstfile;
    //biosA0[0x6a] = psxBios_dev_card_nextfile;
    //biosA0[0x6b] = psxBios_dev_card_erase;
    //biosA0[0x6c] = psxBios_dev_card_undelete;
    //biosA0[0x6d] = psxBios_dev_card_format;
    //biosA0[0x6e] = psxBios_dev_card_rename;
    //biosA0[0x6f] = psxBios_dev_card_6f;

    //biosA0[0x73] = psxBios_sys_a0_73;
    //biosA0[0x74] = psxBios_sys_a0_74;
    //biosA0[0x75] = psxBios_sys_a0_75;
    //biosA0[0x76] = psxBios_sys_a0_76;
    //biosA0[0x77] = psxBios_sys_a0_77;
    //biosA0[0x78] = psxBios__96_CdSeekL;
    //biosA0[0x79] = psxBios_sys_a0_79;
    //biosA0[0x7a] = psxBios_sys_a0_7a;
    //biosA0[0x7b] = psxBios_sys_a0_7b;
    //biosA0[0x7c] = psxBios__96_CdGetStatus;
    //biosA0[0x7d] = psxBios_sys_a0_7d;
    //biosA0[0x7e] = psxBios__96_CdRead;
    //biosA0[0x7f] = psxBios_sys_a0_7f;
    //biosA0[0x80] = psxBios_sys_a0_80;
    //biosA0[0x81] = psxBios_sys_a0_81;
    //biosA0[0x82] = psxBios_sys_a0_82;
    //biosA0[0x83] = psxBios_sys_a0_83;
    //biosA0[0x84] = psxBios_sys_a0_84;
    //biosA0[0x85] = psxBios__96_CdStop;
    //biosA0[0x86] = psxBios_sys_a0_86;
    //biosA0[0x87] = psxBios_sys_a0_87;
    //biosA0[0x88] = psxBios_sys_a0_88;
    //biosA0[0x89] = psxBios_sys_a0_89;
    //biosA0[0x8a] = psxBios_sys_a0_8a;
    //biosA0[0x8b] = psxBios_sys_a0_8b;
    //biosA0[0x8c] = psxBios_sys_a0_8c;
    //biosA0[0x8d] = psxBios_sys_a0_8d;
    //biosA0[0x8e] = psxBios_sys_a0_8e;
    //biosA0[0x8f] = psxBios_sys_a0_8f;
    //biosA0[0x90] = psxBios_sys_a0_90;
    //biosA0[0x91] = psxBios_sys_a0_91;
    //biosA0[0x92] = psxBios_sys_a0_92;
    //biosA0[0x93] = psxBios_sys_a0_93;
    //biosA0[0x94] = psxBios_sys_a0_94;
    //biosA0[0x95] = psxBios_sys_a0_95;
    //biosA0[0x96] = psxBios_AddCDROMDevice;
    //biosA0[0x97] = psxBios_AddMemCardDevide;
    //biosA0[0x98] = psxBios_DisableKernelIORedirection;
    //biosA0[0x99] = psxBios_EnableKernelIORedirection;
    //biosA0[0x9a] = psxBios_sys_a0_9a;
    //biosA0[0x9b] = psxBios_sys_a0_9b;
    //biosA0[0x9c] = psxBios_SetConf;
    //biosA0[0x9d] = psxBios_GetConf;
    //biosA0[0x9e] = psxBios_sys_a0_9e;

    biosA0[0x9f] = psxBios_SetMem;

    //biosA0[0xa0] = psxBios__boot;
    //biosA0[0xa1] = psxBios_SystemError;
    //biosA0[0xa2] = psxBios_EnqueueCdIntr;
    //biosA0[0xa3] = psxBios_DequeueCdIntr;
    //biosA0[0xa4] = psxBios_sys_a0_a4;
    //biosA0[0xa5] = psxBios_ReadSector;
    biosA0[0xa6] = psxBios_get_cd_status;
    //biosA0[0xa7] = psxBios_bufs_cb_0;
    //biosA0[0xa8] = psxBios_bufs_cb_1;
    //biosA0[0xa9] = psxBios_bufs_cb_2;
    //biosA0[0xaa] = psxBios_bufs_cb_3;

#if HLE_ENABLE_EVENT
    if (hle_config_env_event()) {
        biosA0[0x70] = psxBios__bu_init;
        biosB0[0x07] = psxBios_DeliverEvent;
        biosB0[0x08] = psxBios_OpenEvent;
        biosB0[0x09] = psxBios_CloseEvent;
        biosB0[0x0a] = psxBios_WaitEvent;
        biosB0[0x0b] = psxBios_TestEvent;
        biosB0[0x0c] = psxBios_EnableEvent;
        biosB0[0x0d] = psxBios_DisableEvent;
        biosB0[0x20] = psxBios_UnDeliverEvent;
    }
#endif

    biosA0[0xad] = psxBios__card_auto;
    //biosA0[0xae] = psxBios_bufs_cd_4;
    //biosA0[0xaf] = psxBios_sys_a0_af;
    //biosA0[0xb0] = psxBios_sys_a0_b0;
    //biosA0[0xb1] = psxBios_sys_a0_b1;
    //biosA0[0xb2] = psxBios_do_a_long_jmp
    //biosA0[0xb3] = psxBios_sys_a0_b3;
    //biosA0[0xb4] = psxBios_sub_function;
//*******************B0 CALLS****************************
    //biosB0[0x00] = psxBios_SysMalloc;
    //biosB0[0x01] = psxBios_sys_b0_01;

#if HLE_ENABLE_RCNT
    if (hle_config_env_rcnt()) {
        biosB0[0x02] = psxBios_SetRCnt;
        biosB0[0x03] = psxBios_GetRCnt;
        biosB0[0x04] = psxBios_StartRCnt;
        biosB0[0x05] = psxBios_StopRCnt;
        biosB0[0x06] = psxBios_ResetRCnt;
        biosC0[0x0a] = psxBios_ChangeClearRCnt;
    }
#endif

#if HLE_ENABLE_THREAD
    if (hle_config_env_thread()) {
        biosB0[0x0e] = psxBios_OpenTh;
        biosB0[0x0f] = psxBios_CloseTh;
        biosB0[0x10] = psxBios_ChangeTh;
        //biosB0[0x11] = psxBios_psxBios_b0_11;
    }
#endif

#if HLE_ENABLE_PAD
    biosB0[0x12] = psxBios_InitPAD;
    biosB0[0x13] = psxBios_StartPAD;
    biosB0[0x14] = psxBios_StopPAD;
    biosB0[0x15] = psxBios_PAD_init;
    biosB0[0x16] = psxBios_PAD_dr;
    biosB0[0x5b] = psxBios_ChangeClearPad;
#endif

#if HLE_ENABLE_ENTRYINT
    biosB0[0x18] = psxBios_ResetEntryInt;
    biosB0[0x19] = psxBios_HookEntryInt;
#endif

    //biosB0[0x1a] = psxBios_sys_b0_1a;
    //biosB0[0x1b] = psxBios_sys_b0_1b;
    //biosB0[0x1c] = psxBios_sys_b0_1c;
    //biosB0[0x1d] = psxBios_sys_b0_1d;
    //biosB0[0x1e] = psxBios_sys_b0_1e;
    //biosB0[0x1f] = psxBios_sys_b0_1f;
    //biosB0[0x21] = psxBios_sys_b0_21;
    //biosB0[0x22] = psxBios_sys_b0_22;
    //biosB0[0x23] = psxBios_sys_b0_23;
    //biosB0[0x24] = psxBios_sys_b0_24;
    //biosB0[0x25] = psxBios_sys_b0_25;
    //biosB0[0x26] = psxBios_sys_b0_26;
    //biosB0[0x27] = psxBios_sys_b0_27;
    //biosB0[0x28] = psxBios_sys_b0_28;
    //biosB0[0x29] = psxBios_sys_b0_29;
    //biosB0[0x2a] = psxBios_sys_b0_2a;
    //biosB0[0x2b] = psxBios_sys_b0_2b;
    //biosB0[0x2c] = psxBios_sys_b0_2c;
    //biosB0[0x2d] = psxBios_sys_b0_2d;
    //biosB0[0x2e] = psxBios_sys_b0_2e;
    //biosB0[0x2f] = psxBios_sys_b0_2f;
    //biosB0[0x30] = psxBios_sys_b0_30;
    //biosB0[0x31] = psxBios_sys_b0_31;

#if HLE_ENABLE_MCD
    biosA0[0x08] = psxBios_getc;
    biosA0[0x09] = psxBios_putc;
    biosB0[0x32] = psxBios_open;
    biosB0[0x33] = psxBios_lseek;
    biosB0[0x34] = psxBios_read;
    biosB0[0x35] = psxBios_write;
    biosB0[0x36] = psxBios_close;

    //biosB0[0x37] = psxBios_ioctl;
    //biosB0[0x38] = psxBios_exit;
    //biosB0[0x39] = psxBios_sys_b0_39;
    //biosB0[0x3a] = psxBios_getc;
    //biosB0[0x3b] = psxBios_putc;
    biosB0[0x3c] = psxBios_getchar;
    //biosB0[0x3e] = psxBios_gets;
    //biosB0[0x40] = psxBios_cd;
    //biosB0[0x46] = psxBios_undelete;
    //biosB0[0x47] = psxBios_AddDevice;
    //biosB0[0x48] = psxBios_RemoteDevice;
    //biosB0[0x49] = psxBios_PrintInstalledDevices;
#endif

#if HLE_ENABLE_MCD
    biosB0[0x42] = psxBios_firstfile;
    biosB0[0x43] = psxBios_nextfile;
    biosB0[0x44] = psxBios_rename;
    biosB0[0x45] = psxBios_delete;
#endif

#if HLE_ENABLE_MCD
    biosB0[0x41] = psxBios_format;
#endif

#if HLE_ENABLE_MCD
    if (hle_config_env_mcd()) {
        biosA0[0xab] = psxBios__card_info;
        biosA0[0xac] = psxBios__card_load;

        biosB0[0x4a] = psxBios_InitCARD;
        biosB0[0x4b] = psxBios_StartCARD;
        biosB0[0x4c] = psxBios_StopCARD;
        //biosB0[0x4d] = psxBios_sys_b0_4d;
        biosB0[0x4e] = psxBios__card_write;
        biosB0[0x4f] = psxBios__card_read;
        biosB0[0x50] = psxBios__new_card;
        biosB0[0x5c] = psxBios__card_status;
        biosB0[0x58] = psxBios__card_chan;
        biosB0[0x55] = psxBios__get_error;
        biosB0[0x5d] = psxBios__card_wait;
    }
#endif

    biosB0[0x51] = psxBios_Krom2RawAdd;
    //biosB0[0x52] = psxBios_sys_b0_52;
    //biosB0[0x53] = psxBios_sys_b0_53;
    //biosB0[0x54] = psxBios__get_errno;

    //biosB0[0x59] = psxBios_sys_b0_59;
    //biosB0[0x5a] = psxBios_sys_b0_5a;
//*******************C0 CALLS****************************
    //biosC0[0x00] = psxBios_InitRCnt;
    //biosC0[0x01] = psxBios_InitException;

#if HLE_ENABLE_ENTRYINT
    biosB0[0x17] = psxBios_ReturnFromException;
    biosC0[0x02] = psxBios_SysEnqIntRP;
    biosC0[0x03] = psxBios_SysDeqIntRP;
#endif

    //biosC0[0x04] = psxBios_get_free_EvCB_slot;
    //biosC0[0x05] = psxBios_get_free_TCB_slot;
    //biosC0[0x06] = psxBios_ExceptionHandler;
    //biosC0[0x07] = psxBios_InstallExeptionHandler;
    //biosC0[0x08] = psxBios_SysInitMemory;
    //biosC0[0x09] = psxBios_SysInitKMem;
    //biosC0[0x0b] = psxBios_SystemError;
    //biosC0[0x0c] = psxBios_InitDefInt;
    //biosC0[0x0d] = psxBios_sys_c0_0d;
    //biosC0[0x0e] = psxBios_sys_c0_0e;
    //biosC0[0x0f] = psxBios_sys_c0_0f;
    //biosC0[0x10] = psxBios_sys_c0_10;
    //biosC0[0x11] = psxBios_sys_c0_11;
    //biosC0[0x12] = psxBios_InstallDevices;
    //biosC0[0x13] = psxBios_FlushStfInOutPut;
    //biosC0[0x14] = psxBios_sys_c0_14;
    //biosC0[0x15] = psxBios__cdevinput;
    //biosC0[0x16] = psxBios__cdevscan;
    //biosC0[0x17] = psxBios__circgetc;
    //biosC0[0x18] = psxBios__circputc;
    //biosC0[0x19] = psxBios_ioabort;
    //biosC0[0x1a] = psxBios_sys_c0_1a
    //biosC0[0x1b] = psxBios_KernelRedirect;
    //biosC0[0x1c] = psxBios_PatchAOTable;
}

void psxBiosInitFull() {

    psxBiosInit_StdLib();
    psxBiosInit_Lib();

#if HLE_ENABLE_EVENT
    u32 base;
    int size;

    base = 0x1000;
    size = sizeof(EvCB) * 32;
    EventCB = (EvCB *)(PSX_ROM_START + base); base += size * 6;
    if (HLE_FULL) {
        memset(EventCB, 0, size * 6);
    }
    HwEV = EventCB;
    EvEV = EventCB + 32;
    RcEV = EventCB + 32 * 2;
    UeEV = EventCB + 32 * 3;
    SwEV = EventCB + 32 * 4;
    ThEV = EventCB + 32 * 5;
#endif

#if HLE_ENABLE_THREAD
    initThread();
#endif

#if HLE_ENABLE_ENTRYINT
    memset(SysIntRP, 0, sizeof(SysIntRP));
    jmp_int = 0;
#endif

#if HLE_ENABLE_PAD
    pad_stopped = 1;
    pad_buf = NULL;
    pad_buf1 = NULL;
    pad_buf2 = NULL;
    pad_buf1len = pad_buf2len = 0;
#endif

#if HLE_ENABLE_HEAP
    heap_addr = NULL;
    heap_end = NULL;
    heap_size = 0;
#endif

#if HLE_ENABLE_MCD
    CardState = -1;
    card_active_chan = 0;
#endif

#if HLE_ENABLE_MCD
    memset(FDesc, 0, sizeof(FDesc));
#endif

#if HLE_FULL
    if (hle_config_env_full()) {
        is_hle_full_mode = 1;
        psxFs_CacheFilesystem();

        // not sure about these, the HLE seems to skip them which, I expect, is only wise
        // if we're bypassing BIOS entirely. --jstine

        biosA0[0x71] = psxBios__96_init;
        biosA0[0x72] = psxBios__96_remove;

        // I'm not quite sure what this is about ... it's setting up some values into B0/C0 table, so I assume
        // it should only be performed when bypassing BIOS entirely --jstine

        auto* ptr = (u32 *)PSXM(TABLE_B0);
        StoreToLE(ptr[0], 0x4c54 - 0x884);

        ptr = (u32 *)PSXM(TABLE_C0);
        StoreToLE(ptr[6], 0xc80);

        StoreToLE(psxMu32ref(0x0150), 0x160);
        StoreToLE(psxMu32ref(0x0154), 0x320);
        StoreToLE(psxMu32ref(0x0160), 0x248);
        strcpy((char *)PSXM(0x248), "bu");
    /*	StoreToLE(psxMu32ref(0x0ca8), 0x1f410004);
        StoreToLE(psxMu32ref(0x0cf0), 0x3c020000);
        StoreToLE(psxMu32ref(0x0cf4), 0x2442641c);
        StoreToLE(psxMu32ref(0x09e0), 0x43d0);
        StoreToLE(psxMu32ref(0x4d98), 0x946f000a);
    */
        // opcode HLE
        (u32&)PSX_ROM_START[0x0000] = LoadFromLE<u32>((0x3b << 26) | 4);
        /* Whatever this does, it actually breaks CTR, even without the uninitiliazed memory patch.
        Normally games shouldn't read from address 0 yet they do. See explanation below in details. */
        //StoreToLE(psxMu32ref(0x0000), (0x3b << 26) | 0);
        StoreToLE(psxMu32ref(0x00a0), (0x3b << 26) | 1);
        StoreToLE(psxMu32ref(0x00b0), (0x3b << 26) | 2);
        StoreToLE(psxMu32ref(0x00c0), (0x3b << 26) | 3);
        StoreToLE(psxMu32ref(0x4c54), (0x3b << 26) | 0);
        StoreToLE(psxMu32ref(0x8000), (0x3b << 26) | 5);
        StoreToLE(psxMu32ref(0x07a0), (0x3b << 26) | 0);
        StoreToLE(psxMu32ref(0x0884), (0x3b << 26) | 0);
        StoreToLE(psxMu32ref(0x0894), (0x3b << 26) | 0);

        // initial stack pointer for BIOS interrupt
        StoreToLE(psxMu32ref(0x6c80), 0x000085c8);

        // initial RNG seed
        StoreToLE(psxMu32ref(0x9010), 0xac20cc00);

#if HAS_ZLIB
        // fonts
        uLongf len;
        len = 0x80000 - 0x66000;
        uncompress((Bytef *)(PSX_ROM_START + 0x66000), &len, font_8140, sizeof(font_8140));
        len = 0x80000 - 0x69d68;
        uncompress((Bytef *)(PSX_ROM_START + 0x69d68), &len, font_889f, sizeof(font_889f));
#endif

        // memory size 2 MB
        // (mednafen doesn't seem to bother to set this...)
        Write_MEMCTRL2(0x00000b88);


        /*	Some games like R-Types, CTR, Fade to Black read from adress 0x00000000 due to uninitialized pointers.
            See Garbage Area at Address 00000000h in Nocash PSX Specfications for more information.
            Here are some examples of games not working with this fix in place :
            R-type won't get past the Irem logo if not implemented.
            Crash Team Racing will softlock after the Sony logo.
        */

        StoreToLE(psxMu32ref(0x0000),0x00000003);
        /*
        But overwritten by 00000003h after soon.
        StoreToLE(psxMu32ref(0x0000), 0x00001A3C);
        */
        StoreToLE(psxMu32ref(0x0004), 0x800C5A27);
        StoreToLE(psxMu32ref(0x0008), 0x08000403);
        StoreToLE(psxMu32ref(0x000C), 0x00000000);
    }
#endif

    // Init value can be anything but 0/1
    memset(s_debug_ev, 0xFF, sizeof(s_debug_ev));
}

void psxBiosShutdown() {
}


const char* uri_find_domain_colon(const char* src) {
    // tricky: colon is technically a legal filename character if it occurs after a forward slash.
    const char* scan = src;
    while (scan[0] && scan[0] != '/' && scan[0] != ':') ++scan;
    if (scan[0] == ':') {
        return scan;
    }
    return nullptr;
}

void psxBiosLoadExecCdrom() {
    psxFs_CacheFilesystem();

    bool updated_tcb = false;
    std::string exepath;
    if (auto sector = psxFs_GetFileSector("/SYSTEM.CNF;1")) {
        uint8_t buf[2048];
        psxFs_ReadSectorData2048(buf, sector);
        auto alltok = Tokenizer((char*)buf);
        while (auto line = alltok.GetNextToken("\r\n")) {
            auto linetok = Tokenizer(line);
            if (auto lvalue  = linetok.GetNextToken('=')) {
                auto rvalue  = linetok.GetNextToken();

                PSXBIOS_LOG("%s=%s\n", lvalue, rvalue);

                if (strcasecmp(lvalue, "boot") == 0) {
                    if (rvalue) {
                        exepath = rvalue;
                    }
                    else {
                        printf("[ERROR]: SYSTEM.CNF: BOOT lvalue does not have a valid rvalue.\n");
                    }

                    // this shouldn't really ever happen so let's assert by default in debug builds.
                    // probably it's a bug in the parsing logic here, rather than user error.
                    dbg_check(rvalue);
                }
                if (strcasecmp(lvalue, "tcb") == 0) {
                    if (rvalue) {
                        updated_tcb = true;
                        TCB_MAX_THREADS = strtol(rvalue, nullptr, 16);
                    }
                }
            }
        }
    }
    else {
        printf("[INFO]: SYSTEM.CNF not found. Falling back on PSX.EXE...\n");
    }

    if (updated_tcb)
        initThread();

    if (exepath.empty()) {
        exepath = "cdrom:///PSX.EXE";
    }

    // FIXUP all incorrect URI prefixes.
    // Favor triple-slash prefix, since the world of browsers adhere to it.
    //
    // This fixup is only needed when reading legacy SYSTEM.CNF.
    // Homebrew should either assume to adhere to SYSTEM.CNF specs, or should endeavor to define and use
    // an alternative method of defining the executable and parameters for starting the application, one
    // in which a strict conformance can be enforced for the good and sanity of debugging these behaviors.
    //
    // Also it is strongly recommended not to use URIs at all, as these are overly complicated for the purposes
    // of both game and emulator development. Simple mount prefixes are superior, and then the prefixes can
    // be remapped at the system level to a devs hearts content.
    //   /cdrom0/path/to/file  vs. cdrom:///path/to/file


    // Interesting aside: most uses of cdrom: mount points (schemes) are technically incorrect for all URI mount points
    // on PSX. The // should only be used when referencing remote server names. For example, correct notation would be:
    //   cdrom:/SYSTEM.CNF                 (local cdrom, absolute/rooted)
    //   cdrom:///SYSTEM.CNF               (local cdrom, remote machine name is specified as empty)
    //   cdrom://machine2.org/SYSTEM.CNF   (remote machine 'machine2.org' should be referenced)
    //
    // Of special note, this form is considered invalid because relative file URIs are not allowed:
    //   cdrom:SYSTEM.CNF                 (common behavior is to accept this and assume absolute)
    //
    // (more trivia) By the rule of URIs, http:page and http:///page should probably be equivalent to http://127.0.0.1/page
    // except that there's no official statement that 127.0.0.1 or localhost references a local http host (typically it is
    // treated as a remote name that loops back into the host machine in the backend). Moreover, browsers elected to have
    // these forms be converted into remote server names (slugs), if a localhost is not available, eg:
    //  https:page    -> https://page
    //  https:///page -> https://page
    //
    // Which is pretty great when you consider browsers do the opposite for file, assuming that the user's intent is to
    // reference a local resource and thus NOP out the apparent attempt to provide a slug (further complicated on windows,
    // where browsers may attempt to use the presence of the c:\ drive specifier to decide if the URI is intended to
    // refer to a local or remote authority). Note also that browsers favor the triple-slash, file:///, even though the URI
    // standard seems to prefer the single-slash option.
    //
    // Here's how browsers will infer two otherwise-invalid file scheme URIs:
    //  file:tmp      -> file:///tmp
    //  file://tmp    -> file:///tmp

    auto first_nonslash = [](const char* s) {
        if (s[0] != '/' && s[0] != '\\') return s+0;
        if (s[1] != '/' && s[1] != '\\') return s+1;
        if (s[2] != '/' && s[2] != '\\') return s+2;

        dbg_check(s[3] != '/' && s[3] != '\\');
        return s+3;
    };

    static const char mnt_cdrom[] = "cdrom:";

    const char* mount = nullptr;
    const char* post_colon_ptr = nullptr;

    const char* exedata = exepath.c_str();

    if(strncasecmp(exedata, mnt_cdrom, sizeof(mnt_cdrom)-1) == 0) {
        mount = mnt_cdrom;
        post_colon_ptr = exedata + sizeof(mnt_cdrom)-1;
    }

    if (post_colon_ptr) {
        int colon_pos = (post_colon_ptr - exedata);
        if (!colon_pos) {
            PSXBIOS_LOG("Suspicious looking BOOT = %s", exedata);
            mount = mnt_cdrom;
        }
    }
    else {
        post_colon_ptr = exedata;
    }

    if (auto sector = psxFs_GetFileSector(first_nonslash(post_colon_ptr))) {
        uint8_t buf[2048];
        const char id[] = "PS-X EXE";

        psxFs_ReadSectorData2048(buf, sector);
        EXEC_DESCRIPTOR tdesc = *(EXEC_DESCRIPTOR*)(buf + sizeof(PSX_EXE_HEADER));

        for(size_t i=0; i<sizeof(tdesc) / 4; ++i) {
            auto* val = (int32_t*)&tdesc + i;
            StoreToLE(*val, *val);
        }

        intmax_t text_addr = tdesc.t_addr & 0x1fffffff;
        intmax_t text_size = tdesc.t_size;
        auto* ramdest = PSXM(text_addr);
        printf("(hlebios) reading %jd (%08jX) bytes into addr %08jx (host @ %p)\n", JFMT(text_size), JFMT(text_size), text_addr, ramdest);
        if (psxFs_ReadSectorData2048(ramdest, sector+1, (text_size + 2047) / 2048) == 0) {
            dbg_abort("ReadSectorData failed!");
        }

        psxCpuClear(text_addr, text_size / 4);

#if HLE_MEDNAFEN_IFC
        // DUMP! donotcheckin
        if (0) {
            auto* insnptr = (uint32_t*)ramdest;
            for (int i=0; i<text_size; i+=4) {
                printf( "[MIPS] %06jx:%08jx %s\n", JFMT(text_addr) + i, JFMT((uint32_t&)ramdest[i]), DisassembleMIPS(text_addr + i, (uint32_t&)ramdest[i]).c_str());
            }
        }
#endif

        SetPC(tdesc._pc);
        gp  = tdesc._gp;
        sp  = tdesc.s_addr ? tdesc.s_addr : 0x801fff00;

        printf("(hlebios) pc0   = %08X\n", pc0);
        printf("(hlebios) gp    = %08X\n", gp);
        printf("(hlebios) sp    = %08X\n", sp);

        CP0_STATUS &= ~(1ull << 22);	// BEV  (bootstrap)
        CP0_STATUS |=  (7ull << 28);   // enable COP0,1,2
        dbg_check((CP0_STATUS & (1<<31)) == 0);
    }
    else {
        printf("[ERROR]: Failed to load boot executable: %s\n", exedata);
    }
}

#if HLE_ENABLE_PAD

#if HLE_PCSX_IFC
void PAD_startPoll(int port) {
    if (port == 0) {
        PAD1_startPoll(1);
    } else {
        PAD2_startPoll(2);
    }
}

u8 PAD_poll(int port, u8 in) {
    if (port == 0) {
        return PAD1_poll(in);
    } else {
        return PAD2_poll(in);
    }
}

bool PAD_connected(int port) {
    return true;
}
#endif

#if HLE_DUCKSTATION_IFC
bool PAD_connected(int port) {
    return g_pad.GetController(port) != nullptr;
}

u8 PAD_poll(int port, u8 in) {
    const u8 hiz = 0xff;
    auto controller = g_pad.GetController(port);
    if (controller == nullptr)
        return hiz;

    u8 out = hiz;
    bool ack = controller->Transfer(in, &out);
    return out;
}

void PAD_startPoll(int port) {
    PAD_poll(port, 0x01);
}
#endif

#endif

void psxBios_PADpoll(int pad, u8* buf) {
    const u8 hiz = 0xff;
    int bufcount;

    PAD_startPoll(pad);
    buf[0] = PAD_connected(pad) ? 0 : hiz;
    buf[1] = PAD_poll(pad, 0x42);
    if (buf[1] == hiz) {
        bufcount = 0;
    } else if (!(buf[1] & 0x0f)) {
        bufcount = 32;
    } else {
        bufcount = (buf[1] & 0x0f) * 2;
    }
    PAD_poll(pad, 0);
    int i = 2;
    while (bufcount--) {
        buf[i++] = PAD_poll(pad, 0);
    }

#if 0
    PSXBIOS_LOG("psxBios_PADpoll %d:", pad);
    for (int c = 0; c < i; c++) {
        printf("%02x ", buf[c]);
    }
    printf("\n");
#endif
}

#if HLE_ENABLE_EXCEPTION
void biosInterrupt() {
    auto istat = Read_ISTAT();

    // Looks like this is polling the pads on every interrupt, which is definitely
    // not what we want. Will have to dig into it later and see if I can figure out why
    // someone removed the Vsync condition gate below (likely some hack) --jstine

//	if (istat & 0x1) { // Vsync
        if (pad_buf != NULL) {
            u32 *buf = (u32*)pad_buf;

        #if HLE_ENABLE_PAD
            PAD_startPoll(0);
            if (PAD_poll(0, 0x42) == 0x23) {
                PAD_poll(0, 0);
                *buf = PAD_poll(0, 0) << 8;
                *buf |= PAD_poll(0, 0);
                PAD_poll(0, 0);
                *buf &= ~((PAD_poll(0, 0) > 0x20) ? 1 << 6 : 0);
                *buf &= ~((PAD_poll(0, 0) > 0x20) ? 1 << 7 : 0);
            } else {
                PAD_poll(0, 0);
                *buf = PAD_poll(0, 0) << 8;
                *buf|= PAD_poll(0, 0);
            }

            PAD_startPoll(1);
            if (PAD_poll(1, 0x42) == 0x23) {
                PAD_poll(1, 0);
                *buf |= PAD_poll(1, 0) << 24;
                *buf |= PAD_poll(1, 0) << 16;
                PAD_poll(1, 0);
                *buf &= ~((PAD_poll(1, 0) > 0x20) ? 1 << 22 : 0);
                *buf &= ~((PAD_poll(1, 0) > 0x20) ? 1 << 23 : 0);
            } else {
                PAD_poll(1, 0);
                *buf |= PAD_poll(1, 0) << 24;
                *buf |= PAD_poll(1, 0) << 16;
            }
        #endif
        }

        if (!pad_stopped)  {
    #if HLE_ENABLE_PAD
            if (pad_buf1) {
                psxBios_PADpoll(0, pad_buf1);
            }

            if (pad_buf2) {
                psxBios_PADpoll(1, pad_buf2);
            }
    #endif
        }
//	}

    if (istat & 0x1) { // Vsync
        auto& event = RcEV[3][1];
        if (event.status == EVENT_STATUS_ENABLED) {
            if (event.mode == EVENT_MODE_CALLBACK)
                softCall(event.fhandler);
            else
                event.status = EVENT_STATUS_PENDING;
            //assert(false);
            //softCallYield(SCRI_biosInterrupt_Vsync, RcEV[3][1].fhandler);
//			hwWrite32(0x1f801070, ~(1));
        }
    }

    if (istat & 0x70) { // Rcnt 0,1,2

        for (int i = 0; i < 3; i++) {
            if (Read_ISTAT() & (1 << (i + 4))) {
                auto& event = RcEV[i][1];
                if (event.status == EVENT_STATUS_ENABLED) {
                    if (event.mode == EVENT_MODE_CALLBACK)
                        softCall(event.fhandler);
                    else
                        event.status = EVENT_STATUS_PENDING;
                    //assert(false);
                    // FIXME: need to push the current rcnt on the stack.
                    //softCallYield(SCRI_biosInterrupt_Rcnt, RcEV[i][1].fhandler);
                }
                //PSXBIOS_LOG("Clear ISTAT for RCNT %d\n", i);
                Write_ISTAT(~(1 << (i + 4)));
            }
        }
    }
}

void psxBiosException180() {
    // bfc00180 exception vector, which occurs when an exception occurs from the exception handler.
    // normally this never happens, usually indicates a bug in the emulator.

    dbg_abort();
}

void psxBiosException80() {

  static const char* const exmne[16] =
  {
   "INT", "MOD", "TLBL", "TLBS", "ADEL", "ADES", "IBE", "DBE", "SYSCALL", "BP", "RI", "COPU", "OV", NULL, NULL, NULL
  };

    auto excode = (CP0_CAUSE & 0x3c) >> 2;
    switch (excode) {
        case 0x00: // Interrupt
            interrupt_r26 = CP0_EPC;
            // PSXCPU_LOG("interrupt\n");
            SaveRegs();
            sp = psxMu32(0x6c80); // create new stack for interrupt handlers
            biosInterrupt();

            for (int i = 0; i < 8; i++) {
                if (!SysIntRP[i]) continue;

                u32 *queue = (u32 *)PSXM(SysIntRP[i]);
                u32 next = queue[0]; // FIXME queue shall be a linked list
                u32 handler = queue[1];
                u32 verifier = queue[2];

                // In case someone got the idea to read those register to get info on current handler
                s0 = handler;
                s1 = verifier;

                // Call first the verifier
                if (!verifier)  continue;
                softCall(verifier);

                // Continue if verifier return 0
                if (!v0) continue;

                // Otherwise fire the handler
                a0 = v0;
                softCall(handler);

                // FIXME: need to push current queue on the stack.
                //assert(false);
                //softCallYield(SCRI_biosException_Queue, handler);
            }

            if (jmp_int) {
                uint32_t* jmpptr = (uint32_t*)PSXM(jmp_int);
                //PSXBIOS_LOG("jmp_int @ %08x - ra=%08x sp=%08x fp=%08x\n", jmp_int, jmpptr[0], jmpptr[1], jmpptr[2]);
                Write_ISTAT(0xffffffff);

                ra = jmpptr[0];
                sp = jmpptr[1];
                fp = jmpptr[2];
                for (int i = 0; i < 8; i++) // s0-s7
                     GPR_ARRAY[16 + i] = jmpptr[3 + i];
                gp = jmpptr[11];

                v0 = 1;
                pc0 = ra;
                return;
            }
            Write_ISTAT(0);
            break;

        case 0x08: // Syscall
            //PSXBIOS_LOG("syscall exp %x\n", a0);
            switch (a0) {
                case 1: // EnterCritical - disable irq's
                    /* Fixes Medievil 2 not loading up new game, Digimon World not booting up and possibly others */
                    v0 = (CP0_STATUS & 0x404) == 0x404;
                    CP0_STATUS &= ~0x404;
                    break;

                case 2: // ExitCritical - enable irq's
                    CP0_STATUS |= 0x404;
                    break;

                /* Normally this should cover SYS(00h, SYS(04h but they don't do anything relevant so... */
                default:
                    break;
            }
            pc0 = CP0_EPC + 4;

            CP0_RFE();
            return;

        case 0xa:  // Reserved instruction exception
            dbg_check(false);
        break;

        case 0xb:  // Reserved instruction exception
            dbg_check(false);
        break;

        default:
            PSXBIOS_LOG("unknown bios exception 0x%x (%s)\n", excode, exmne[excode]);
            break;
    }

    pc0 = CP0_EPC;
    if (CP0_CAUSE & 0x80000000) pc0+=4;

    CP0_RFE();
}
#endif

bool psxbios_invoke_any(u32 callTableId, const HLE_BIOS_TABLE& table, const char * const names[256]) {
    int call = t1 & 0xff;

    // Legend replaces malloc/free with custom implementation
    if (callTableId == 0xA0 && call >= 0x33 && call <= 0x39) {
        auto* ptr = (u32*)PSXM(TABLE_A0);
        auto func = LoadFromLE(ptr[call]);
        if ((func & 0xFF00'0000) == 0x8000'0000) {
            PSXBIOS_LOG("skip callfunc %s (game custom version)\n", names[call]);
            pc0 = func; // Jump to the function as we don't have the table dispatcher
            return 1;
        }
    }

    if (table[call]) {
        auto yieldCallId = HleMakeYieldUid(callTableId, call, 0);
        table[call](yieldCallId);
        return 1;
    }
    else {
        // a trace for calls that are being made to unimplemented functions.
        // Traces for implemented functions are handled by the functions, to allow them to add their own clever/useful info.
        if (const char* name = names[call]) {
            auto my_suppress = [name](const char* check) {
                if (strcmp(name, check)) return false;
                return is_suppressed(check);
            };
            // filter out some very spammy calls.
            if (!s_suppress_spam ||
                   ((!my_suppress("putchar"               ))
                &&  (!my_suppress("strlen"                ))
                &&  (!my_suppress("ReturnFromExecption"   ))
                &&  (!my_suppress("TestEvent"             ))
            )){
                PSXBIOS_LOG("callfunc %s\n", names[call]);
            }
        }
    }

    if (is_hle_full_mode) {
        dbg_abort();
    }
    return 0;
}

extern "C" int32_t psxbios_invoke_A0() { return psxbios_invoke_any(0xA0, biosA0, biosA0n); }
extern "C" int32_t psxbios_invoke_B0() { return psxbios_invoke_any(0xB0, biosB0, biosB0n); }
extern "C" int32_t psxbios_invoke_C0() { return psxbios_invoke_any(0xC0, biosC0, biosC0n); }

static int psxbios_dummy() {
    pc0 = ra;
    return 1;
}

extern "C" int HleDispatchCall(uint32_t pc) {

    if (IsHlePC(pc)) {
        auto id = HleGetCallId(pc);
        // TODO: replace this with improved system... (YieldCallId)
        dbg_abort();
        //dbg_check((u32)id < SCRI_MAX_COUNT);
        //HLE_Call_Table[id]();
        return 1;
    }

    auto masked_pc = pc & USEG_MASK;

    switch (masked_pc) {
        case 0x1FC00180:
            psxBiosException180();
            return 0;
        case 0x80:
            psxBiosException80();
            return 1;
        case 0xA0:
            return psxbios_invoke_A0();
        case 0xB0:
            return psxbios_invoke_B0();
        case 0xC0:
            return psxbios_invoke_C0();
        case 0x8000:
            psxBios_ExecRet();
            return 1;
        case 0x07a0:
        case 0x0884:
        case 0x0894:
        case 0x4c54:
            return psxbios_dummy();
        default:
            break;
    }

    if (is_hle_full_mode) {
        dbg_check (masked_pc >= 0x10000);
    }

    return 0;
}

#if HLE_PCSX_IFC
#define bfreeze(ptr, size) { \
    if (Mode == 1) memcpy(&psxR[base], ptr, size); \
    if (Mode == 0) memcpy(ptr, &psxR[base], size); \
    base += size; \
}

#define bfreezes(ptr) bfreeze(ptr, sizeof(ptr))
#define bfreezel(ptr) bfreeze(ptr, sizeof(*ptr))

#define bfreezepsxMptr(ptr, type) { \
    if (Mode == 1) { \
        if (ptr) StoreToLE(psxRu32ref(base), (u32)(((s8 *)ptr) - psxM)); \
        else psxRu32ref(base) = 0; \
    } else { \
        if (psxRu32(base) != 0) ptr = (type *)(psxM + psxRu32(base)); \
        else (ptr) = NULL; \
    } \
    base += sizeof(u32); \
}

void psxBiosFreeze(int Mode) {
    u32 base = 0x40000;

    //bfreezepsxMptr(jmp_int, u32);
    bfreezepsxMptr(pad_buf, int);
    bfreezepsxMptr(pad_buf1, u8);
    bfreezepsxMptr(pad_buf2, u8);
    bfreezepsxMptr(heap_addr, u32);
    bfreezel(&pad_buf1len);
    bfreezel(&pad_buf2len);
    bfreezes(regs);
    bfreezes(SysIntRP);
    bfreezel(&CardState);
    bfreezes(FDesc);
    bfreezel(&card_active_chan);
    bfreezel(&pad_stopped);
    bfreezel(&heap_size);
}
#endif

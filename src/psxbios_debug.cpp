#include "psxhle-emu-ifc.h"
#include "psdisc-endian.h"

#if HLE_DUCKSTATION_IFC
Log_SetChannel(HLEBIOS);
#endif

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
    "SysMalloc",		"SysFree",	"sys_b0_02",	"sys_b0_03",
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
    "RemoveDevice",		"PrintInstalledDevices", "InitCARD", "StartCARD",
    "StopCARD",			"_card_info_int",	"_card_write",	"_card_read",
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

// Intended to be called by the emulator as a basic bios tracing
void psxBiosPrintCall(int table) {
    bool print_internal = false;
    bool print_all = true;
    bool print_spam = false;
    int call = t1 & 0xff;

    // Skip internal (call from kernel)
    if (!print_internal) {
        if ((ra & PS1_SegmentAddrMask) < 0xFFFF || (ra & PS1_SegmentAddrMask) > PS1_RamMirrorSize)
            return;
    }

    if (table == 0xA0) {
        if (print_all || biosA0[call])
            PSXBIOS_LOG("psxBios traceA: %s (0x%x, 0x%x, 0x%x, 0x%x) (EPC:0x%x, RA:0x%x)\n", biosA0n[call], a0, a1, a2, a3, CP0_EPC, ra);
    } else if (table == 0xB0) {
        if (!print_spam && (call == 0xb || call == 0x17 || call == 0x10))
            return;
        if (call == 0x3d)
            PSXBIOS_LOG("psxBios put: %c\n", a0);
        else if (print_all || biosB0[call])
            PSXBIOS_LOG("psxBios traceB: %s (0x%x, 0x%x, 0x%x, 0x%x) (EPC:0x%x, RA:0x%x)\n", biosB0n[call], a0, a1, a2, a3, CP0_EPC, ra);
    } else if (table == 0xC0) {
        if (print_all || biosC0[call])
            PSXBIOS_LOG("psxBios traceC: %s (0x%x, 0x%x, 0x%x, 0x%x) (EPC:0x%x, RA:0x%x)\n", biosC0n[call], a0, a1, a2, a3, CP0_EPC, ra);
    }

    // Print extra information for some calls
    if (table == 0xB0 && call == 0x10) {
        u32 pcb = LoadFromLE(psxMu32ref(G_PROCESS));
        u32 tcb_current = LoadFromLE(psxMu32ref(pcb));
        u32 tcb_0 = LoadFromLE(psxMu32ref(G_THREADS));
        PSXBIOS_LOG("Change Thread from %x (%d)\n", tcb_current, (tcb_current - tcb_0) / SIZEOF_TCB);
    }
}

void psxBiosPrintEvents() {
    uint32_t status_start = LoadFromLE(psxMu32ref(G_EVENTS)) + offsetof(EVCB, status);
    u32 evcb_max = LoadFromLE(psxMu32ref(G_EVENTS_SIZE)) / SIZEOF_EVCB;

    auto evcb = GetEVCB();
    for (uint32_t i = 0; i < evcb_max; i++) {
        const auto& e = evcb[i];
        if (e.status == EVENT_STATUS::FREE)
            continue;

        std::string status;
        switch (e.status) {
            case EVENT_STATUS::FREE:        status = "FREE"; break;
            case EVENT_STATUS::DISABLED:    status = "DISABLED"; break;
            case EVENT_STATUS::ENABLED:     status = "ENABLED"; break;
            case EVENT_STATUS::DELIVERED:   status = "DELIVERED"; break;
            default:                        status = "???"; break;
        }

        uint32_t status_offset = status_start + i * SIZEOF_EVCB;

        printf("[%d] Status:%s (@0x%08x)\n", i, status.c_str(), status_offset);
        printf("\tclass=%08x\n", e.ev);
        printf("\tspec=%04x\n", e.spec);
        if (e.mode == EVENT_MODE::CALLBACK)
            printf("\thandler=0x%08x\n", e.fhandler);
    }
}

void psxBiosPrintThreads() {
    uint32_t tcb_ptr = LoadFromLE(psxMu32ref(G_THREADS));
    TCB* tcbs = (TCB*)PSXM(tcb_ptr);
    uint32_t tcb_max = LoadFromLE(psxMu32ref(G_THREADS_SIZE)) / SIZEOF_TCB;

    u32 pcb = LoadFromLE(psxMu32ref(G_PROCESS));
    u32 tcb_current = LoadFromLE(psxMu32ref(pcb));

    u32 active_thread = ~0u;
    if (tcb_current >= tcb_ptr) {
        active_thread = (tcb_current - tcb_ptr) / SIZEOF_TCB;
    }

    printf("Thread Control Block 0x%08x, Current Thread 0x%08x\n", tcb_ptr, tcb_current);

    for (auto i = 0u; i < tcb_max ; i++) {
        const auto& t = tcbs[i];

        std::string status;
        switch (t.status) {
            case TCB_THREAD_FREE:       status = "FREE"; break;
            case TCB_THREAD_RESERVED:   status = (active_thread == i) ? "ACTIVE" : "IDLE"; break;
            default:                    status = "???"; break;
        }

        printf("[%d] Status:%s\n", i, status.c_str());
        if (t.status == TCB_THREAD_RESERVED) {
            printf("\tPC:   0x%08x\n", t.func);
            printf("\tSP:   0x%08x\n", t.reg[29]);
            printf("\tSR:   0x%08x\n", t.SR);
            printf("\tCAUSE:0x%08x\n", t.cause);
        }
    }
}

void psxBiosPrintHandlers() {
    uint32_t handlers = LoadFromLE(psxMu32ref(G_HANDLERS));
    uint32_t handlers_max = LoadFromLE(psxMu32ref(G_HANDLERS_SIZE)) / SIZEOF_HANDLER;
    const char* hint[4] = {
        "CDROM, Syscall",
        "Card, VBlank, Timers",
        "Pad",
        "Irqs"
    };
    for (uint32_t prio = 0; prio < handlers_max; prio++) {
        printf("----------  priority %d (%s) ----------\n", prio, (prio < 4) ? hint[prio] : "???");
        uint32_t head = handlers + prio * SIZEOF_HANDLER;
        uint32_t count = 0;
        while (head != 0) {
            HandlerInfo* h = (HandlerInfo*)PSXM(head);
            printf("[%d]\n", count);
            if (h->verifier) {
                printf("\tVerifier: 0x%08x\n", h->verifier);
                if (h->handler)
                    printf("\tHandler:  0x%08x\n", h->handler);
            }
            head = h->next;
            count++;
        }
    };
}

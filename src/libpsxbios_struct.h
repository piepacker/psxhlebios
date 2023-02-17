#include <cstddef>

// Magic value to match the PSX "ABI"
const uint32_t TCB_THREAD_FREE     = 0x1000;
const uint32_t TCB_THREAD_RESERVED = 0x4000;
const uint32_t SIZEOF_PCB          = 0x8;
const uint32_t SIZEOF_TCB          = 0xC0;
const uint32_t SIZEOF_HANDLER      = 0x8;
const uint32_t SIZEOF_EVCB         = 0x1C;

const uint32_t G_HANDLERS      = 0x0100;
const uint32_t G_HANDLERS_SIZE = 0x0104;
const uint32_t G_PROCESS       = 0x0108;
const uint32_t G_PROCESS_SIZE  = 0x010C;
const uint32_t G_THREADS       = 0x0110;
const uint32_t G_THREADS_SIZE  = 0x0114;
const uint32_t G_EVENTS        = 0x0120;
const uint32_t G_EVENTS_SIZE   = 0x0124;
const uint32_t G_FILES         = 0x0140;
const uint32_t G_FILES_SIZE    = 0x0144;
const uint32_t G_DEVICES       = 0x0150;
const uint32_t G_DEVICES_SIZE  = 0x0154;
const uint32_t TABLE_A0        = 0x0200; // vector for call a0
const uint32_t TABLE_B0        = 0x0874; // vector for call b0
const uint32_t TABLE_C0        = 0x0674; // vector for call c0

const uint32_t TIMER_IRQ_AUTO_ACK = 0x8600;
// End of Magic value

const uint32_t CALL_MALLOC = 0x33;
const uint32_t CALL_INIT_HEAP = 0x39;

// Statically allocate some data at the end of the kernel space (0xE000-0xFFFF)
const uint32_t KERNEL_EXCEPTION_VECTOR  = 0x0080; // The exception vector
const uint32_t KERNEL_CP0_STATUS        = 0xE000; // Used internally to disable exception
const uint32_t KERNEL_EXCEPTION_HANDLER = 0xE004; // Reserved PC to call the HLE exception handler
const uint32_t KERNEL_HLE_MAGIC         = 0xE008; // Magical value to detect savestate mode
const uint32_t KERNEL_HEAP              = 0xE100; // Put a heap in the middle for kernel data structure
const uint32_t KERNEL_HEAP_END          = 0xF800;
const uint32_t KERNEL_END               = 0xFFFC;

// Statically allocate some data in the rom
const uint32_t ROM_HLE_STATE        = 0x01000;
const uint32_t ROM_DEVIL_DICE_MAGIC = 0x65FF0;
const uint32_t ROM_FONT_8140        = 0x66000;
const uint32_t ROM_FONT_889F        = 0x69d68;

// Default value of internal structure size
extern uint32_t PCB_MAX;
extern uint32_t TCB_MAX;
extern uint32_t HANDLER_MAX;
extern uint32_t EVCB_MAX;

// Event related info
enum class EVENT_STATUS : uint32_t {
    FREE       = 0x0000,
    DISABLED   = 0x1000,
    ENABLED    = 0x2000, // AKA 'busy'
    DELIVERED  = 0x4000, // AKA 'ready', 'pending'
};

enum class EVENT_MODE : uint32_t {
    CALLBACK    = 0x1000,
    NO_CALLBACK = 0x2000,
};

const uint32_t EVENT_CLASS_CARD_HW   = 0xf000'0011;
const uint32_t EVENT_CLASS_CARD_BIOS = 0xf400'0001;
const uint32_t EVENT_CLASS_TIMER     = 0xf200'0000;
const uint32_t EVENT_CLASS_EXCEPTION = 0xf000'0010;

const uint16_t EVENT_SPEC_INTERRUPT = 0x0002;
const uint16_t EVENT_SPEC_END_IO    = 0x0004;
const uint16_t EVENT_SPEC_TIMEOUT   = 0x0100;
const uint16_t EVENT_SPEC_SYSCALL   = 0x4000;

typedef struct {
    uint32_t ev;
    EVENT_STATUS status;
    uint32_t spec;
    EVENT_MODE mode;
    uint32_t fhandler;
    uint32_t pad0;
    uint32_t pad1;
} EVCB;

typedef struct {
    uint32_t status;
    uint32_t _pad0;
    uint32_t reg[32];
    uint32_t func;
    uint32_t gpr_hi;
    uint32_t gpr_lo;
    uint32_t SR;
    uint32_t cause;
    uint32_t _pad1[9];
} TCB;
static_assert(sizeof(TCB) == SIZEOF_TCB);
const uint32_t TCB_REGS_IDX   = offsetof(TCB, reg)/ 4;
const uint32_t TCB_HI_IDX     = offsetof(TCB, gpr_hi)/ 4;
const uint32_t TCB_LO_IDX     = offsetof(TCB, gpr_lo)/ 4;
const uint32_t TCB_PC_IDX     = offsetof(TCB, func)/ 4;
const uint32_t TCB_STATUS_IDX = offsetof(TCB, SR)/ 4;
const uint32_t TCB_CAUSE_IDX  = offsetof(TCB, cause)/ 4;

struct DIRENTRY {
    char name[20];
    int32_t attr;
    int32_t size;
    uint32_t next;
    int32_t head;
    char system[4];
};

typedef struct {
    char name[32];
    uint32_t  mode;
    uint32_t  offset;
    uint32_t  mcfile;
} FileDesc;

struct AsyncEventInfo {
    uint32_t ev;
    uint16_t spec;
    struct {
        // In order to keep savestate compatibility the old uint16_t port
        // was cut in half. Max repeat value shall be 1024 (for a 128KB memory card of 128B sector)
        uint16_t repeat:12;
        uint16_t port:4;
    };
};
const uint16_t INVALID_REPEAT = 0xFFF; // 12 bits
const uint16_t INVALID_PORT = 0xF; // 4 bits
static_assert(sizeof(AsyncEventInfo) == 8);

struct HandlerInfo {
    uint32_t next;
    uint32_t handler;
    uint32_t verifier;
    uint32_t pad;
};

struct HleState {
    uint32_t version;
    // Entry point
    uint32_t jmp_int; // PSX address
    // Pad
    uint32_t pad_started;
    uint32_t pad_buf;  // PSX address
    uint32_t pad_buf1; // PSX address
    uint32_t pad_buf2; // PSX address
    // Memory Card
    uint32_t cardState;
    uint32_t card_active_chan;
    // Heap
    uint32_t heap_size;
    uint32_t heap_addr; // PSX address
    uint32_t kheap_size;
    uint32_t kheap_addr; // PSX address
    // File
    uint32_t  nfile;
    char ffile[64];
    FileDesc FDesc[32];
    // Misc
    uint32_t initial_sp;
    // Async event handling
    uint32_t async_event_nb;
    AsyncEventInfo async_events[128];
    uint32_t busy_card_info; // 1 bit per port (so 2 bits)
    // Change directory
    uint8_t pwd[32];
};

extern HleState* g_hle;

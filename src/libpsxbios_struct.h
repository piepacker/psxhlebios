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

const uint32_t EVENT_SPEC_INTERRUPT = 0x0002;
const uint32_t EVENT_SPEC_END_IO    = 0x0004;
const uint32_t EVENT_SPEC_SYSCALL   = 0x4000;

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


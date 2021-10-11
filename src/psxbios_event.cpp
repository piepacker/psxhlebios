#include "libpsxbios.h"
#include "psxhle-emu-ifc.h"
#include "psdisc-endian.h"

#include <list>

// Until code is ready
#define ASYNC_EVENT 0

// Keep trace of the event status to only print change
static std::array<u8, 256> s_debug_ev;
static u8 s_print_waitevent_log = true;

struct AsyncEventInfo {
    u32 ev;
    u32 spec;

    AsyncEventInfo(u32 ev_, u32 spec_) : ev(ev_), spec(spec_) {}
};
std::list<AsyncEventInfo> s_pending_events;

void initEvents(u32 kernel_evcb) {
    // Setup Global pointer to event blocks
    StoreToLE(psxMu32ref(G_EVENTS), kernel_evcb | PS1_KernelSegment);
    StoreToLE(psxMu32ref(G_EVENTS_SIZE), SIZEOF_EVCB * EVCB_MAX);

    // Fill the struct with 0
    auto* evcb = PSXM(kernel_evcb);
    memset(evcb, 0, SIZEOF_EVCB * EVCB_MAX);

    // Init not-psx related data structure
    s_pending_events.clear();
    s_debug_ev.fill(0xFF);
}

EVCB* GetEVCB() {
    u32 evcb_addr = LoadFromLE(psxMu32ref(G_EVENTS));
    return(EVCB*)PSXM(evcb_addr);
}

void DeliverEvent(u32 ev, u32 spec) {
#if 1
    // Quite spammy due to default kernel IRQ (vsync and timers)
    if (spec != EVENT_SPEC_INTERRUPT)
        PSXBIOS_LOG("DeliverEvent %x;%x\n", ev, spec);
#endif

    auto evcb = GetEVCB();
    for (u32 i = 0; i < EVCB_MAX; i++) {
        if (evcb[i].status == EVENT_STATUS::ENABLED && evcb[i].ev == ev && evcb[i].spec == spec) {
            if (evcb[i].mode == EVENT_MODE::CALLBACK)
                softCall(evcb[i].fhandler);
            else
                evcb[i].status = EVENT_STATUS::DELIVERED;
        }
    }
}

void PostAsyncEvent(u32 ev, u32 spec) {
#if ASYNC_EVENT
    PSXBIOS_LOG("PostAsyncEvent %8x;%x\n", ev, spec);
    s_pending_events.emplace_back(ev, spec);
#else
    DeliverEvent(ev, spec);
#endif
}

void DeliverAsyncEvent() {
    if (s_pending_events.empty())
        return;

    std::list<AsyncEventInfo> events;
    std::swap(s_pending_events, events);

    while(!events.empty()) {
        const auto& e = events.front();
        DeliverEvent(e.ev, e.spec);
        events.pop_front();
    }
}

static int getFreeEventSlot() {
    auto evcb = GetEVCB();
    for (int i = 0; i < (int)EVCB_MAX; i++) {
        if (evcb[i].status == EVENT_STATUS::FREE) return i;
    }
    return -1;
}

void psxBios_DeliverEvent(HLE_BIOS_CALL_ARGS) { // 07
    PSXBIOS_LOG("psxBios_%s %x,%x\n", biosB0n[0x07], a0, a1);

    DeliverEvent(a0, a1);

    pc0 = ra;
}

void psxBios_OpenEvent(HLE_BIOS_CALL_ARGS) { // 08
    u32 evcb_max = LoadFromLE(psxMu32ref(G_EVENTS_SIZE)) / SIZEOF_EVCB;
    if (evcb_max != EVCB_MAX) {
        PSXBIOS_LOG("psxBios_OpenEvent() WARNING! max events was updated from %d to %d\n", EVCB_MAX, evcb_max);
        EVCB_MAX = evcb_max;
    }

    PSXBIOS_LOG("psxBios_%s (class:%x, spec:%x, mode:%x, func:%x)\n", biosB0n[0x08], a0, a1, a2, a3);

    auto slot = getFreeEventSlot();
    if (slot < 0) {
        v0 = slot;
        pc0 = ra;
        SysPrintf("OpenEvent: no more slot available\n");
        return;
    } else {
        auto evcb = GetEVCB();
        evcb[slot].status = EVENT_STATUS::DISABLED; //  Don't use setOpenEventStatus to set status
        evcb[slot].ev = a0;
        evcb[slot].spec = a1;
        evcb[slot].mode = (EVENT_MODE)a2;
        evcb[slot].fhandler = a3;
    }

    v0 = slot | 0xf100'0000;
    pc0 = ra;

    SysPrintf("\t\t\tslot => %x\n", v0);
}

bool isValidSlot(u32 slot) {
    slot &= 0xFFFF;
    u32 evcb_max = LoadFromLE(psxMu32ref(G_EVENTS_SIZE)) / SIZEOF_EVCB;
    if (slot >= evcb_max) {
        // Game is buggy and depends on an "undefined" behavior
        // In order to behave like the original bios, we will need to have the same
        // start address for the EVCB...
        SysPrintf("\t\t\t=> invalid slot (%x)\n", slot);
        return false;
    }

    return true;
}

void setOpenEventStatus(u32 slot, EVENT_STATUS status) {
    if (!isValidSlot(slot)) {
        return;
    }

    slot &= 0xFFFF;
    auto evcb = GetEVCB();
    if (evcb[slot].status != EVENT_STATUS::FREE)
        evcb[slot].status = status;
}

void psxBios_CloseEvent(HLE_BIOS_CALL_ARGS) { // 09
    PSXBIOS_LOG("psxBios_%s %x\n", biosB0n[0x09], a0);

    setOpenEventStatus(a0, EVENT_STATUS::FREE);

    v0 = 1;
    pc0 = ra;
}

void psxBios_WaitEvent(HLE_BIOS_CALL_ARGS) { // 0a
    uint32_t slot = a0 & 0xFFFF;
    if (!isValidSlot(slot)) {
        v0 = 0;
        pc0 = ra;
        return;
    }

    if (s_print_waitevent_log)
        PSXBIOS_LOG("psxBios_%s %x\n", biosB0n[0x0a], slot);

    auto evcb = GetEVCB();
    switch (evcb[slot].status) {
        case EVENT_STATUS::DELIVERED:
            // Event was delivered. Return valid and get back to enabled state
            // Callback events (mode=EVENT_MODE_CALLBACK) do never set the pending/delivered state (and thus WaitEvent would hang forever).
            if (evcb[slot].mode == EVENT_MODE::NO_CALLBACK) {
                evcb[slot].status = EVENT_STATUS::ENABLED;
            }
            v0 = 1;
            pc0 = ra;
            s_print_waitevent_log = true;
            break;

        case EVENT_STATUS::ENABLED:
            // Event wasn't delivered yet
            // 1/ advance time in the emulator. 200 is a random number. The minimum time for this call is around 30 ticks.
            // But this call is about halting the CPU waiting an event (IRQ), so it is expected to be slow
            AdvanceClock(200);
            // 2/ Emulate an infinite loop
            t1  = 0x0A;
            pc0 = 0xB0;
            // Let's avoid the spam
            s_print_waitevent_log = false;
            break;

        case EVENT_STATUS::DISABLED:
        case EVENT_STATUS::FREE:
        default:
            s_print_waitevent_log = true;
            // Event is invalid
            v0 = 0;
            pc0 = ra;
            break;
    }
}

void psxBios_TestEvent(HLE_BIOS_CALL_ARGS) { // 0b
    auto slot = a0 & 0xFFFF;
    if (!isValidSlot(slot)) {
        v0 = 0;
        pc0 = ra;
        return;
    }

    auto evcb = GetEVCB();
    if (evcb[slot].status == EVENT_STATUS::DELIVERED) {
        if (evcb[slot].mode == EVENT_MODE::NO_CALLBACK) {
            evcb[slot].status = EVENT_STATUS::ENABLED;
        }
        v0 = 1;
    } else {
        v0 = 0;
    }
    pc0 = ra;

    // Print only TestEvent change. The spamy part is the polling of the result
    //PSXBIOS_LOG_SPAM("TestEvent", "psxBios_%s %x,%x: result=%x\n", biosB0n[0x0b], ev, spec, v0);
    if (slot < s_debug_ev.size() && s_debug_ev[slot] != v0) {
        s_debug_ev[slot] = v0;
        PSXBIOS_LOG("psxBios_%s %x: result=%x\n", biosB0n[0x0b], slot, v0);
    }
}

void psxBios_EnableEvent(HLE_BIOS_CALL_ARGS) { // 0c
    PSXBIOS_LOG("psxBios_%s %x\n", biosB0n[0x0c], a0);

    setOpenEventStatus(a0, EVENT_STATUS::ENABLED);

    v0 = 1;
    pc0 = ra;
}

void psxBios_DisableEvent(HLE_BIOS_CALL_ARGS) { // 0d
    PSXBIOS_LOG("psxBios_%s %x\n", biosB0n[0x0d], a0);

    setOpenEventStatus(a0, EVENT_STATUS::DISABLED);

    v0 = 1;
    pc0 = ra;
}

void psxBios_UnDeliverEvent(HLE_BIOS_CALL_ARGS) { // 0x20
    PSXBIOS_LOG("psxBios_%s %x,%x\n", biosB0n[0x20], a0, a1);
    auto evcb = GetEVCB();
    for (u32 i = 0; i < EVCB_MAX; i++) {
        if (evcb[i].status == EVENT_STATUS::DELIVERED && evcb[i].ev == a0 && evcb[i].spec == a1) {
            if (evcb[i].mode == EVENT_MODE::NO_CALLBACK)
                evcb[i].status = EVENT_STATUS::ENABLED;
        }
    }
    pc0 = ra;
}


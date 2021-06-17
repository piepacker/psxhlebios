
#include "psxhle-emu-ifc.h"

#include <cstdint>
#include <cstdio>

#include "psxhle-emu-ifc-regs.h"

// Pick an unmapped area of PSX memory to treat as soft call return address.
static const u32 kSoftCallBaseRetAddr = 0x8100'0000;

HleYieldUid MakeYieldCallId(uint32_t biosCallPage, uint32_t biosCallId)
{
    dbg_check((biosCallPage & 15) == 0);
    dbg_check(biosCallPage < 0xf0);
    dbg_check(biosCallId <= 0xff);

    // page is stored in the lower 8 bits since we need the lower 4 bits to be 0 anyway.
    // and page is always A0/B0/C0 so it satisfies this criteria.

    return (biosCallId << 8) | biosCallPage;
}

// Proper SoftCall:
//  * save the current value of $ra onto the stack.
//  * set a special return address that identifies our HLE function and it's current yield state
//  * when the return address is detected from CPU, it bounces through HLE and resumes state
//    machine execution
//  * pop original $ra off the stack.
//
// Using the VM's stack machine is of critical importance to ensure proper handling of thread
// context switching which may occur during open-ended execution of interpreter.

// Pedantic: the PSX expects 16 bytes of shadow space below the current callstack.
//   Mostly things work without this, because it was only meant for use by debug builds to shadow values
//   passd by register ($a0 -> $a4).  --jstine

static void StackPush(u32 val) {
    sp -= 4;
    psxMu32ref(sp) = val;
}

static void StackPop(u32& val) {
    val = psxMu32ref(sp);
    sp += 4;
}

static HleYieldUid softCallYield(HleYieldUid id, u32 pc) {
    dbg_check ((id & 15) == 0);

    StackPush(ra);
    sp -= 0x10;     // shadow space (see notes earlier)

    // perform equivalent of JAL -- update $ra and set PC.
    ra = kSoftCallBaseRetAddr | id;
    pc0 = pc;
    return id;
}


static void softCallResume() {
    sp += 0x10;
    StackPop(ra);
}

static void HleCallYield(HleYieldUid id) {
    StackPush(ra);
    ra = kSoftCallBaseRetAddr | id;
}

static void HleCallResume() {
    StackPop(ra);
}

static bool IsHlePC(u32 pc) {
    return ((pc & 0xff00'0000) == kSoftCallBaseRetAddr);
}

static HleYieldUid HleGetCallId(u32 pc) {
    return (HleYieldUid)(pc & ~kSoftCallBaseRetAddr);
}

static bool HleYieldCheck(HleYieldUid id) {
    auto pc = pc0;
    if (IsHlePC(pc)) {
        if (id == 0) return 0;
        return (int)id == HleGetCallId(pc);
    }
    return 1;
}

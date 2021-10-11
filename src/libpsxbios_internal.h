#pragma once

#include <stdint.h>
#include "libpsxbios_struct.h"

#undef SysPrintf
#define SysPrintf(fmt, ...) (printf(fmt, ##__VA_ARGS__), fflush(stdout))

extern const char * const biosA0n[256];
extern const char * const biosB0n[256];
extern const char * const biosC0n[256];

void psxBiosInit_StdLib();
void psxBiosInit_Lib();

void psxBiosShutdown();
void psxBiosException80();
void psxBiosFreeze(int Mode);
void psxBiosInitKernelDataStructure();

// Debug function
void psxBiosPrintEvents(); // Called from GDB
void psxBiosPrintThreads(); // Called from GDB

extern uint8_t hleSoftCall;
using HleYieldUid = uint32_t;

// its really helpful to be able to change the call signature of all these functions at once.
#define HLE_BIOS_CALL_ARGS HleYieldUid huid
#define HLE_BIOS_INVOKE_ARGS huid
#define HLE_BIOS_DUMMY_ARGS 0

using VoidFnptr = void (*)();
using HleBiosFnptr = void (*)(HLE_BIOS_CALL_ARGS);

using HLE_BIOS_TABLE = HleBiosFnptr[256];
extern HLE_BIOS_TABLE biosA0;
extern HLE_BIOS_TABLE biosB0;
extern HLE_BIOS_TABLE biosC0;

void softCall(uint32_t pc);

// Event
EVCB* GetEVCB();
void DeliverEvent(uint32_t ev, uint32_t spec);
void initEvents(uint32_t kernel_evcb);
// Async Event
void PostAsyncEvent(uint32_t ev, uint32_t spec);
void DeliverAsyncEvent();
// Event PSX
void psxBios_DeliverEvent(HLE_BIOS_CALL_ARGS);
void psxBios_UnDeliverEvent(HLE_BIOS_CALL_ARGS);
void psxBios_OpenEvent(HLE_BIOS_CALL_ARGS);
void psxBios_CloseEvent(HLE_BIOS_CALL_ARGS);
void psxBios_WaitEvent(HLE_BIOS_CALL_ARGS);
void psxBios_TestEvent(HLE_BIOS_CALL_ARGS);
void psxBios_EnableEvent(HLE_BIOS_CALL_ARGS);
void psxBios_DisableEvent(HLE_BIOS_CALL_ARGS);

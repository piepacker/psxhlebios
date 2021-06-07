#pragma once
#include <cstdint>

// Typical Playstation executable has a 76 byte header laid out as:
//   [16 bytes]        [60 bytes]
//   EXE_HEADER      EXEC_DESCRIPTOR
//
// The EXE_HEADER is only needed within the file itself. The descriptor is maintained separately within
// the HLE BIOS implementation.

struct PSX_EXE_HEADER {
    uint8_t  id[8];
    uint32_t text;
    uint32_t data;
};

struct EXEC_DESCRIPTOR {
    uint32_t _pc;
    uint32_t _gp;
    uint32_t t_addr;
    uint32_t t_size;
    uint32_t d_addr;
    uint32_t d_size;
    uint32_t b_addr;
    uint32_t b_size;
    uint32_t s_addr;
    uint32_t s_size;
    uint32_t SavedSP;
    uint32_t SavedFP;
    uint32_t SavedGP;
    uint32_t SavedRA;
    uint32_t SavedS0;
};

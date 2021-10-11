#include "psxhle-emu-ifc.h"

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

#if HLE_MEDNAFEN_IFC
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

#if HLE_DUCKSTATION_IFC
void VmcDirty(int port) {
    dbg_check((u32)port < 2);
    // FIXME need to set m_changed of the memory card in DS to flush the memory
    // card to the disk
}

char* VmcGet(int port) {
    dbg_check((u32)port < 2);
    auto card = g_pad.GetMemoryCard(port);
    if (!card)
        return nullptr;

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

bool VmcEnabled(int port, int slot) {
    dbg_check((u32)port < 2);
    auto card = g_pad.GetMemoryCard(port);
    return card != nullptr;
}

void VmcCreate(int port) {
}
#endif

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

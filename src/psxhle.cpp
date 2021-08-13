/***************************************************************************
 *   Copyright (C) 2007 Ryan Schultz, PCSX-df Team, PCSX team              *
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

/*
* Internal PSX HLE functions.
* This file is almost entirely PCSX-specific for the time being.
*/

#include "libpsxhle.h"
#include "libpsxbios.h"
#include "psxhle-emu-ifc.h"

#if 0
#define PSXHLE_LOG SysPrintf
#else
#define PSXHLE_LOG(...)
#endif

#if HLE_PCSX_IFC
#include "r3000a.h"

static void hleDummy() {
    psxRegs.pc = psxRegs.GPR.n.ra;
    psxBranchTest();
}

static void hleA0() {
    psxbios_invoke_A0();
    psxBranchTest();
}

static void hleB0() {
    psxbios_invoke_B0();
    psxBranchTest();
}

static void hleC0() {
    psxbios_invoke_C0();
    psxBranchTest();
}

static void hleBootstrap() { // 0xbfc00000
    PSXBIOS_LOG("hleBootstrap\n");
    CheckCdrom();
    LoadCdrom();
    PSXBIOS_LOG("CdromLabel: \"%s\": PC = %8.8x (SP = %8.8x)\n", CdromLabel, psxRegs.pc, psxRegs.GPR.n.sp);
}

typedef struct {
    u32 _pc0;
    u32 gp0;
    u32 t_addr;
    u32 t_size;
    u32 d_addr;
    u32 d_size;
    u32 b_addr;
    u32 b_size;
    u32 S_addr;
    u32 s_size;
    u32 _sp,_fp,_gp,ret,base;
} EXEC;

static void hleExecRet() {
    EXEC *header = (EXEC*)PSXM(psxRegs.GPR.n.s0);

    PSXBIOS_LOG("ExecRet %x: %x\n", psxRegs.GPR.n.s0, header->ret);

    psxRegs.GPR.n.ra = header->ret;
    psxRegs.GPR.n.sp = header->_sp;
    psxRegs.GPR.n.s8 = header->_fp;
    psxRegs.GPR.n.gp = header->_gp;
    psxRegs.GPR.n.s0 = header->base;

    psxRegs.GPR.n.v0 = 1;
    psxRegs.pc = psxRegs.GPR.n.ra;
}

void (*psxHLEt[256])() = {
    hleDummy, hleA0, hleB0, hleC0,
    hleBootstrap, hleExecRet,
    hleDummy, hleDummy
};

#endif

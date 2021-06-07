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

#ifndef __PSXBIOS_H__
#define __PSXBIOS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "psxcommon.h"
#include "r3000a.h"
#include "psxmem.h"
#include "misc.h"
#include "sio.h"

extern const char * const biosA0n[256];
extern const char * const biosB0n[256];
extern const char * const biosC0n[256];

void psxBiosInit_StdLib();
void psxBiosInitFull();
void psxBiosInit();
void psxBiosLoadExecCdrom();
void psxBiosShutdown();
void psxBiosException80();
void psxBiosFreeze(int Mode);

extern void (*biosA0[256])();
extern void (*biosB0[256])();
extern void (*biosC0[256])();

extern boolean hleSoftCall;

#ifdef __cplusplus
}
#endif
#endif

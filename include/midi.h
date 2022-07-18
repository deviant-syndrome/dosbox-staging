/*
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *
 *  Copyright (C) 2020-2021  The DOSBox Staging Team
 *  Copyright (C) 2002-2021  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef DOSBOX_MIDI_H
#define DOSBOX_MIDI_H

#include "dosbox.h"

#include "setup.h"

class Program;

extern uint8_t MIDI_evt_len[256];

constexpr auto MIDI_SYSEX_SIZE = 8192;

void MIDI_Init(Section *sec);
bool MIDI_Available();
void MIDI_ListAll(Program *output_handler);
void MIDI_RawOutByte(uint8_t data);


enum {MOUT_MPU,MOUT_SBUART,MOUT_GUS,MOUT_THRU};

enum {MDEV_MPU,MDEV_SBUART,MDEV_GUS,MDEV_SB16,MDEV_NONE};

void MIDI_RawOutByte(Bit8u data, Bit8u slot);
Bits MIDI_InputSysex(Bit8u *sysex,Bitu len, bool abort);
void MIDI_RawOutRTByte(Bit8u data);
void MIDI_RawOutThruRTByte(Bit8u data);
void MIDI_ClearBuffer(Bit8u slot);
bool MIDI_Available(void);
Bit32s MIDI_ToggleInputDevice(Bit32u device,bool status);

void MIDI_InputMsg(Bit8u msg[4], Bitu len);
// void MPU401_InputMsg(Bit8u msg[4]);
void SB_UART_InputMsg(Bit8u msg[4], Bitu len);
// void GUS_UART_In putMsg(Bit8u msg[4]);

// Bits MPU401_InputSysex(Bit8u* buffer,Bitu len,bool abort);
Bits SB_UART_InputSysex(Bit8u* buffer,Bitu len, bool abort);
// Bits GUS_UART_InputSysex(Bit8u* buffer,Bitu len,bool abort);

// void MPU401_SetupTxHandler(void);
// void MPU401_SetTx(bool status);

// void SB16_MPU401_IrqToggle(bool status);

// Bit32s MIDI_ToggleInputDevice(Bit32u device,bool status);


#if C_FLUIDSYNTH
void FLUID_AddConfigSection(const config_ptr_t &conf);
#endif

#if C_MT32EMU
void MT32_AddConfigSection(const config_ptr_t &conf);
#endif

#endif

/******************************************************************************/
/* Mednafen NEC PC-FX Emulation Module                                        */
/******************************************************************************/
/* king.h:
**  Copyright (C) 2006-2016 Mednafen Team
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software Foundation, Inc.,
** 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef __PCFX_KING_H
#define __PCFX_KING_H

namespace MDFN_IEN_PCFX
{

//
// Be sure to keep the numbered/lettered *_GSREG_* registers(MPROG0*, AFFIN*, etc.) here in contiguous sequential order(since we do stuff like "- KING_GSREG_MPROG0"
// in king.cpp.
//

void KING_StartFrame(VDC **, EmulateSpecStruct *espec);
void KING_SetPixelFormat(const MDFN_PixelFormat &format);
uint16 FXVCE_Read16(uint32 A);
void FXVCE_Write16(uint32 A, uint16 V);

uint8 KING_Read8(const v810_timestamp_t timestamp, uint32 A);
uint16 KING_Read16(const v810_timestamp_t timestamp, uint32 A);

void KING_Write8(const v810_timestamp_t timestamp, uint32 A, uint8 V);
void KING_Write16(const v810_timestamp_t timestamp, uint32 A, uint16 V);
void KING_Init(void) MDFN_COLD;
void KING_Close(void) MDFN_COLD;
void KING_Reset(const v810_timestamp_t timestamp) MDFN_COLD;

uint16 KING_GetADPCMHalfWord(int ch);

uint8 KING_MemPeek(uint32 A);

uint8 KING_RB_Fetch();

void KING_SetLayerEnableMask(uint64 mask);

void KING_StateAction(StateMem *sm, const unsigned load, const bool data_only);

void KING_SetGraphicsDecode(MDFN_Surface *surface, int line, int which, int xscroll, int yscroll, int pbn);

void KING_NotifyOfBPE(bool read, bool write);

void KING_SetLogFunc(void (*logfunc)(const char *, const char *, ...));

void KING_EndFrame(v810_timestamp_t timestamp);
void KING_ResetTS(v810_timestamp_t ts_base);

v810_timestamp_t MDFN_FASTCALL KING_Update(const v810_timestamp_t timestamp);

}

#endif

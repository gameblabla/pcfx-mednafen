/* V810 Emulator
 *
 * Copyright (C) 2006 David Tucker
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* Alternatively, the V810 emulator code(and all V810 emulation header files) can be used/distributed under the following license(you can adopt either
   license exclusively for your changes by removing one of these license headers, but it's STRONGLY preferable
   to keep your changes dual-licensed as well):

This Reality Boy emulator is copyright (C) David Tucker 1997-2008, all rights
reserved.   You may use this code as long as you make no money from the use of
this code and you acknowledge the original author (Me).  I reserve the right to
dictate who can use this code and how (Just so you don't do something stupid
with it).
   Most Importantly, this code is swap ware.  If you use It send along your new
program (with code) or some other interesting tidbits you wrote, that I might be
interested in.
   This code is in beta, there are bugs!  I am not responsible for any damage
done to your computer, reputation, ego, dog, or family life due to the use of
this code.  All source is provided as is, I make no guaranties, and am not
responsible for anything you do with the code (legal or otherwise).
   Virtual Boy is a trademark of Nintendo, and V810 is a trademark of NEC.  I am
in no way affiliated with either party and all information contained hear was
found freely through public domain sources.
*/

//////////////////////////////////////////////////////////
// CPU routines

#include <mednafen/mednafen.h>
#include <trio/trio.h>

//#include "pcfx.h"
//#include "debug.h"

#include <string.h>
#include <errno.h>

#include "v810_opt.h"
#include "v810_cpu.h"

V810::V810()
{
	MemRead8 = NULL;
	MemRead16 = NULL;
	MemRead32 = NULL;

	IORead8 = NULL;
	IORead16 = NULL;
	IORead32 = NULL;

	MemWrite8 = NULL;
	MemWrite16 = NULL;
	MemWrite32 = NULL;

	IOWrite8 = NULL;
	IOWrite16 = NULL;
	IOWrite32 = NULL;

	memset(FastMap, 0, sizeof(FastMap));

	memset(MemReadBus32, 0, sizeof(MemReadBus32));
	memset(MemWriteBus32, 0, sizeof(MemWriteBus32));

	v810_timestamp = 0;
	next_event_ts = 0x7FFFFFFF;
}

V810::~V810()
{
	Kill();
}

INLINE void V810::RecalcIPendingCache(void)
{
	IPendingCache = 0;

	// Of course don't generate an interrupt if there's not one pending!
	if(ilevel < 0)
		return;
	
	// If the NMI pending, exception pending, and/or interrupt disabled bit
	// is set, don't accept any interrupts.
	if(S_REG[PSW] & (PSW_NP | PSW_EP | PSW_ID))
		return;

	// If the interrupt level is lower than the interrupt enable level, don't
	// accept it.
	if(ilevel < (int)((S_REG[PSW] & PSW_IA) >> 16))
		return;

	IPendingCache = 0xFF;
}

// TODO: "An interrupt that occurs during restore/dump/clear operation is internally held and is accepted after the
// operation in progress is finished. The maskable interrupt is held internally only when the EP, NP, and ID flags
// of PSW are all 0."
//
// This behavior probably doesn't have any relevance on the PC-FX, unless we're sadistic
// and try to restore cache from an interrupt acknowledge register or dump it to a register
// controlling interrupt masks...  I wanna be sadistic~

void V810::CacheClear(v810_timestamp_t &timestamp, uint32 start, uint32 count)
{
	//printf("Cache clear: %08x %08x\n", start, count);
	for(uint32 i = 0; i < count && (i + start) < 128; i++)
		memset(&Cache[i + start], 0, sizeof(V810_CacheEntry_t));
}

INLINE void V810::CacheOpMemStore(v810_timestamp_t &timestamp, uint32 A, uint32 V)
{
	if(MemWriteBus32[A >> 24])
	{
		timestamp += 2;
		MemWrite32(timestamp, A, V);
	}
	else
	{
		timestamp += 2;
		MemWrite16(timestamp, A, V & 0xFFFF);

		timestamp += 2;
		MemWrite16(timestamp, A | 2, V >> 16);
	}
}

INLINE uint32 V810::CacheOpMemLoad(v810_timestamp_t &timestamp, uint32 A)
{
	if(MemReadBus32[A >> 24])
	{
		timestamp += 2;
		return(MemRead32(timestamp, A));
	}
	else
	{
		uint32 ret;

		timestamp += 2;
		ret = MemRead16(timestamp, A);

		timestamp += 2;
		ret |= MemRead16(timestamp, A | 2) << 16;
		return(ret);
	}
}

void V810::CacheDump(v810_timestamp_t &timestamp, const uint32 SA)
{
	printf("Cache dump: %08x\n", SA);

	for(int i = 0; i < 128; i++)
	{
		CacheOpMemStore(timestamp, SA + i * 8 + 0, Cache[i].data[0]);
		CacheOpMemStore(timestamp, SA + i * 8 + 4, Cache[i].data[1]);
	}

	for(int i = 0; i < 128; i++)
	{
		uint32 icht = Cache[i].tag | ((int)Cache[i].data_valid[0] << 22) | ((int)Cache[i].data_valid[1] << 23);

		CacheOpMemStore(timestamp, SA + 1024 + i * 4, icht);
	}

}

void V810::CacheRestore(v810_timestamp_t &timestamp, const uint32 SA)
{
	printf("Cache restore: %08x\n", SA);

	for(int i = 0; i < 128; i++)
	{
		Cache[i].data[0] = CacheOpMemLoad(timestamp, SA + i * 8 + 0);
		Cache[i].data[1] = CacheOpMemLoad(timestamp, SA + i * 8 + 4);
	}

	for(int i = 0; i < 128; i++)
	{
		uint32 icht;

		icht = CacheOpMemLoad(timestamp, SA + 1024 + i * 4);

		Cache[i].tag = icht & ((1 << 22) - 1);
		Cache[i].data_valid[0] = (icht >> 22) & 1;
		Cache[i].data_valid[1] = (icht >> 23) & 1;
	}
}


INLINE uint32 V810::RDCACHE(v810_timestamp_t &timestamp, uint32 addr)
{
	const int CI = (addr >> 3) & 0x7F;
	const int SBI = (addr & 4) >> 2;

	if(Cache[CI].tag == (addr >> 10))
	{
		if(!Cache[CI].data_valid[SBI])
		{
			timestamp += 2;       // or higher?  Penalty for cache miss seems to be higher than having cache disabled.
			if(MemReadBus32[addr >> 24])
				Cache[CI].data[SBI] = MemRead32(timestamp, addr & ~0x3);
			else
			{
				timestamp++;
				uint32 tmp;
				tmp = MemRead16(timestamp, addr & ~0x3);
				tmp |= MemRead16(timestamp, (addr & ~0x3) | 0x2) << 16;
				Cache[CI].data[SBI] = tmp;
			}
			Cache[CI].data_valid[SBI] = true;
		}
	}
	else
	{
		Cache[CI].tag = addr >> 10;

		timestamp += 2;	// or higher?  Penalty for cache miss seems to be higher than having cache disabled.
		if(MemReadBus32[addr >> 24])
			Cache[CI].data[SBI] = MemRead32(timestamp, addr & ~0x3);
		else
		{
			timestamp++;
			uint32 tmp;
			tmp = MemRead16(timestamp, addr & ~0x3);
			tmp |= MemRead16(timestamp, (addr & ~0x3) | 0x2) << 16;

			Cache[CI].data[SBI] = tmp;
		}
		Cache[CI].data_valid[SBI] = true;
		Cache[CI].data_valid[SBI ^ 1] = false;
	}
	return(Cache[CI].data[SBI]);
}

INLINE uint16 V810::RDOP(v810_timestamp_t &timestamp, uint32 addr, uint32 meow)
{
	uint16 ret;

	if(S_REG[CHCW] & 0x2)
	{
		uint32 d32 = RDCACHE(timestamp, addr);
		ret = d32 >> ((addr & 2) * 8);
	}
	else
	{
		timestamp += meow; //++;
		ret = MemRead16(timestamp, addr);
	}
	return(ret);
}

#define BRANCH_ALIGN_CHECK(x)	{ if((S_REG[CHCW] & 0x2) && (x & 0x2)) { ADDCLOCK(1); } }

// Reinitialize the defaults in the CPU
void V810::Reset() 
{
	memset(&Cache, 0, sizeof(Cache));

	memset(P_REG, 0, sizeof(P_REG));
	memset(S_REG, 0, sizeof(S_REG));
	memset(Cache, 0, sizeof(Cache));

	P_REG[0]      =  0x00000000;
	SetPC(0xFFFFFFF0);

	S_REG[ECR]    =  0x0000FFF0;
	S_REG[PSW]    =  0x00008000;

	S_REG[PIR]    =  0x00008100;

	S_REG[TKCW]   =  0x000000E0;
	Halted = HALT_NONE;
	ilevel = -1;

	lastop = 0;

	in_bstr = false;

	RecalcIPendingCache();
}

bool V810::Init(V810_Emu_Mode mode)
{
	EmuMode = mode;

	in_bstr = false;
	in_bstr_to = 0;

	if(mode == V810_EMU_MODE_FAST)
	{
		memset(DummyRegion, 0, V810_FAST_MAP_PSIZE);

		for(unsigned int i = V810_FAST_MAP_PSIZE; i < V810_FAST_MAP_PSIZE + V810_FAST_MAP_TRAMPOLINE_SIZE; i += 2)
		{
			DummyRegion[i + 0] = 0;
			DummyRegion[i + 1] = 0x36 << 2;
		}

		for(uint64 A = 0; A < (1ULL << 32); A += V810_FAST_MAP_PSIZE)
			FastMap[A / V810_FAST_MAP_PSIZE] = DummyRegion - A;
	}

 return(true);
}

void V810::Kill(void)
{
	FastMapAllocList.clear();
}

void V810::SetInt(int level)
{
	ilevel = level;
	RecalcIPendingCache();
}

uint8 *V810::SetFastMap(uint32 addresses[], uint32 length, unsigned int num_addresses, const char *name)
{
	FastMapAllocList.emplace_back(std::unique_ptr<uint8[]>(new uint8[length + V810_FAST_MAP_TRAMPOLINE_SIZE]));
	uint8* ret = FastMapAllocList.back().get();

	for(unsigned int i = length; i < length + V810_FAST_MAP_TRAMPOLINE_SIZE; i += 2)
	{
		ret[i + 0] = 0;
		ret[i + 1] = 0x36 << 2;
	}

	for(unsigned int i = 0; i < num_addresses; i++)
	{  
		for(uint64 addr = addresses[i]; addr != (uint64)addresses[i] + length; addr += V810_FAST_MAP_PSIZE)
		{
			FastMap[addr / V810_FAST_MAP_PSIZE] = ret - addresses[i];
		}
	}

	return ret;
}


void V810::SetMemReadBus32(uint8 A, bool value)
{
	MemReadBus32[A] = value;
}

void V810::SetMemWriteBus32(uint8 A, bool value)
{
	MemWriteBus32[A] = value;
}

void V810::SetMemReadHandlers(uint8 MDFN_FASTCALL (*read8)(v810_timestamp_t &, uint32), uint16 MDFN_FASTCALL (*read16)(v810_timestamp_t &, uint32), uint32 MDFN_FASTCALL (*read32)(v810_timestamp_t &, uint32))
{
	MemRead8 = read8;
	MemRead16 = read16;
	MemRead32 = read32;
}

void V810::SetMemWriteHandlers(void MDFN_FASTCALL (*write8)(v810_timestamp_t &, uint32, uint8), void MDFN_FASTCALL (*write16)(v810_timestamp_t &, uint32, uint16), void MDFN_FASTCALL (*write32)(v810_timestamp_t &, uint32, uint32))
{
	MemWrite8 = write8;
	MemWrite16 = write16;
	MemWrite32 = write32;
}

void V810::SetIOReadHandlers(uint8 MDFN_FASTCALL (*read8)(v810_timestamp_t &, uint32), uint16 MDFN_FASTCALL (*read16)(v810_timestamp_t &, uint32), uint32 MDFN_FASTCALL (*read32)(v810_timestamp_t &, uint32))
{
	IORead8 = read8;
	IORead16 = read16;
	IORead32 = read32;
}

void V810::SetIOWriteHandlers(void MDFN_FASTCALL (*write8)(v810_timestamp_t &, uint32, uint8), void MDFN_FASTCALL (*write16)(v810_timestamp_t &, uint32, uint16), void MDFN_FASTCALL (*write32)(v810_timestamp_t &, uint32, uint32))
{
	IOWrite8 = write8;
	IOWrite16 = write16;
	IOWrite32 = write32;
}


INLINE void V810::SetFlag(uint32 n, bool condition)
{
	S_REG[PSW] &= ~n;
	if(condition)
		S_REG[PSW] |= n;
}
	
INLINE void V810::SetSZ(uint32 value)
{
	SetFlag(PSW_Z, !value);
	SetFlag(PSW_S, value & 0x80000000);
}

#define SetPREG(n, val) { P_REG[n] = val; }

INLINE void V810::SetSREG(v810_timestamp_t &timestamp, unsigned int which, uint32 value)
{
	switch(which)
	{
		default:	// Reserved
		//printf("LDSR to reserved system register: 0x%02x : 0x%08x\n", which, value);
		break;

		case ECR:      // Read-only
		break;

		case PIR:      // Read-only (obviously)
		break;

		case TKCW:     // Read-only
		break;

		case EIPSW:
		case FEPSW:
			S_REG[which] = value & 0xFF3FF;
		break;

		case PSW:
			S_REG[which] = value & 0xFF3FF;
			RecalcIPendingCache();
		break;

		case EIPC:
		case FEPC:
			S_REG[which] = value & 0xFFFFFFFE;
		break;

		case ADDTRE:
			S_REG[ADDTRE] = value & 0xFFFFFFFE;
			printf("Address trap(unemulated): %08x\n", value);
		break;

		case CHCW:
			S_REG[CHCW] = value & 0x2;

			switch(value & 0x31)
			{
				default: 
					printf("Undefined cache control bit combination: %08x\n", value);       
				break;

				case 0x00:
				break;

				case 0x01:
					CacheClear(timestamp, (value >> 20) & 0xFFF, (value >> 8) & 0xFFF);
				break;

				case 0x10: 
					CacheDump(timestamp, value & ~0xFF);
				break;

				case 0x20:
					CacheRestore(timestamp, value & ~0xFF);
				break;
			}
		break;
	}
}

INLINE uint32 V810::GetSREG(unsigned int which)
{
	uint32 ret;
	ret = S_REG[which];
	return(ret);
}

#define RB_SETPC(new_pc_raw) 										\
	{										\
		const uint32 new_pc = new_pc_raw;	/* So RB_SETPC(RB_GETPC()) won't mess up */	\
		{										\
			PC_ptr = &FastMap[(new_pc) >> V810_FAST_MAP_SHIFT][(new_pc)];		\
			PC_base = PC_ptr - (new_pc);						\
		}										\
	}

#define RB_PCRELCHANGE(delta) { 				\
		{				\
			uint32 PC_tmp = RB_GETPC();	\
			PC_tmp += (delta);		\
			RB_SETPC(PC_tmp);		\
		}					\
	}

#define RB_INCPCBY2()	{ PC_ptr += 2; }
#define RB_INCPCBY4()   { PC_ptr += 4; }

#define RB_DECPCBY2()   { PC_ptr -= 2; }
#define RB_DECPCBY4()   { PC_ptr -= 4; }

//
// Define fast mode defines
//
#define RB_GETPC()      	((uint32)(PC_ptr - PC_base))

#define RB_RDOP(PC_offset, ...) MDFN_de16lsb<true>(&PC_ptr[PC_offset])

void V810::Run_Fast(int32 MDFN_FASTCALL (*event_handler)(const v810_timestamp_t timestamp))
{
	#define RB_ADDBT(n,o,p)
	#define RB_CPUHOOK(n)

	#include "v810_oploop.inc"

	#undef RB_CPUHOOK
	#undef RB_ADDBT
}

//
// Undefine fast mode defines
//
#undef RB_GETPC
#undef RB_RDOP

v810_timestamp_t V810::Run(int32 MDFN_FASTCALL (*event_handler)(const v810_timestamp_t timestamp))
{
	Running = true;
	Run_Fast(event_handler);
	return(v810_timestamp);
}

void V810::Exit(void)
{
	Running = false;
}

uint32 V810::GetRegister(unsigned int which, char *special, const uint32 special_len)
{
	if(which >= GSREG_PR && which <= GSREG_PR + 31)
	{
		return GetPR(which - GSREG_PR);
	}
	else if(which >= GSREG_SR && which <= GSREG_SR + 31)
	{
		uint32 val = GetSREG(which - GSREG_SR);

		if(special && which == GSREG_SR + PSW)
		{
			snprintf(special, special_len, "Z: %d, S: %d, OV: %d, CY: %d, ID: %d, AE: %d, EP: %d, NP: %d, IA: %2d",
			(int)(bool)(val & PSW_Z), (int)(bool)(val & PSW_S), (int)(bool)(val & PSW_OV), (int)(bool)(val & PSW_CY),
			(int)(bool)(val & PSW_ID), (int)(bool)(val & PSW_AE), (int)(bool)(val & PSW_EP), (int)(bool)(val & PSW_NP),
			(val & PSW_IA) >> 16);
		}
		return val;
	}
	else if(which == GSREG_PC)
	{
		return GetPC();
	}
	else if(which == GSREG_TIMESTAMP)
	{
		return v810_timestamp;
	}
	return 0xDEADBEEF;
}

void V810::SetRegister(unsigned int which, uint32 value)
{
	if(which >= GSREG_PR && which <= GSREG_PR + 31)
	{
		if(which)
		P_REG[which - GSREG_PR] = value;
	}
	else if(which == GSREG_PC)
	{
		SetPC(value & ~1);
	}
}

uint32 V810::GetPC(void)
{
	return(PC_ptr - PC_base);
}

void V810::SetPC(uint32 new_pc)
{
	PC_ptr = &FastMap[new_pc >> V810_FAST_MAP_SHIFT][new_pc];
	PC_base = PC_ptr - new_pc;
}

#define BSTR_OP_MOV dst_cache &= ~(1 << dstoff); dst_cache |= ((src_cache >> srcoff) & 1) << dstoff;
#define BSTR_OP_NOT dst_cache &= ~(1 << dstoff); dst_cache |= (((src_cache >> srcoff) & 1) ^ 1) << dstoff;

#define BSTR_OP_XOR dst_cache ^= ((src_cache >> srcoff) & 1) << dstoff;
#define BSTR_OP_OR dst_cache |= ((src_cache >> srcoff) & 1) << dstoff;
#define BSTR_OP_AND dst_cache &= ~((((src_cache >> srcoff) & 1) ^ 1) << dstoff);

#define BSTR_OP_XORN dst_cache ^= (((src_cache >> srcoff) & 1) ^ 1) << dstoff;
#define BSTR_OP_ORN dst_cache |= (((src_cache >> srcoff) & 1) ^ 1) << dstoff;
#define BSTR_OP_ANDN dst_cache &= ~(((src_cache >> srcoff) & 1) << dstoff);

INLINE uint32 V810::BSTR_RWORD(v810_timestamp_t &timestamp, uint32 A)
{
	if(MemReadBus32[A >> 24])
	{
		timestamp += 2;
		return(MemRead32(timestamp, A));
	}
	else
	{
		uint32 ret;
		timestamp += 2;
		ret = MemRead16(timestamp, A);
 
		timestamp += 2;
		ret |= MemRead16(timestamp, A | 2) << 16;
		return(ret);
	}
}

INLINE void V810::BSTR_WWORD(v810_timestamp_t &timestamp, uint32 A, uint32 V)
{
	if(MemWriteBus32[A >> 24])
	{
		timestamp += 2;
		MemWrite32(timestamp, A, V);
	}
	else
	{
		timestamp += 2;
		MemWrite16(timestamp, A, V & 0xFFFF);

		timestamp += 2;
		MemWrite16(timestamp, A | 2, V >> 16);
	}
}

#define DO_BSTR(op) { 						\
		while(len)					\
		{						\
			if(!have_src_cache)                            \
			{                                              \
				have_src_cache = true;			\
				src_cache = BSTR_RWORD(timestamp, src);       \
			}                                              \
								\
			if(!have_dst_cache)				\
			{						\
				have_dst_cache = true;			\
				dst_cache = BSTR_RWORD(timestamp, dst);       \
			}                                              \
								\
			op;						\
			srcoff = (srcoff + 1) & 0x1F;			\
			dstoff = (dstoff + 1) & 0x1F;			\
			len--;						\
								\
			if(!srcoff)					\
			{                                              \
				src += 4;					\
				have_src_cache = false;			\
			}                                              \
								\
			if(!dstoff)                                    \
			{                                              \
				BSTR_WWORD(timestamp, dst, dst_cache);        \
				dst += 4;                                     \
				have_dst_cache = false;			\
				if(timestamp >= next_event_ts)		\
					break;					\
			}                                              \
		}						\
			if(have_dst_cache)				\
			BSTR_WWORD(timestamp, dst, dst_cache);		\
		}

INLINE bool V810::Do_BSTR_Search(v810_timestamp_t &timestamp, const int inc_mul, unsigned int bit_test)
{
	uint32 srcoff = (P_REG[27] & 0x1F);
	uint32 len = P_REG[28];
	uint32 bits_skipped = P_REG[29];
	uint32 src = (P_REG[30] & 0xFFFFFFFC);
	bool found = false;

	while(len)
	{
		if(!have_src_cache)
		{
			have_src_cache = true;
			timestamp++;
			src_cache = BSTR_RWORD(timestamp, src);
		}

		if(((src_cache >> srcoff) & 1) == bit_test)
		{
			found = true;

			/* Fix the bit offset and word address to "1 bit before" it was found */
			srcoff -= inc_mul * 1;
			if(srcoff & 0x20)		/* Handles 0x1F->0x20(0x00) and 0x00->0xFFFF... */
			{
				src -= inc_mul * 4;
				srcoff &= 0x1F;
			}
			break;
		}
		srcoff = (srcoff + inc_mul * 1) & 0x1F;
		bits_skipped++;
		len--;

		if(!srcoff)
		{
			have_src_cache = false;
			src += inc_mul * 4;
			if(timestamp >= next_event_ts)
			break;
		}
	}

	P_REG[27] = srcoff;
	P_REG[28] = len;
	P_REG[29] = bits_skipped;
	P_REG[30] = src;


	if(found)               // Set Z flag to 0 if the bit was found
		SetFlag(PSW_Z, 0);
	else if(!len)           // ...and if the search is over, and the bit was not found, set it to 1
		SetFlag(PSW_Z, 1);

	if(found)               // Bit found, so don't continue the search.
		return(false);

	return((bool)len);      // Continue the search if any bits are left to search.
}

bool V810::bstr_subop(v810_timestamp_t &timestamp, int sub_op, int arg1)
{
	if(sub_op & 0x08)
	{
		uint32 dstoff = (P_REG[26] & 0x1F);
		uint32 srcoff = (P_REG[27] & 0x1F);
		uint32 len =     P_REG[28];
		uint32 dst =    (P_REG[29] & 0xFFFFFFFC);
		uint32 src =    (P_REG[30] & 0xFFFFFFFC);

		switch(sub_op)
		{
			case ORBSU: DO_BSTR(BSTR_OP_OR); break;

			case ANDBSU: DO_BSTR(BSTR_OP_AND); break;

			case XORBSU: DO_BSTR(BSTR_OP_XOR); break;

			case MOVBSU: DO_BSTR(BSTR_OP_MOV); break;

			case ORNBSU: DO_BSTR(BSTR_OP_ORN); break;

			case ANDNBSU: DO_BSTR(BSTR_OP_ANDN); break;

			case XORNBSU: DO_BSTR(BSTR_OP_XORN); break;

			case NOTBSU: DO_BSTR(BSTR_OP_NOT); break;
		}
        P_REG[26] = dstoff; 
        P_REG[27] = srcoff;
        P_REG[28] = len;
        P_REG[29] = dst;
        P_REG[30] = src;
        
		return((bool)P_REG[28]);
	}
	else
	{
		printf("BSTR Search: %02x\n", sub_op);
		return(Do_BSTR_Search(timestamp, ((sub_op & 1) ? -1 : 1), (sub_op & 0x2) >> 1));
	}
	return(false);
}

INLINE void V810::SetFPUOPNonFPUFlags(uint32 result)
{
		// Now, handle flag setting
		SetFlag(PSW_OV, 0);
		if(!(result & 0x7FFFFFFF)) // Check to see if exponent and mantissa are 0
		{
			 // If Z flag is set, S and CY should be clear, even if it's negative 0(confirmed on real thing with subf.s, at least).
			SetFlag(PSW_Z, 1);
			SetFlag(PSW_S, 0);
			SetFlag(PSW_CY, 0);
		}
		else
		{
			SetFlag(PSW_Z, 0);
			SetFlag(PSW_S, result & 0x80000000);
			SetFlag(PSW_CY, result & 0x80000000);
		}
}

bool V810::IsSubnormal(uint32 fpval)
{
	if( ((fpval >> 23) & 0xFF) == 0 && (fpval & ((1 << 23) - 1)) )
		return(true);
	return(false);
}

INLINE void V810::FPU_Math_Template(uint32 (V810_FP_Ops::*func)(uint32, uint32), uint32 arg1, uint32 arg2)
{
	uint32 result;

	fpo.clear_flags();
	result = (fpo.*func)(P_REG[arg1], P_REG[arg2]);

	SetFPUOPNonFPUFlags(result);
	SetPREG(arg1, result);
}

void V810::fpu_subop(v810_timestamp_t &timestamp, int sub_op, int arg1, int arg2)
{
	uint32 result;
	
	switch(sub_op) 
	{
		case CVT_WS: 
			timestamp += 5;

			fpo.clear_flags();
			result = fpo.itof(P_REG[arg2]);

			SetPREG(arg1, result);
			SetFPUOPNonFPUFlags(result);
		break;	// End CVT.WS

		case CVT_SW:
			timestamp += 8;

			fpo.clear_flags();
			result = fpo.ftoi(P_REG[arg2], false);

			SetPREG(arg1, result);
			SetFlag(PSW_OV, 0);
			SetSZ(result);
		break;	// End CVT.SW

		case ADDF_S: 
			timestamp += 8;
		     FPU_Math_Template(&V810_FP_Ops::add, arg1, arg2);
		break;

		case SUBF_S: 
			timestamp += 11;
		     FPU_Math_Template(&V810_FP_Ops::sub, arg1, arg2);
		break;

        case CMPF_S: 
			timestamp += 6;
			// Don't handle this like subf.s because the flags
			// have slightly different semantics(mostly regarding underflow/subnormal results) (confirmed on real V810).
			fpo.clear_flags();
			result = fpo.cmp(P_REG[arg1], P_REG[arg2]);
		    SetFPUOPNonFPUFlags(result);
		break;

		case MULF_S: 
			timestamp += 7;
			FPU_Math_Template(&V810_FP_Ops::mul, arg1, arg2);
		break;

		case DIVF_S:
			timestamp += 43;
			FPU_Math_Template(&V810_FP_Ops::div, arg1, arg2);
		break;

		case TRNC_SW:
			timestamp += 7;
			
			fpo.clear_flags();
			result = fpo.ftoi(P_REG[arg2], true);

			SetPREG(arg1, result);
			SetFlag(PSW_OV, 0);
			SetSZ(result);
		break;	// end TRNC.SW
	}
}

void V810::StateAction(StateMem *sm, const unsigned load, const bool data_only)
{
	PODFastVector<uint32> cache_tag_temp;
	PODFastVector<uint32> cache_data_temp;
	PODFastVector<bool> cache_data_valid_temp;

	uint32 PC_tmp = GetPC();
	
	int32 next_event_ts_delta = next_event_ts - v810_timestamp;

	SFORMAT StateRegs[] =
	{
	SFARRAY32(P_REG, 32),
	SFARRAY32(S_REG, 32),
	SFVARN(PC_tmp, "PC"),
	SFVAR(Halted),

	SFVAR(lastop),

	SFARRAY32N(&cache_tag_temp[0], cache_tag_temp.size(), "cache_tag_temp"),
	SFARRAY32N(&cache_data_temp[0], cache_data_temp.size(), "cache_data_temp"),
	SFARRAYBN(&cache_data_valid_temp[0], cache_data_valid_temp.size(), "cache_data_valid_temp"),

	SFVAR(ilevel),		// Perhaps remove in future?
	SFVAR(next_event_ts_delta),

	// Bitstring stuff:
	SFVAR(src_cache),
	SFVAR(dst_cache),
	SFVAR(have_src_cache),
	SFVAR(have_dst_cache),
	SFVAR(in_bstr),
	SFVAR(in_bstr_to),

	SFEND
	};

	MDFNSS_StateAction(sm, load, data_only, StateRegs, "V810");

	if(load)
	{
		// std::max is sanity check for a corrupted save state to not crash emulation,
		// std::min<int64>(0x7FF... is a sanity check and for the case where next_event_ts is set to an extremely large value to
		// denote that it's not happening anytime soon, which could cause an overflow if our current timestamp is larger
		// than what it was when the state was saved.
		next_event_ts = std::max<int64>(v810_timestamp, std::min<int64>(0x7FFFFFFF, (int64)v810_timestamp + next_event_ts_delta));

		RecalcIPendingCache();

		SetPC(PC_tmp);
	}
}

/******************************************************************************/
/* Mednafen - Multi-system Emulator                                           */
/******************************************************************************/
/* v810_fp_ops.cpp:
**  Copyright (C) 2014-2016 Mednafen Team
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

#include "v810_fp_ops.h"

bool V810_FP_Ops::fp_is_zero(uint32 v)
{
	return((v & 0x7FFFFFFF) == 0);
}

bool V810_FP_Ops::fp_is_inf_nan_sub(uint32 v)
{
	if((v & 0x7FFFFFFF) == 0)
		return(false);

	switch((v >> 23) & 0xFF)
	{
		case 0x00:
		case 0xff:
			return(true);
	}
	return(false);
}

void V810_FP_Ops::fpim_decode(fpim* df, uint32 v)
{
	df->exp = ((v >> 23) & 0xFF) - 127;
	df->f = (v & 0x7FFFFF) | ((v & 0x7FFFFFFF) ? 0x800000 : 0);
	df->sign = v >> 31;
}

void V810_FP_Ops::fpim_round(fpim* df)
{
	int vbc = 64 - MDFN_lzcount64(df->f);

	if(vbc > 24)
	{
		const unsigned sa = vbc - 24;
		uint64 old_f = df->f;

		df->f = (df->f + ((df->f >> sa) & 1) + ((1ULL << (sa - 1)) - 1)) & ~((1ULL << sa) - 1);
	}
}

void V810_FP_Ops::fpim_round_int(fpim* df, bool truncate)
{
	if(df->exp < 23)
	{
		const unsigned sa = 23 - df->exp;
		uint64 old_f = df->f;

		if(sa > 24)
			df->f = 0;
		else
		{
			if(truncate)
				df->f = df->f & ~((1ULL << sa) - 1);
			else
				df->f = (df->f + ((df->f >> sa) & 1) + ((1ULL << (sa - 1)) - 1)) & ~((1ULL << sa) - 1);
		}
	}
}

uint32 V810_FP_Ops::fpim_encode(fpim* df)
{
	const int lzc = MDFN_lzcount64(df->f);
	int tmp_exp = df->exp - lzc;
	uint64 tmp_walrus = df->f << (lzc & 0x3F);
	int tmp_sign = df->sign;

	tmp_exp += 40;
	tmp_walrus >>= 40;

	if(tmp_walrus == 0)
		tmp_exp = -127;
	else if(tmp_exp <= -127)
	{
		tmp_exp = -127;
		tmp_walrus = 0;
	}
	else if(tmp_exp >= 128)
	{
		tmp_exp -= 192;
	}
	return (tmp_sign << 31) | ((tmp_exp + 127) << 23) | (tmp_walrus & 0x7FFFFF);
}

uint32 V810_FP_Ops::mul(uint32 a, uint32 b)
{
	fpim ins[2];
	fpim res;
	
	fpim_decode(&ins[0], a);
	fpim_decode(&ins[1], b);

	res.exp = ins[0].exp + ins[1].exp - 23;
	res.f = ins[0].f * ins[1].f;
	res.sign = ins[0].sign ^ ins[1].sign;

	fpim_round(&res);

	return fpim_encode(&res);
}

uint32 V810_FP_Ops::add(uint32 a, uint32 b)
{
	fpim ins[2];
	fpim res;
	int64 ft[2];
	int64 tr;
	int max_exp;

	if(a == b && !(a & 0x7FFFFFFF))
	{
		return(a & 0x80000000);
	}

	fpim_decode(&ins[0], a);
	fpim_decode(&ins[1], b);

	max_exp = std::max<int>(ins[0].exp, ins[1].exp);


	for(uint8_t i = 0; i < 2; i++)
	{
		unsigned sd = (max_exp - ins[i].exp);
	
		ft[i] = ins[i].f << 24;

		if(sd >= 48)
		{
			if(ft[i] != 0)
			ft[i] = 1;
		}
		else
		{
			int64 nft = ft[i] >> sd;

			if(ft[i] != (nft << sd))
				nft |= 1;
		ft[i] = nft;
		}

		if(ins[i].sign)
			ft[i] = -ft[i];
	}

	tr = ft[0] + ft[1];
	if(tr < 0)
	{
		tr = -tr;
		res.sign = true;
	}
	else
		res.sign = false;

	res.f = tr;
	res.exp = max_exp - 24;

	fpim_round(&res);

	return fpim_encode(&res);
}

uint32 V810_FP_Ops::sub(uint32 a, uint32 b)
{
	return add(a, b ^ 0x80000000);
}

uint32 V810_FP_Ops::div(uint32 a, uint32 b)
{
	fpim ins[2];
	fpim res;
	uint64 mtmp;

	fpim_decode(&ins[0], a);
	fpim_decode(&ins[1], b);

	res.sign = ins[0].sign ^ ins[1].sign;

	if(ins[1].f == 0)
	{
		return((res.sign << 31) | (255 << 23));
	}
	else
	{
		res.exp = ins[0].exp - ins[1].exp - 2 - 1; // + 23 - 2;
		res.f = ((ins[0].f << 24) / ins[1].f) << 2;
		mtmp = ((ins[0].f << 24) % ins[1].f) << 1;

		if(mtmp > ins[1].f)
			res.f |= 3;
		else if(mtmp == ins[1].f)
			res.f |= 2;
		else if(mtmp > 0)
		res.f |= 1;
	}

	fpim_round(&res);

	return fpim_encode(&res);
}

int V810_FP_Ops::cmp(uint32 a, uint32 b)
{
	fpim ins[2];

	fpim_decode(&ins[0], a);
	fpim_decode(&ins[1], b);

	if(ins[0].exp > ins[1].exp)
		return(ins[0].sign ? -1 : 1);

	if(ins[0].exp < ins[1].exp)
		return(ins[1].sign ? 1 : -1);

	if(ins[0].f > ins[1].f)
		return(ins[0].sign ? -1 : 1);

	if(ins[0].f < ins[1].f)
		return(ins[1].sign ? 1 : -1);

	if((ins[0].sign ^ ins[1].sign) && ins[0].f != 0)
		return(ins[0].sign ? -1 : 1);

	return(0);
}

uint32 V810_FP_Ops::itof(uint32 v)
{
	fpim res;

	res.sign = (bool)(v & 0x80000000);
	res.exp = 23;
	res.f = res.sign ? (0x80000000 - (v & 0x7FFFFFFF)) : (v & 0x7FFFFFFF);

	fpim_round(&res);
	
	return fpim_encode(&res);
}


uint32 V810_FP_Ops::ftoi(uint32 v, bool truncate)
{
	fpim ins;
	int sa;
	int ret;

	fpim_decode(&ins, v);
	fpim_round_int(&ins, truncate);

	sa = ins.exp - 23;

	if(sa < 0)
	{
		if(sa <= -32)
			ret = 0;
		else
			ret = ins.f >> -sa;
	}
	else
	{
		if(sa >= 8)
		{
			if(sa == 8 && ins.f == 0x800000 && ins.sign)
				return(0x80000000);
			else
				ret = ~0U;
		}
		else
		{
			ret = ins.f << sa;
		}
	}

	if(ins.sign)
		ret = -ret;

	return(ret);
}


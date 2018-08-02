/******************************************************************************/
/* Mednafen - Multi-system Emulator                                           */
/******************************************************************************/
/* surface.cpp:
**  Copyright (C) 2009-2016 Mednafen Team
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

#include <mednafen/mednafen.h>
#include "surface.h"

MDFN_PixelFormat::MDFN_PixelFormat()
{
	bpp = 0;
	colorspace = 0;

	Rshift = 0;
	Gshift = 0;
	Bshift = 0;
	Ashift = 0;

	Rprec = 0;
	Gprec = 0;
	Bprec = 0;
	Aprec = 0;
}

MDFN_PixelFormat::MDFN_PixelFormat(const unsigned int p_colorspace, const uint8 p_rs, const uint8 p_gs, const uint8 p_bs, const uint8 p_as)
{
	bpp = 32;
	colorspace = p_colorspace;

	Rshift = p_rs;
	Gshift = p_gs;
	Bshift = p_bs;
	Ashift = p_as;

	Rprec = 8;
	Gprec = 8;
	Bprec = 8;
	Aprec = 8;
}

MDFN_Surface::MDFN_Surface()
{
	memset(&format, 0, sizeof(format));

	pixels = NULL;
	pixels8 = NULL;
	pixels16 = NULL;
	palette = NULL;
	pixels_is_external = false;
	pitchinpix = 0;
	w = 0;
	h = 0;
}

MDFN_Surface::MDFN_Surface(void *const p_pixels, const uint32 p_width, const uint32 p_height, const uint32 p_pitchinpix, const MDFN_PixelFormat &nf, const bool alloc_init_pixels)
{
	Init(p_pixels, p_width, p_height, p_pitchinpix, nf, alloc_init_pixels);
}

void MDFN_Surface::Init(void *const p_pixels, const uint32 p_width, const uint32 p_height, const uint32 p_pitchinpix, const MDFN_PixelFormat &nf, const bool alloc_init_pixels)
{
	void *rpix = NULL;
	assert(nf.bpp == 8 || nf.bpp == 16 || nf.bpp == 32);

	format = nf;

	pixels16 = NULL;
	pixels8 = NULL;
	pixels = NULL;
	palette = NULL;
	
	pixels_is_external = false;

	if(p_pixels)
	{
		rpix = p_pixels;
		pixels_is_external = true;
	}
	else
	{
		if(alloc_init_pixels)
			rpix = calloc(1, p_pitchinpix * p_height * (nf.bpp / 8));
		else
			rpix = malloc(p_pitchinpix * p_height * (nf.bpp / 8));

		if(!rpix)
		{
			ErrnoHolder ene(errno);
			throw(MDFN_Error(ene.Errno(), "%s", ene.StrError()));
		}
	}

	if(nf.bpp == 8)
	{
		if(!(palette = (MDFN_PaletteEntry*) calloc(sizeof(MDFN_PaletteEntry), 256)))
		{
			ErrnoHolder ene(errno);
			if(!pixels_is_external)
				free(rpix);

			throw(MDFN_Error(ene.Errno(), "%s", ene.StrError()));
		}
	}

	if(nf.bpp == 16)
		pixels16 = (uint16 *)rpix;
	else if(nf.bpp == 8)
		pixels8 = (uint8 *)rpix;
	else
		pixels = (uint32 *)rpix;

	w = p_width;
	h = p_height;

	pitchinpix = p_pitchinpix;
}

// When we're converting, only convert the w*h area(AKA leave the last part of the line, pitch32 - w, alone),
// for places where we store auxillary information there(graphics viewer in the debugger), and it'll be faster
// to boot.
void MDFN_Surface::SetFormat(const MDFN_PixelFormat &nf, bool convert)
{
	format = nf;
}

void MDFN_Surface::Fill(uint8 r, uint8 g, uint8 b, uint8 a)
{
	/*uint32 color = MakeColor(r, g, b, a);

	if(format.bpp == 8)
	{
		assert(pixels8);
		MDFN_FastArraySet(pixels8, color, pitchinpix * h);
	}
	else if(format.bpp == 16)
	{
		assert(pixels16);
		MDFN_FastArraySet(pixels16, color, pitchinpix * h);
	}
	else
	{
		assert(pixels);
		MDFN_FastArraySet(pixels, color, pitchinpix * h);
	}*/
}

MDFN_Surface::~MDFN_Surface()
{
	if(!pixels_is_external)
	{
		if(pixels)
			free(pixels);
		if(pixels16)
			free(pixels16);
		if(pixels8)
			free(pixels8);
		if(palette)
			free(palette);
	}
}

/* Mednafen - Multi-system Emulator
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


#include "main.h"

#ifdef WIN32
#include <windows.h>
#endif

#include <trio/trio.h>

#include "video.h"
#include "nongl.h"

#include "icon.h"

#include "debugger.h"
#include "fps.h"
#include "video-state.h"

#ifdef WANT_FANCY_SCALERS
#include "scalebit.h"
#include "hqxx-common.h"
#include "2xSaI.h"
#endif

class SDL_to_MDFN_Surface_Wrapper : public MDFN_Surface
{
 public:
 INLINE SDL_to_MDFN_Surface_Wrapper(SDL_Surface *sdl_surface) : ss(sdl_surface)
 {
  // Locking should be first thing.
  if(SDL_MUSTLOCK(ss))
   SDL_LockSurface(ss);

  format.bpp = ss->format->BitsPerPixel;
  format.colorspace = MDFN_COLORSPACE_RGB;
  format.Rshift = ss->format->Rshift;
  format.Gshift = ss->format->Gshift;
  format.Bshift = ss->format->Bshift;
  format.Ashift = ss->format->Ashift;

  format.Rprec = 8 - ss->format->Rloss;
  format.Gprec = 8 - ss->format->Gloss;
  format.Bprec = 8 - ss->format->Bloss;
  format.Aprec = 8 - ss->format->Aloss;

  pixels_is_external = true;

  pixels16 = NULL;
  pixels = NULL;

  if(ss->format->BitsPerPixel == 16)
  {
   pixels16 = (uint16*)ss->pixels;
   pitchinpix = ss->pitch >> 1;
  }
  else
  {
   pixels = (uint32*)ss->pixels;
   pitchinpix = ss->pitch >> 2;
  }

  format = MDFN_PixelFormat(MDFN_COLORSPACE_RGB, sdl_surface->format->Rshift, sdl_surface->format->Gshift, sdl_surface->format->Bshift, sdl_surface->format->Ashift);

  w = ss->w;
  h = ss->h;
 }

 INLINE ~SDL_to_MDFN_Surface_Wrapper()
 {
  if(SDL_MUSTLOCK(ss))
   SDL_UnlockSurface(ss);
  ss = NULL;
 }
 private:
 SDL_Surface *ss;
};

enum
{
 VDRIVER_OPENGL = 0,
 VDRIVER_SOFTSDL = 1,
 VDRIVER_OVERLAY = 2
};

enum
{
 NTVB_NONE = 0,

 NTVB_HQ2X,
 NTVB_HQ3X,
 NTVB_HQ4X,

 NTVB_SCALE2X,
 NTVB_SCALE3X,
 NTVB_SCALE4X,

 NTVB_NN2X,
 NTVB_NN3X,
 NTVB_NN4X,

 NTVB_NNY2X,
 NTVB_NNY3X,
 NTVB_NNY4X,

 NTVB_2XSAI,
 NTVB_SUPER2XSAI,
 NTVB_SUPEREAGLE,
};

static const MDFNSetting_EnumList VDriver_List[] =
{
 // Legacy:
 { "0", VDRIVER_SOFTSDL },
 { "sdl", VDRIVER_SOFTSDL, "SDL Surface", gettext_noop("Slower with lower-quality scaling than OpenGL, but if you don't have hardware-accelerated OpenGL rendering, it will be faster than software OpenGL rendering. Bilinear interpolation not available. OpenGL shaders do not work with this output method, of course.") },

 { NULL, 0 },
};

static const MDFNSetting GlobalVideoSettings[] =
{
 { "video.driver", MDFNSF_NOFLAGS, gettext_noop("Video output method/driver."), NULL, MDFNST_ENUM, "sdl", NULL, NULL, NULL, NULL, VDriver_List },

 { "video.fs", MDFNSF_NOFLAGS, gettext_noop("Enable fullscreen mode."), NULL, MDFNST_BOOL, "0", },
 { "video.glvsync", MDFNSF_NOFLAGS, gettext_noop("Attempt to synchronize OpenGL page flips to vertical retrace period."), 
			       gettext_noop("Note: Additionally, if the environment variable \"__GL_SYNC_TO_VBLANK\" does not exist, then it will be created and set to the value specified for this setting.  This has the effect of forcibly enabling or disabling vblank synchronization when running under Linux with NVidia's drivers."),
				MDFNST_BOOL, "1" },

 { "video.disable_composition", MDFNSF_NOFLAGS, gettext_noop("Attempt to disable desktop composition."), gettext_noop("Currently, this setting only has an effect on Windows Vista and Windows 7(and probably the equivalent server versions as well)."), MDFNST_BOOL, "1" },
};

static const MDFNSetting_EnumList StretchMode_List[] =
{
 { "0", 0, gettext_noop("Disabled") },
 { "off", 0 },
 { "none", 0 },

 { "1", 1 },
 { "full", 1, gettext_noop("Full"), gettext_noop("Full-screen stretch, disregarding aspect ratio.") },

 { "2", 2 },
 { "aspect", 2, gettext_noop("Aspect Preserve"), gettext_noop("Full-screen stretch as far as the aspect ratio(in this sense, the equivalent xscalefs == yscalefs) can be maintained.") },

 { "aspect_int", 3, gettext_noop("Aspect Preserve + Integer Scale"), gettext_noop("Full-screen stretch, same as \"aspect\" except that the equivalent xscalefs and yscalefs are rounded down to the nearest integer.") },
 { "aspect_mult2", 4, gettext_noop("Aspect Preserve + Integer Multiple-of-2 Scale"), gettext_noop("Full-screen stretch, same as \"aspect_int\", but rounds down to the nearest multiple of 2.") },

 { NULL, 0 },
};

static const MDFNSetting_EnumList VideoIP_List[] =
{
 { "0", VIDEOIP_OFF, gettext_noop("Disabled") },

 { "1", VIDEOIP_BILINEAR, gettext_noop("Bilinear") },

 // Disabled until a fix can be made for rotation.
 { "x", VIDEOIP_LINEAR_X, gettext_noop("Linear (X)"), gettext_noop("Interpolation only on the X axis.") },
 { "y", VIDEOIP_LINEAR_Y, gettext_noop("Linear (Y)"), gettext_noop("Interpolation only on the Y axis.") },

 { NULL, 0 },
};

static const MDFNSetting_EnumList Special_List[] =
{
    { "0", 	NTVB_NONE },
    { "none", 	NTVB_NONE, "None/Disabled" },

#ifdef WANT_FANCY_SCALERS
    { "hq2x", 	NTVB_HQ2X, "hq2x" },
    { "hq3x", 	NTVB_HQ3X, "hq3x" },
    { "hq4x", 	NTVB_HQ4X, "hq4x" },
    { "scale2x",NTVB_SCALE2X, "scale2x" },
    { "scale3x",NTVB_SCALE3X, "scale3x" },
    { "scale4x",NTVB_SCALE4X, "scale4x" },

    { "2xsai", 	NTVB_2XSAI, "2xSaI" },
    { "super2xsai", NTVB_SUPER2XSAI, "Super 2xSaI" },
    { "supereagle", NTVB_SUPEREAGLE, "Super Eagle" },
#endif

    { "nn2x",	NTVB_NN2X, "Nearest-neighbor 2x" },
    { "nn3x",	NTVB_NN3X, "Nearest-neighbor 3x" },
    { "nn4x",	NTVB_NN4X, "Nearest-neighbor 4x" },
    { "nny2x",	NTVB_NNY2X, "Nearest-neighbor 2x, y axis only" },
    { "nny3x",	NTVB_NNY3X, "Nearest-neighbor 3x, y axis only" }, 
    { "nny4x",	NTVB_NNY4X, "Nearest-neighbor 4x, y axis only" },

    { NULL, 0 },
};

static const MDFNSetting_EnumList Shader_List[] =
{
    { NULL, 0 },
};

static const MDFNSetting_EnumList GoatPat_List[] =
{
	{ NULL, 0 },
};

void Video_MakeSettings(std::vector <MDFNSetting> &settings)
{
 static const char *CSD_xres = gettext_noop("Full-screen horizontal resolution.");
 static const char *CSD_yres = gettext_noop("Full-screen vertical resolution.");
 static const char *CSDE_xres = gettext_noop("A value of \"0\" will cause the desktop horizontal resolution to be used.");
 static const char *CSDE_yres = gettext_noop("A value of \"0\" will cause the desktop vertical resolution to be used.");

 static const char *CSD_xscale = gettext_noop("Scaling factor for the X axis in windowed mode.");
 static const char *CSD_yscale = gettext_noop("Scaling factor for the Y axis in windowed mode.");

 static const char *CSD_xscalefs = gettext_noop("Scaling factor for the X axis in fullscreen mode.");
 static const char *CSD_yscalefs = gettext_noop("Scaling factor for the Y axis in fullscreen mode.");
 static const char *CSDE_xyscalefs = gettext_noop("For this settings to have any effect, the \"<system>.stretch\" setting must be set to \"0\".");

 static const char *CSD_stretch = gettext_noop("Stretch to fill screen.");
 static const char *CSD_videoip = gettext_noop("Enable (bi)linear interpolation.");

 static const char *CSD_special = gettext_noop("Enable specified special video scaler.");
 static const char *CSDE_special = gettext_noop("The destination rectangle is NOT altered by this setting, so if you have xscale and yscale set to \"2\", and try to use a 3x scaling filter like hq3x, the image is not going to look that great. The nearest-neighbor scalers are intended for use with bilinear interpolation enabled, at high resolutions(such as 1280x1024; nn2x(or nny2x) + bilinear interpolation + fullscreen stretching at this resolution looks quite nice).");

 static const char *CSD_shader = gettext_noop("Enable specified OpenGL shader.");
 static const char *CSDE_shader = gettext_noop("Obviously, this will only work with the OpenGL \"video.driver\" setting, and only on cards and OpenGL implementations that support shaders, otherwise you will get a black screen, or Mednafen may display an error message when starting up. When a shader is enabled, the \"<system>.videoip\" setting is ignored.");

 for(unsigned int i = 0; i < MDFNSystems.size() + 1; i++)
 {
  int nominal_width;
  int nominal_height;
  bool multires;
  const char *sysname;
  char default_value[256];
  MDFNSetting setting;
  const int default_xres = 0, default_yres = 0;
  const double default_scalefs = 1.0;
  double default_scale;

  if(i == MDFNSystems.size())
  {
   nominal_width = 384;
   nominal_height = 240;
   multires = FALSE;
   sysname = "player";
  }
  else
  {
   nominal_width = MDFNSystems[i]->nominal_width;
   nominal_height = MDFNSystems[i]->nominal_height;
   multires = MDFNSystems[i]->multires;
   sysname = (const char *)MDFNSystems[i]->shortname;
  }

  default_scale = ceil(1024 / nominal_width);

  if(default_scale * nominal_width > 1024)
   default_scale--;

  if(!default_scale)
   default_scale = 1;

  trio_snprintf(default_value, 256, "%d", default_xres);
  BuildSystemSetting(&setting, sysname, "xres", CSD_xres, CSDE_xres, MDFNST_UINT, strdup(default_value), "0", "65536");
  settings.push_back(setting);

  trio_snprintf(default_value, 256, "%d", default_yres);
  BuildSystemSetting(&setting, sysname, "yres", CSD_yres, CSDE_yres, MDFNST_UINT, strdup(default_value), "0", "65536");
  settings.push_back(setting);

  trio_snprintf(default_value, 256, "%f", default_scale);
  BuildSystemSetting(&setting, sysname, "xscale", CSD_xscale, NULL, MDFNST_FLOAT, strdup(default_value), "0.01", "256");
  settings.push_back(setting);
  BuildSystemSetting(&setting, sysname, "yscale", CSD_yscale, NULL, MDFNST_FLOAT, strdup(default_value), "0.01", "256");
  settings.push_back(setting);

  trio_snprintf(default_value, 256, "%f", default_scalefs);
  BuildSystemSetting(&setting, sysname, "xscalefs", CSD_xscalefs, CSDE_xyscalefs, MDFNST_FLOAT, strdup(default_value), "0.01", "256");
  settings.push_back(setting);
  BuildSystemSetting(&setting, sysname, "yscalefs", CSD_yscalefs, CSDE_xyscalefs, MDFNST_FLOAT, strdup(default_value), "0.01", "256");
  settings.push_back(setting);

  BuildSystemSetting(&setting, sysname, "stretch", CSD_stretch, NULL, MDFNST_ENUM, "aspect_mult2", NULL, NULL, NULL, NULL, StretchMode_List);
  settings.push_back(setting);

  BuildSystemSetting(&setting, sysname, "videoip", CSD_videoip, NULL, MDFNST_ENUM, multires ? "1" : "0", NULL, NULL, NULL, NULL, VideoIP_List);
  settings.push_back(setting);

  BuildSystemSetting(&setting, sysname, "special", CSD_special, CSDE_special, MDFNST_ENUM, "none", NULL, NULL, NULL, NULL, Special_List);
  settings.push_back(setting);
 }

 for(unsigned i = 0; i < sizeof(GlobalVideoSettings) / sizeof(GlobalVideoSettings[0]); i++)
  settings.push_back(GlobalVideoSettings[i]);
}


typedef struct
{
        int xres;
        int yres;
        double xscale, xscalefs;
        double yscale, yscalefs;
        int videoip;
        int stretch;
        int special;
        int scanlines;
} CommonVS;

static CommonVS _video;
static int _fullscreen;

static bool osd_alpha_blend;
static unsigned int vdriver = VDRIVER_SOFTSDL;

static struct ScalerDefinition
{
	int id;
	int xscale;
	int yscale;
} Scalers[] = 
{
	{ NTVB_HQ2X, 2, 2 },
	{ NTVB_HQ3X, 3, 3 },
	{ NTVB_HQ4X, 4, 4 },

	{ NTVB_SCALE2X, 2, 2 },
	{ NTVB_SCALE3X, 3, 3 },
	{ NTVB_SCALE4X, 4, 4 },

	{ NTVB_NN2X, 2, 2 },
        { NTVB_NN3X, 3, 3 },
        { NTVB_NN4X, 4, 4 },

	{ NTVB_NNY2X, 1, 2 },
	{ NTVB_NNY3X, 1, 3 },
	{ NTVB_NNY4X, 1, 4 },

	{ NTVB_2XSAI, 2, 2 },
	{ NTVB_SUPER2XSAI, 2, 2 },
	{ NTVB_SUPEREAGLE, 2, 2 },
};

static MDFNGI *VideoGI;

static bool sdlhaveogl = false;

static int best_xres = 0, best_yres = 0;

static int cur_xres, cur_yres, cur_flags;

static ScalerDefinition *CurrentScaler = NULL;

static SDL_Surface *screen = NULL;

static MDFN_Rect screen_dest_rect;

static MDFN_Surface *SMSurface = NULL;
static MDFN_Rect SMRect;
static MDFN_Rect SMDRect;

static int curbpp;

static double exs,eys;
static int evideoip;

static int NeedClear = 0;
static uint32 LastBBClearTime = 0;

static MDFN_PixelFormat pf_overlay, pf_normal;

static void MarkNeedBBClear(void)
{
 NeedClear = 15;
}

static void ClearBackBuffer(void)
{
	SDL_FillRect(screen, NULL, 0);
}

void Video_Kill(void)
{
	if(SMSurface)
	{
		delete SMSurface;
		SMSurface = NULL;
	}

	screen = NULL;
	VideoGI = NULL;
	cur_xres = 0;
	cur_yres = 0;
	cur_flags = 0;
}


static bool weset_glstvb = false; 
static uint32 real_rs, real_gs, real_bs, real_as;

void Video_Init(MDFNGI *gi)
{
	const SDL_VideoInfo *vinf;
	int flags = 0; //SDL_RESIZABLE;
	int desbpp;

	VideoGI = gi;

	MDFNI_printf(_("Initializing video...\n"));
	MDFN_AutoIndent aindv(1);

	osd_alpha_blend = MDFN_GetSettingB("osd.alpha_blend");

	std::string snp = std::string(gi->shortname) + ".";

	const std::string special_string = MDFN_GetSettingS(snp + std::string("special"));
	const unsigned special_id = MDFN_GetSettingUI(snp + std::string("special"));

	_fullscreen = 0;			;
	_video.xres = MDFN_GetSettingUI(snp + "xres");
	_video.yres = MDFN_GetSettingUI(snp + "yres");
	_video.xscale = 1;
	_video.yscale = 1;
	_video.xscalefs = 1;
	_video.yscalefs = 1;
	_video.videoip = 0;
	_video.stretch = 0;
	_video.scanlines = 0;

	_video.special = special_id;

	CurrentScaler = nullptr;
	for(auto& scaler : Scalers)
	if(_video.special == scaler.id)
		CurrentScaler = &scaler;
	assert(_video.special == NTVB_NONE || CurrentScaler);

	vinf = SDL_GetVideoInfo();

	if(!best_xres)
	{
		best_xres = vinf->current_w;
		best_yres = vinf->current_h;
	}


	if(vinf->hw_available)
		flags |= SDL_HWSURFACE | SDL_DOUBLEBUF;

	vdriver = MDFN_GetSettingI("video.driver");

	exs = _fullscreen ? _video.xscalefs : _video.xscale;
	eys = _fullscreen ? _video.yscalefs : _video.yscale;
	evideoip = _video.videoip;

	desbpp = 16;

	if(!(screen = SDL_SetVideoMode(256, 232, desbpp, SDL_HWSURFACE)))
	{
		throw MDFN_Error(0, "%s", SDL_GetError());
	}

	cur_xres = screen->w;
	cur_yres = screen->h;
	cur_flags = flags;
	curbpp = screen->format->BitsPerPixel;


	MDFN_printf(_("Video Mode: %d x %d x %d bpp\n"),screen->w,screen->h,screen->format->BitsPerPixel);

	MDFN_printf(_("Fullscreen: %s\n"), _fullscreen ? _("Yes") : _("No"));
	MDFN_printf(_("Scanlines: Off\n"));
	MDFN_printf(_("Destination Rectangle: X=%d, Y=%d, W=%d, H=%d\n"), screen_dest_rect.x, screen_dest_rect.y, screen_dest_rect.w, screen_dest_rect.h);

	if(gi && gi->name.size() > 0)
	{
		const char* gics = gi->name.c_str();
		SDL_WM_SetCaption(gics, gics);
	}
	else
		SDL_WM_SetCaption("Mednafen", "Mednafen");

	uint8_t rs, gs, bs, as;

	rs = screen->format->Rshift;
	gs = screen->format->Gshift;
	bs = screen->format->Bshift;

	as = 0;
	while(as == rs || as == gs || as == bs) // Find unused 8-bits to use as our alpha channel
		as += 8;

	//printf("%d %d %d %d\n", rs, gs, bs, as);

	SDL_ShowCursor(0);

	real_rs = rs;
	real_gs = gs;
	real_bs = bs;
	real_as = as;

	{
		int xmu = std::max<int>(1, screen->w / 402);
		int ymu = std::max<int>(1, screen->h / 288);

		SMRect.h = 18 + 2;
		SMRect.x = 0;
		SMRect.y = 0;
		SMRect.w = screen->w;

		SMDRect.w = SMRect.w * xmu;
		SMDRect.h = SMRect.h * ymu;
		SMDRect.x = (screen->w - SMDRect.w) / 2;
		SMDRect.y = screen->h - SMDRect.h;

		if(SMDRect.x < 0)
		{
			SMRect.w += SMDRect.x * 2 / xmu;
			SMDRect.w = SMRect.w * xmu;
			SMDRect.x = 0;
		}
		SMSurface = new MDFN_Surface(NULL, SMRect.w, SMRect.h, SMRect.w, MDFN_PixelFormat(MDFN_COLORSPACE_RGB, real_rs, real_gs, real_bs, real_as));
	}

	//MDFNI_SetPixelFormat(rs, gs, bs, as);
	memset(&pf_normal, 0, sizeof(pf_normal));
	memset(&pf_overlay, 0, sizeof(pf_overlay));

	pf_normal.bpp = desbpp;
	pf_normal.colorspace = MDFN_COLORSPACE_RGB;
	pf_normal.Rshift = rs;
	pf_normal.Gshift = gs;
	pf_normal.Bshift = bs;
	pf_normal.Ashift = as;

	//SetPixelFormatHax((vdriver == VDRIVER_OVERLAY) ? pf_overlay : pf_normal); //rs, gs, bs, as);

	for(int i = 0; i < 2; i++)
	{
		ClearBackBuffer();
		SDL_Flip(screen);
	}

	MarkNeedBBClear();
}

static uint32 howlong = 0;
static char *CurrentMessage = NULL;

void VideoShowMessage(char *text)
{
 if(text)
  howlong = Time::MonoMS() + MDFN_GetSettingUI("osd.message_display_time");
 else
  howlong = 0;

 if(CurrentMessage)
 {
  free(CurrentMessage);
  CurrentMessage = NULL;
 }

 CurrentMessage = text;
}

void BlitRaw(MDFN_Surface *src, const MDFN_Rect *src_rect, const MDFN_Rect *dest_rect, int source_alpha)
{
 {
  SDL_to_MDFN_Surface_Wrapper m_surface(screen);

  //MDFN_SrcAlphaBlitSurface(src, src_rect, &m_surface, dest_rect);
  MDFN_StretchBlitSurface(src, *src_rect, &m_surface, *dest_rect, (source_alpha > 0) && osd_alpha_blend);
 }

 bool cond1 = (dest_rect->x < screen_dest_rect.x || (dest_rect->x + dest_rect->w) > (screen_dest_rect.x + screen_dest_rect.w));
 bool cond2 = (dest_rect->y < screen_dest_rect.y || (dest_rect->y + dest_rect->h) > (screen_dest_rect.y + screen_dest_rect.h));

 if(cond1 || cond2)
  MarkNeedBBClear();
}

static bool IsInternalMessageActive(void)
{
 return(Time::MonoMS() < howlong);
}

static bool BlitInternalMessage(void)
{
 if(Time::MonoMS() >= howlong)
 {
  if(CurrentMessage)
  {
   free(CurrentMessage);
   CurrentMessage = NULL;
  }
  return(0);
 }

 if(CurrentMessage)
 {
  SMSurface->Fill(0x00, 0x00, 0x00, 0xC0);

  DrawTextShadow(SMSurface, 0, 1, CurrentMessage,
	SMSurface->MakeColor(0xFF, 0xFF, 0xFF, 0xFF), SMSurface->MakeColor(0x00, 0x00, 0x00, 0xFF), MDFN_FONT_9x18_18x18, SMRect.w);
  free(CurrentMessage);
  CurrentMessage = NULL;
 }

 BlitRaw(SMSurface, &SMRect, &SMDRect);
 return(1);
}

static bool OverlayOK;	// Set to TRUE when vdriver == "overlay", and it's safe to use an overlay format
			// "Safe" is equal to OSD being off, and not using a special scaler that
			// requires an RGB pixel format(HQnx)
			// Otherwise, set to FALSE.
			// (Set in the BlitScreen function before any calls to SubBlit())

static void SubBlit(const MDFN_Surface *source_surface, const MDFN_Rect &src_rect, const MDFN_Rect &dest_rect, const int InterlaceField)
{
	const MDFN_Surface *eff_source_surface = source_surface;
	MDFN_Rect eff_src_rect = src_rect;
	int overlay_softscale = 0;
	SDL_to_MDFN_Surface_Wrapper m_surface(screen);
	MDFN_StretchBlitSurface(eff_source_surface, eff_src_rect, &m_surface, dest_rect, false, _video.scanlines, &eff_src_rect, CurGame->rotated, InterlaceField);
}

void BlitScreen(MDFN_Surface *msurface, const MDFN_Rect *DisplayRect, const int32 *LineWidths, const int InterlaceField, const bool take_ssnapshot)
{
	MDFN_Rect src_rect;
	const MDFN_PixelFormat *pf_needed = &pf_normal;

	msurface->SetFormat(*pf_needed, TRUE);

	src_rect.x = DisplayRect->x;
	src_rect.w = DisplayRect->w;
	src_rect.y = DisplayRect->y;
	src_rect.h = DisplayRect->h;

	SubBlit(msurface, src_rect, screen_dest_rect, InterlaceField);

	SDL_Flip(screen);
}

void Video_PtoV(const int in_x, const int in_y, int32 *out_x, int32 *out_y)
{
 assert(VideoGI);
 if(VideoGI->rotated)
 {
  double tmp_x, tmp_y;

  // Swap X and Y
  tmp_x = ((double)(in_y - screen_dest_rect.y) / eys);
  tmp_y = ((double)(in_x - screen_dest_rect.x) / exs);

  // Correct position(and movement)
  if(VideoGI->rotated == MDFN_ROTATE90)
   tmp_x = VideoGI->nominal_width - 1 - tmp_x;
  else if(VideoGI->rotated == MDFN_ROTATE270)
   tmp_y = VideoGI->nominal_height - 1 - tmp_y;

  *out_x = (int32)round(65536 * tmp_x);
  *out_y = (int32)round(65536 * tmp_y);
 }
 else
 {
  *out_x = (int32)round(65536 * (double)(in_x - screen_dest_rect.x) / exs);
  *out_y = (int32)round(65536 * (double)(in_y - screen_dest_rect.y) / eys);
 }
}

int32 Video_PtoV_J(const int32 inv, const bool axis, const bool scr_scale)
{
 assert(VideoGI);
 if(!scr_scale)
 {
  return((inv - 32768) * std::max(VideoGI->nominal_width, VideoGI->nominal_height) + (32768 * (axis ? VideoGI->nominal_height : VideoGI->nominal_width)));
 }
 else
 {
  int32 prescale = (axis ? screen->h : screen->w);
  int32 offs = -(axis ? screen_dest_rect.y : screen_dest_rect.x);
  double postscale = 65536.0 / (axis ? eys : exs);

  //printf("%.64f\n", floor(0.5 + ((((((int64)inv * prescale) + 0x8000) >> 16) + offs) * postscale)) / 65536.0);

  return (int32)floor(0.5 + ((((((int64)inv * prescale) + 0x8000) >> 16) + offs) * postscale));
 }
}

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

#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/stat.h>

#include <locale.h>

#ifdef HAVE_GETPWUID
#include <pwd.h>
#endif

#ifdef HAVE_ICONV
#include <iconv.h>
#endif

#include <atomic>

#include "input.h"
#include "Joystick.h"
#include "video.h"
#include "sound.h"
#include "fps.h"
#include "video-state.h"
#include "ers.h"
#include "rmdui.h"
#include <mednafen/tests.h>
#include <mednafen/MemoryStream.h>
#include <mednafen/file.h>


JoystickManager *joy_manager = NULL;
bool MDFNDHaveFocus;
bool pending_save_state, pending_snapshot, pending_ssnapshot, pending_save_movie;
static bool ffnosound;

static const MDFNSetting_EnumList SDriver_List[] =
{
 { "default", -1, "Default", gettext_noop("Selects the default sound driver.") },

 { "alsa", -1, "ALSA", gettext_noop("The default for Linux(if available).") },
 { "openbsd", -1, "OpenBSD Audio", gettext_noop("The default for OpenBSD.") },
 { "oss", -1, "Open Sound System", gettext_noop("The default for non-Linux UN*X/POSIX/BSD(other than OpenBSD) systems, or anywhere ALSA is unavailable. If the ALSA driver gives you problems, you can try using this one instead.\n\nIf you are using OSSv4 or newer, you should edit \"/usr/lib/oss/conf/osscore.conf\", uncomment the max_intrate= line, and change the value from 100(default) to 1000(or higher if you know what you're doing), and restart OSS. Otherwise, performance will be poor, and the sound buffer size in Mednafen will be orders of magnitude larger than specified.\n\nIf the sound buffer size is still excessively larger than what is specified via the \"sound.buffer_time\" setting, you can try setting \"sound.period_time\" to 2666, and as a last resort, 5333, to work around a design flaw/limitation/choice in the OSS API and OSS implementation.") },

 { "wasapish", -1, "WASAPI(Shared Mode)", gettext_noop("The default when it's available(running on Microsoft Windows Vista and newer).") },

 { "dsound", -1, "DirectSound", gettext_noop("The default for Microsoft Windows XP and older.") },

 { "wasapi", -1, "WASAPI(Exclusive Mode)", gettext_noop("Experimental exclusive-mode WASAPI driver, usable on Windows Vista and newer.  Use it for lower-latency sound.  May not work properly on all sound cards.") },

 { "sdl", -1, "Simple Directmedia Layer", gettext_noop("This driver is not recommended, but it serves as a backup driver if the others aren't available. Its performance is generally sub-par, requiring higher latency or faster CPUs/SMP for glitch-free playback, except where the OS provides a sound callback API itself, such as with Mac OS X and BeOS.") },

 { "jack", -1, "JACK", gettext_noop("The latency reported during startup is for the local sound buffer only and does not include server-side latency.  Please note that video card drivers(in the kernel or X), and hardware-accelerated OpenGL, may interfere with jackd's ability to effectively run with realtime response.") },

 { "dummy", -1 },

 { NULL, 0 },
};

static const MDFNSetting_EnumList FontSize_List[] =
{
 { "5x7",	MDFN_FONT_5x7, gettext_noop("5x7") },
 { "6x9",	MDFN_FONT_6x9, gettext_noop("6x9") },
 { "6x12",	MDFN_FONT_6x12, gettext_noop("6x12") },
#ifdef WANT_INTERNAL_CJK
 { "6x13",	MDFN_FONT_6x13_12x13, gettext_noop("6x13.  CJK support.") },
 { "9x18",	MDFN_FONT_9x18_18x18, gettext_noop("9x18;  CJK support.") },
#else
 { "6x13",	MDFN_FONT_6x13_12x13, gettext_noop("6x13.") },
 { "9x18",	MDFN_FONT_9x18_18x18, gettext_noop("9x18.") },
#endif
 // Backwards compat:
 { "xsmall", 	MDFN_FONT_5x7 }, // 4x5 font was removed.
 { "small",	MDFN_FONT_5x7 },
 { "medium",	MDFN_FONT_6x13_12x13 },
 { "large",	MDFN_FONT_9x18_18x18 },

 { "0",		MDFN_FONT_9x18_18x18 },
 { "1",		MDFN_FONT_5x7 },

 { NULL, 0 },
};


static std::vector <MDFNSetting> NeoDriverSettings;
static const MDFNSetting DriverSettings[] =
{
  { "input.joystick.global_focus", MDFNSF_NOFLAGS, gettext_noop("Update physical joystick(s) internal state in Mednafen even when Mednafen lacks OS focus."), NULL, MDFNST_BOOL, "1" },
  { "input.joystick.axis_threshold", MDFNSF_NOFLAGS, gettext_noop("Analog axis binary press detection threshold."), gettext_noop("Threshold for detecting a digital-like \"button\" press on analog axis, in percent."), MDFNST_FLOAT, "75", "0", "100" },
  { "input.autofirefreq", MDFNSF_NOFLAGS, gettext_noop("Auto-fire frequency."), gettext_noop("Auto-fire frequency = GameSystemFrameRateHz / (value + 1)"), MDFNST_UINT, "3", "0", "1000" },
  { "input.ckdelay", MDFNSF_NOFLAGS, gettext_noop("Dangerous key action delay."), gettext_noop("The length of time, in milliseconds, that a button/key corresponding to a \"dangerous\" command like power, reset, exit, etc. must be pressed before the command is executed."), MDFNST_UINT, "0", "0", "99999" },

  { "video.frameskip", MDFNSF_NOFLAGS, gettext_noop("Enable frameskip during emulation rendering."), 
					gettext_noop("Disable for rendering code performance testing."), MDFNST_BOOL, "1" },

  { "video.blit_timesync", MDFNSF_NOFLAGS, gettext_noop("Enable time synchronization(waiting) for frame blitting."),
					gettext_noop("Disable to reduce latency, at the cost of potentially increased video \"juddering\", with the maximum reduction in latency being about 1 video frame's time.\nWill work best with emulated systems that are not very computationally expensive to emulate, combined with running on a relatively fast CPU."),
					MDFNST_BOOL, "1" },

  { "ffspeed", MDFNSF_NOFLAGS, gettext_noop("Fast-forwarding speed multiplier."), NULL, MDFNST_FLOAT, "4", "1", "15" },
  { "fftoggle", MDFNSF_NOFLAGS, gettext_noop("Treat the fast-forward button as a toggle."), NULL, MDFNST_BOOL, "0" },
  { "ffnosound", MDFNSF_NOFLAGS, gettext_noop("Silence sound output when fast-forwarding."), NULL, MDFNST_BOOL, "0" },

  { "sfspeed", MDFNSF_NOFLAGS, gettext_noop("SLOW-forwarding speed multiplier."), NULL, MDFNST_FLOAT, "0.75", "0.25", "1" },
  { "sftoggle", MDFNSF_NOFLAGS, gettext_noop("Treat the SLOW-forward button as a toggle."), NULL, MDFNST_BOOL, "0" },

  { "nothrottle", MDFNSF_NOFLAGS, gettext_noop("Disable speed throttling when sound is disabled."), NULL, MDFNST_BOOL, "0"},
  { "autosave", MDFNSF_NOFLAGS, gettext_noop("Automatic load/save state on game load/save."), gettext_noop("Automatically save and load save states when a game is closed or loaded, respectively."), MDFNST_BOOL, "0"},

#ifdef USE_OSS_DEFAULT
  { "sound.driver", MDFNSF_NOFLAGS, gettext_noop("Select sound driver."), gettext_noop("The following choices are possible, sorted by preference, high to low, when \"default\" driver is used, but dependent on being compiled in."), MDFNST_ENUM, "oss", NULL, NULL, NULL, NULL, SDriver_List },
#endif
#ifdef USE_ALSA_DEFAULT
  { "sound.driver", MDFNSF_NOFLAGS, gettext_noop("Select sound driver."), gettext_noop("The following choices are possible, sorted by preference, high to low, when \"default\" driver is used, but dependent on being compiled in."), MDFNST_ENUM, "alsa", NULL, NULL, NULL, NULL, SDriver_List },
#endif
#ifdef USE_SDL_DEFAULT
  { "sound.driver", MDFNSF_NOFLAGS, gettext_noop("Select sound driver."), gettext_noop("The following choices are possible, sorted by preference, high to low, when \"default\" driver is used, but dependent on being compiled in."), MDFNST_ENUM, "sdl", NULL, NULL, NULL, NULL, SDriver_List },
#endif

  { "sound.device", MDFNSF_NOFLAGS, gettext_noop("Select sound output device."), gettext_noop("When using ALSA sound output under Linux, the \"sound.device\" setting \"default\" is Mednafen's default, IE \"hw:0\", not ALSA's \"default\". If you want to use ALSA's \"default\", use \"sexyal-literal-default\"."), MDFNST_STRING, "default", NULL, NULL },
  { "sound.volume", MDFNSF_NOFLAGS, gettext_noop("Sound volume level, in percent."), gettext_noop("Setting this volume control higher than the default of \"100\" may severely distort the sound."), MDFNST_UINT, "100", "0", "150" },
  { "sound", MDFNSF_NOFLAGS, gettext_noop("Enable sound output."), NULL, MDFNST_BOOL, "1" },
  { "sound.period_time", MDFNSF_NOFLAGS, gettext_noop("Desired period size in microseconds(μs)."), gettext_noop("Currently only affects OSS, ALSA, WASAPI(exclusive mode), and SDL output.  A value of 0 defers to the default in the driver code in SexyAL.\n\nNote: This is not the \"sound buffer size\" setting, that would be \"sound.buffer_time\"."), MDFNST_UINT,  "0", "0", "100000" },
  { "sound.buffer_time", MDFNSF_NOFLAGS, gettext_noop("Desired buffer size in milliseconds(ms)."), gettext_noop("The default value of 0 enables automatic buffer size selection."), MDFNST_UINT, "0", "0", "1000" },
  { "sound.rate", MDFNSF_NOFLAGS, gettext_noop("Specifies the sound playback rate, in sound frames per second(\"Hz\")."), NULL, MDFNST_UINT, "22050", "22050", "192000"},

  { "osd.message_display_time", MDFNSF_NOFLAGS, gettext_noop("Length of time, in milliseconds, to display internal status and error messages"), gettext_noop("Time lengths less than 100ms are recommended against unless you understand you may miss important non-fatal error messages, and that the input configuration process may become unusable."), MDFNST_UINT, "2500", "0", "15000" },
  { "osd.state_display_time", MDFNSF_NOFLAGS, gettext_noop("Length of time, in milliseconds, to display the save state or the movie selector after selecting a state or movie."),  NULL, MDFNST_UINT, "2000", "0", "15000" },
  { "osd.alpha_blend", MDFNSF_NOFLAGS, gettext_noop("Enable alpha blending for OSD elements."), NULL, MDFNST_BOOL, "1" },

  { "srwautoenable", MDFNSF_SUPPRESS_DOC, gettext_noop("DO NOT USE UNLESS YOU'RE A SPACE GOAT"/*"Automatically enable state rewinding functionality on game load."*/), gettext_noop("Use this setting with caution, as save state rewinding can have widely variable memory and CPU usage requirements among different games and different emulated systems."), MDFNST_BOOL, "0" },
};

void BuildSystemSetting(MDFNSetting *setting, const char *system_name, const char *name, const char *description, const char *description_extra, MDFNSettingType type, 
	const char *default_value, const char *minimum, const char *maximum,
	bool (*validate_func)(const char *name, const char *value), void (*ChangeNotification)(const char *name), 
        const MDFNSetting_EnumList *enum_list)
{
 char setting_name[256];

 memset(setting, 0, sizeof(MDFNSetting));

 snprintf(setting_name, 256, "%s.%s", system_name, name);

 setting->name = strdup(setting_name);
 setting->flags = MDFNSF_COMMON_TEMPLATE;
 setting->description = (char*)description;
 setting->description_extra = description_extra;
 setting->type = type;
 setting->default_value = default_value;
 setting->minimum = minimum;
 setting->maximum = maximum;
 setting->validate_func = validate_func;
 setting->ChangeNotification = ChangeNotification;
 setting->enum_list = enum_list;
}

void MakeDebugSettings(std::vector <MDFNSetting> &settings)
{
}

static struct
{
 std::unique_ptr<MDFN_Surface> surface = nullptr;
 MDFN_Rect rect;
 std::unique_ptr<int32[]> lw = nullptr;
 int field = -1;
} SoftFB[2];

static bool SoftFB_BackBuffer = false;

static std::atomic_int VTReady;
static bool VTSSnapshot = false;

//
//
//
//
//

static bool sc_blit_timesync;

static char *soundrecfn=0;	/* File name of sound recording. */

static char *DrBaseDirectory;

MDFNGI *CurGame=NULL;

void MDFND_PrintError(const char *s)
{
}

void MDFND_Message(const char *s)
{
 {
  fputs(s,stdout);
  fflush(stdout);
 }
}

static void CreateDirs(void)
{
 static const char* const subs[] = { "mcs", "mcm", "snaps", "palettes", "sav", "cheats", "firmware", "pgconfig" };

 try
 {
  MDFN_mkdir_T(DrBaseDirectory);
 }
 catch(MDFN_Error &e)
 {
  if(e.GetErrno() != EEXIST)
   throw;
 }

 for(auto const& s : subs)
 {
  std::string tdir = std::string(DrBaseDirectory) + std::string(PSS) + std::string(s);

  try
  {
   MDFN_mkdir_T(tdir.c_str());
  }
  catch(MDFN_Error &e)
  {
   if(e.GetErrno() != EEXIST)
    throw;
  }
 }
}

#if defined(HAVE_SIGNAL) || defined(HAVE_SIGACTION)

static const char *SiginfoString = NULL;
static bool volatile SignalSafeExitWanted = false;
typedef struct
{
 int number;
 const char *name;
 const char *message;
 const char *translated;	// Needed since gettext() can potentially deadlock when used in a signal handler.
 const bool SafeTryExit;
} SignalInfo;

static SignalInfo SignalDefs[] =
{
 #ifdef SIGINT
 { SIGINT, "SIGINT", gettext_noop("How DARE you interrupt me!\n"), NULL, TRUE },
 #endif

 #ifdef SIGTERM
 { SIGTERM, "SIGTERM", gettext_noop("MUST TERMINATE ALL HUMANS\n"), NULL, TRUE },
 #endif

 #ifdef SIGHUP
 { SIGHUP, "SIGHUP", gettext_noop("Reach out and hang-up on someone.\n"), NULL, FALSE },
 #endif

 #ifdef SIGSEGV
 { SIGSEGV, "SIGSEGV", gettext_noop("Iyeeeeeeeee!!!  A segmentation fault has occurred.  Have a fluffy day.\n"), NULL, FALSE },
 #endif

 #ifdef SIGPIPE
 { SIGPIPE, "SIGPIPE", gettext_noop("The pipe has broken!  Better watch out for floods...\n"), NULL, FALSE },
 #endif

 #if defined(SIGBUS) && SIGBUS != SIGSEGV
 /* SIGBUS can == SIGSEGV on some platforms */
 { SIGBUS, "SIGBUS", gettext_noop("I told you to be nice to the driver.\n"), NULL, FALSE },
 #endif

 #ifdef SIGFPE
 { SIGFPE, "SIGFPE", gettext_noop("Those darn floating points.  Ne'er know when they'll bite!\n"), NULL, FALSE },
 #endif

 #ifdef SIGALRM
 { SIGALRM, "SIGALRM", gettext_noop("Don't throw your clock at the meowing cats!\n"), NULL, TRUE },
 #endif

 #ifdef SIGABRT
 { SIGABRT, "SIGABRT", gettext_noop("Abort, Retry, Ignore, Fail?\n"), NULL, FALSE },
 #endif
 
 #ifdef SIGUSR1
 { SIGUSR1, "SIGUSR1", gettext_noop("Killing your processes is not nice.\n"), NULL, TRUE },
 #endif

 #ifdef SIGUSR2
 { SIGUSR2, "SIGUSR2", gettext_noop("Killing your processes is not nice.\n"), NULL, TRUE },
 #endif
};

static volatile int SignalSTDOUT;

static void SetSignals(void (*t)(int))
{
 SignalSTDOUT = fileno(stdout);

 SiginfoString = _("\nSignal has been caught and dealt with: ");
 for(unsigned int x = 0; x < sizeof(SignalDefs) / sizeof(SignalInfo); x++)
 {
  if(!SignalDefs[x].translated)
   SignalDefs[x].translated = _(SignalDefs[x].message);

  #ifdef HAVE_SIGACTION
  struct sigaction act;

  memset(&act, 0, sizeof(struct sigaction));

  act.sa_handler = t;
  act.sa_flags = SA_RESTART;

  sigaction(SignalDefs[x].number, &act, NULL);
  #else
  signal(SignalDefs[x].number, t);

  //#ifdef HAVE_SIGINTERRUPT
  //siginterrupt(SignalDefs[x].number, 0);
  //#endif

  #endif
 }
}

static void SignalPutString(const char *string)
{
 size_t count = 0;

 while(string[count]) { count++; }

 write(SignalSTDOUT, string, count);
}

static void CloseStuff(int signum)
{
	const int save_errno = errno;
	const char *name = "unknown";
	const char *translated = NULL;
	bool safetryexit = false;

	for(unsigned int x = 0; x < sizeof(SignalDefs) / sizeof(SignalInfo); x++)
	{
	 if(SignalDefs[x].number == signum)
	 {
	  name = SignalDefs[x].name;
	  translated = SignalDefs[x].translated;
	  safetryexit = SignalDefs[x].SafeTryExit;
	  break;
	 }
	}

	SignalPutString(SiginfoString);
	SignalPutString(name);
        SignalPutString("\n");
	SignalPutString(translated);

	if(safetryexit)
	{
         SignalSafeExitWanted = safetryexit;
	 errno = save_errno;
         return;
	}

	_exit(1);
}
#endif

//
//
//
#include <mednafen/FileStream.h>
#include <mednafen/compress/GZFileStream.h>
static void Stream64Test(const char* path)
{
 try
 {
  {
   FileStream fp(path, FileStream::MODE_WRITE_SAFE);

   assert(fp.tell() == 0);
   assert(fp.size() == 0);
   fp.put_BE<uint32>(0xDEADBEEF);
   assert(fp.tell() == 4);
   assert(fp.size() == 4);

   fp.seek(0x7FFFFFFFU, SEEK_SET);
   assert(fp.tell() == 0x7FFFFFFFU);
   fp.truncate(0x7FFFFFFFU);
   assert(fp.size() == 0x7FFFFFFFU);
   fp.put_LE<uint8>(0xB0);
   assert(fp.tell() == 0x80000000U);
   assert(fp.size() == 0x80000000U);
   fp.put_LE<uint8>(0x0F);
   assert(fp.tell() == 0x80000001U);
   assert(fp.size() == 0x80000001U);

   fp.seek(0xFFFFFFFFU, SEEK_SET);
   assert(fp.tell() == 0xFFFFFFFFU);
   fp.truncate(0xFFFFFFFFU);
   assert(fp.size() == 0xFFFFFFFFU);
   fp.put_LE<uint8>(0xCA);
   assert(fp.tell() == 0x100000000ULL);
   assert(fp.size() == 0x100000000ULL);
   fp.put_LE<uint8>(0xAD);
   assert(fp.tell() == 0x100000001ULL);
   assert(fp.size() == 0x100000001ULL);

   fp.seek((uint64)8192 * 1024 * 1024, SEEK_SET);
   fp.put_BE<uint32>(0xCAFEBABE);
   assert(fp.tell() == (uint64)8192 * 1024 * 1024 + 4);
   assert(fp.size() == (uint64)8192 * 1024 * 1024 + 4);

   fp.put_BE<uint32>(0xAAAAAAAA);
   assert(fp.tell() == (uint64)8192 * 1024 * 1024 + 8);
   assert(fp.size() == (uint64)8192 * 1024 * 1024 + 8);

   fp.truncate((uint64)8192 * 1024 * 1024 + 4);
   assert(fp.size() == (uint64)8192 * 1024 * 1024 + 4);

   fp.seek(-((uint64)8192 * 1024 * 1024 + 8), SEEK_CUR);
   assert(fp.tell() == 0);
   fp.seek((uint64)-4, SEEK_END);
   assert(fp.tell() == (uint64)8192 * 1024 * 1024);
  }

  {
   FileStream fp(path, FileStream::MODE_READ);
   uint32 tmp;

   assert(fp.size() == (uint64)8192 * 1024 * 1024 + 4);
   tmp = fp.get_LE<uint32>();
   assert(tmp == 0xEFBEADDE);
   fp.seek((uint64)8192 * 1024 * 1024 - 4, SEEK_CUR);
   tmp = fp.get_LE<uint32>(); 
   assert(tmp == 0xBEBAFECA);
  }

  {
   GZFileStream fp(path, GZFileStream::MODE::READ);
   uint32 tmp;

   tmp = fp.get_LE<uint32>();
   assert(tmp == 0xEFBEADDE);
   fp.seek((uint64)8192 * 1024 * 1024 - 4, SEEK_CUR);
   tmp = fp.get_LE<uint32>(); 
   assert(tmp == 0xBEBAFECA);  
   assert(fp.tell() == (uint64)8192 * 1024 * 1024 + 4);
  }
 }
 catch(std::exception& e)
 {
  printf("%s\n", e.what());
  abort();
 }
}
//
//
//
#include <mednafen/cdrom/cdromif.h>
static void CDTest(const char* path)
{
 try
 {
  CDIF* cds[2];
  CDUtility::TOC toc[2];

  cds[0] = CDIF_Open(path, false);
  cds[1] = CDIF_Open(path, true);

  for(unsigned i = 0; i < 2; i++)
   cds[0]->ReadTOC(&toc[i]);

  assert(!memcmp(&toc[0], &toc[1], sizeof(CDUtility::TOC)));

  srand(0xDEADBEEF);

  for(int32 lba = -150; lba < (int32)toc[0].tracks[100].lba + 5*60*75; lba++)
  {
   uint8 secbuf[2][2352 + 96];
   uint8 pwobuf[2][96];

   for(unsigned i = 0; i < 2; i++)
   {
    for(unsigned sbj = 0; sbj < 2352 + 96; sbj++)
     secbuf[i][sbj] = rand() >> 8;

    for(unsigned sbj = 0; sbj < 96; sbj++)
     pwobuf[i][sbj] = rand() >> 8;

    cds[i]->ReadRawSector(secbuf[i], lba);
    cds[i]->ReadRawSectorPWOnly(pwobuf[i], lba, true);

    for(unsigned p = 0; p < 96; p++)
     assert(secbuf[i][2352 + p] == pwobuf[i][p]);
   }
   assert(!memcmp(secbuf[0], secbuf[1], 2352 + 96));
   assert(!memcmp(pwobuf[0], pwobuf[1], 96));

   uint8 subq[12];
   CDUtility::subq_deinterleave(pwobuf[0], subq);
   if(CDUtility::subq_check_checksum(subq))
   {
   }
   else
    printf("SubQ checksum error at lba=%d\n", lba);
  }
 }
 catch(std::exception& e)
 {
  printf("%s\n", e.what());
  abort();
 }

 printf("CDTest Done.\n");
}
//
//
//
static ARGPSTRUCT *MDFN_Internal_Args = NULL;

static int HokeyPokeyFallDown(const char *name, const char *value)
{
 if(!MDFNI_SetSetting(name, value))
  return(0);
 return(1);
}

static void DeleteInternalArgs(void)
{
 if(!MDFN_Internal_Args) return;
 ARGPSTRUCT *argptr = MDFN_Internal_Args;

 do
 {
  free((void*)argptr->name);
  argptr++;
 } while(argptr->name || argptr->var || argptr->subs);
 free(MDFN_Internal_Args);
 MDFN_Internal_Args = NULL;
}

static void MakeMednafenArgsStruct(void)
{
 const std::vector<MDFNCS>* settings;
 std::vector<MDFNCS>::const_iterator sit;

 settings = MDFNI_GetSettings();

 MDFN_Internal_Args = (ARGPSTRUCT *)malloc(sizeof(ARGPSTRUCT) * (1 + settings->size()));

 unsigned int x = 0;

 for(sit = settings->begin(); sit != settings->end(); sit++)
 {
  MDFN_Internal_Args[x].name = strdup(sit->name);
  MDFN_Internal_Args[x].description = sit->desc->description ? _(sit->desc->description) : NULL;
  MDFN_Internal_Args[x].var = NULL;
  MDFN_Internal_Args[x].subs = (void *)HokeyPokeyFallDown;
  MDFN_Internal_Args[x].substype = SUBSTYPE_FUNCTION;
  x++;
 }
 MDFN_Internal_Args[x].name = NULL;
 MDFN_Internal_Args[x].var = NULL;
 MDFN_Internal_Args[x].subs = NULL;
}

static int netconnect = 0;
static char* loadcd = NULL;	// Deprecated
static int which_medium = -2;

static char * force_module_arg = NULL;
static int DoArgs(int argc, char *argv[], char **filename)
{
	int ShowCLHelp = 0;

	char *dsfn = NULL;
	char *dmfn = NULL;
	char *dummy_remote = NULL;
	char *stream64testpath = NULL;
	char *cdtestpath = NULL;
	int mtetest = 0;

        ARGPSTRUCT MDFNArgs[] = 
	{
	 { "help", _("Show help!"), &ShowCLHelp, 0, 0 },
	 { "remote", _("Enable remote mode with the specified stdout key(EXPERIMENTAL AND INCOMPLETE)."), 0, &dummy_remote, SUBSTYPE_STRING_ALLOC },

	 // -loadcd is deprecated and only still supported because it's been around for yeaaaars.
	 { "loadcd", NULL/*_("Load and boot a CD for the specified system.")*/, 0, &loadcd, SUBSTYPE_STRING_ALLOC },

	 { "which_medium", _("Start with specified disk/CD(numbered from 0) inserted."), 0, &which_medium, SUBSTYPE_INTEGER },

	 { "force_module", _("Force usage of specified emulation module."), 0, &force_module_arg, SUBSTYPE_STRING_ALLOC },

	 { "soundrecord", _("Record sound output to the specified filename in the MS WAV format."), 0,&soundrecfn, SUBSTYPE_STRING_ALLOC },

	 { "dump_settings_def", _("Dump settings definition data to specified file."), 0, &dsfn, SUBSTYPE_STRING_ALLOC },
	 { "dump_modules_def", _("Dump modules definition data to specified file."), 0, &dmfn, SUBSTYPE_STRING_ALLOC },

         { 0, NULL, (int *)MDFN_Internal_Args, 0, 0},

	 { "connect", _("Connect to the remote server and start network play."), &netconnect, 0, 0 },

	 // Testing functionality for FileStream and GZFileStream largefile support(mostly intended for testing the Windows builds)
	 { "stream64test", NULL, 0, &stream64testpath, SUBSTYPE_STRING_ALLOC },

	 { "cdtest", NULL, 0, &cdtestpath, SUBSTYPE_STRING_ALLOC },

	 { "mtetest", NULL, &mtetest, 0, 0 },

	 { 0, 0, 0, 0 }
        };

	const char *usage_string = _("Usage: %s [OPTION]... [FILE]\n");
	if(argc <= 1)
	{
	 printf(_("No command-line arguments specified.\n\n"));
	 printf(usage_string, argv[0]);
	 printf(_("\tPlease refer to the documentation for option parameters and usage.\n\n"));
	 return(0);
	}
	else
	{
	 if(!ParseArguments(argc - 1, &argv[1], MDFNArgs, filename))
	  return(0);

	 if(dummy_remote)
	 {
	  free(dummy_remote);
	  dummy_remote = NULL;
	 }

	 if(ShowCLHelp)
	 {
          printf(usage_string, argv[0]);
          ShowArgumentsHelp(MDFNArgs, false);
	  printf("\n");
	  printf(_("Each setting(listed in the documentation) can also be passed as an argument by prefixing the name with a hyphen,\nand specifying the value to change the setting to as the next argument.\n\n"));
	  printf(_("For example:\n\t%s -pce.stretch aspect -pce.pixshader autoipsharper \"Hyper Bonk Soldier.pce\"\n\n"), argv[0]);
	  printf(_("Settings specified in this manner are automatically saved to the configuration file, hence they\ndo not need to be passed to future invocations of the Mednafen executable.\n"));
	  printf("\n");
	  return(0);
	 }

	 if(mtetest)
	  MDFN_RunExceptionTests(4, 30000);

	 if(stream64testpath)
	 {
	  Stream64Test(stream64testpath);
	  free(stream64testpath);
	  stream64testpath = NULL;
	 }

	 if(cdtestpath)
	 {
	  CDTest(cdtestpath);
	  free(cdtestpath);
	  cdtestpath = NULL;
	 }

	 if(dsfn)
	  MDFNI_DumpSettingsDef(dsfn);

	 if(dmfn)
	  MDFNI_DumpModulesDef(dmfn);

	 if(dsfn || dmfn)
	  return(0);

	 if(*filename == NULL)
	 {
	  MDFN_PrintError(_("No game filename specified!"));
	  return(0);
	 }
	}
	return(1);
}

static int volatile NeedVideoChange = 0;
int GameLoop();
uint8_t GameThreadRun = 0;
static bool MDFND_Update(int WhichVideoBuffer, int16 *Buffer, int Count);

bool sound_active;	// true if sound is enabled and initialized


static EmuRealSyncher ers;

static int LoadGame(const char *force_module, const char *path)
{
	MDFNGI *tmp;

	CloseGame();

	pending_save_state = false;
	pending_save_movie = false;
	pending_snapshot = false;
	pending_ssnapshot = false;

	if(loadcd)	// Deprecated
	{
	 if(!(tmp = MDFNI_LoadCD(loadcd ? loadcd : force_module, path)))
	  return(0);
	}
	else
	{
         if(!(tmp=MDFNI_LoadGame(force_module, path)))
	  return 0;
	}

	CurGame = tmp;
	InitGameInput(tmp);
	InitCommandInput(tmp);
	RMDUI_Init(tmp, which_medium);

	RefreshThrottleFPS(1);

	sound_active = 0;

	sc_blit_timesync = MDFN_GetSettingB("video.blit_timesync");

	if(MDFN_GetSettingB("sound"))
		sound_active = Sound_Init(tmp);

	if(MDFN_GetSettingB("autosave"))
		MDFNI_LoadState(NULL, "mca");
	 
	ers.SetEmuClock(CurGame->MasterClock >> 32);

	ffnosound = MDFN_GetSettingB("ffnosound");

	//
	// Game thread creation should come lastish.
	//
	GameThreadRun = 1;

	return 1;
}

/* Closes a game and frees memory. */
int CloseGame(void)
{
	if(!CurGame) return(0);

	if(soundrecfn)
		MDFNI_StopWAVRecord();

	if(MDFN_GetSettingB("autosave"))
	 MDFNI_SaveState(NULL, "mca", NULL, NULL, NULL);
	 
	MDFNI_CloseGame();

	RMDUI_Kill();
	KillCommandInput();
        KillGameInput();
	Sound_Kill();

	CurGame = NULL;

	return(1);
}

static void GameThread_HandleEvents(void);
static int volatile NeedExitNow = 0;	// Set 'true' in various places, including signal handler.
double CurGameSpeed = 1;

void MainRequestExit(void)
{
	NeedExitNow = 1;
}

bool MDFND_CheckNeedExit(void)	// Called from netplay code, so we can break out of blocking loops after receiving a signal.
{
	return (bool)NeedExitNow;
}

static bool InFrameAdvance = 0;
static bool NeedFrameAdvance = 0;

bool IsInFrameAdvance(void)
{
	return InFrameAdvance;
}

void DoRunNormal(void)
{
	NeedFrameAdvance = 0;
	InFrameAdvance = 0;
}

void DoFrameAdvance(void)
{
	NeedFrameAdvance |= InFrameAdvance;
	InFrameAdvance = 1;
}

static int GameLoopPaused = 0;


int GameLoop()
{
	int16 *sound;
	int32 ssize;
	bool fskip;
        
	 /* If we requested a new video mode, wait until it's set before calling the emulation code again.
	 */
	while(NeedVideoChange)
	{
		if(!GameThreadRun) return 1;	// Might happen if video initialization failed
	}

	if(Sound_NeedReInit())
		GT_ReinitSound();

	//
	//
	fskip = ers.NeedFrameSkip();
	fskip &= MDFN_GetSettingB("video.frameskip");
	fskip &= !(pending_ssnapshot || pending_snapshot || pending_save_state || pending_save_movie || NeedFrameAdvance);
	fskip |= (bool)NoWaiting;

	//printf("fskip %d; NeedFrameAdvance=%d\n", fskip, NeedFrameAdvance);

	NeedFrameAdvance = false;
	SoftFB[SoftFB_BackBuffer].lw[0] = ~0;
	EmulateSpecStruct espec;

	memset(&espec, 0, sizeof(EmulateSpecStruct));

	espec.surface = SoftFB[SoftFB_BackBuffer].surface.get();
	espec.LineWidths = SoftFB[SoftFB_BackBuffer].lw.get();
	espec.skip = fskip;
	espec.soundmultiplier = CurGameSpeed;
	espec.NeedRewind = DNeedRewind;

	espec.SoundRate = Sound_GetRate();
	espec.SoundBuf = Sound_GetEmuModBuffer(&espec.SoundBufMaxSize);
	espec.SoundVolume = (double)MDFN_GetSettingUI("sound.volume") / 100;
	MDFNI_Emulate(&espec);

	ers.AddEmuTime((espec.MasterCycles - espec.MasterCyclesALMS) / CurGameSpeed);

	SoftFB[SoftFB_BackBuffer].rect = espec.DisplayRect;
	SoftFB[SoftFB_BackBuffer].field = espec.InterlaceOn ? espec.InterlaceField : -1;

	sound = espec.SoundBuf + (espec.SoundBufSizeALMS * CurGame->soundchan);
	ssize = espec.SoundBufSize - espec.SoundBufSizeALMS;

	FPS_IncVirtual();
	if(!fskip)
		FPS_IncDrawn();

	bool do_flip = false;

	do
	{
		do_flip = MDFND_Update(fskip ? -1 : SoftFB_BackBuffer, sound, ssize);

		FPS_UpdateCalc();

		if((InFrameAdvance && !NeedFrameAdvance) || GameLoopPaused)
		{
			if(ssize)
				for(int x = 0; x < CurGame->soundchan * ssize; x++)
					sound[x] = 0;
		}
	} 
	while(((InFrameAdvance && !NeedFrameAdvance) || GameLoopPaused) && GameThreadRun);
		SoftFB_BackBuffer ^= do_flip;

	return(1);
}   

char *GetBaseDirectory(void)
{
 char *ol;
 char *ret;

#ifndef DINGUX
 ol = getenv("MEDNAFEN_HOME");
 if(ol != NULL && ol[0] != 0)
 {
  ret = strdup(ol);
  return(ret);
 }
#endif

 ol = getenv("HOME");

 if(ol)
 {
  ret=(char *)malloc(strlen(ol)+1+strlen(PSS ".mednafen"));
  strcpy(ret,ol);
  strcat(ret,PSS ".mednafen");
  return(ret);
 }

 #if defined(HAVE_GETUID) && defined(HAVE_GETPWUID)
 {
  struct passwd *psw;

  psw = getpwuid(getuid());

  if(psw != NULL && psw->pw_dir[0] != 0 && strcmp(psw->pw_dir, "/dev/null"))
  {
   ret = (char *)malloc(strlen(psw->pw_dir) + 1 + strlen(PSS ".mednafen"));
   strcpy(ret, psw->pw_dir);
   strcat(ret, PSS ".mednafen");
   return(ret);
  }
 }
 #endif

 #ifdef WIN32
 {
  char *sa;

  ret=(char *)malloc(MAX_PATH+1);
  GetModuleFileName(NULL,ret,MAX_PATH+1);

  sa=strrchr(ret,'\\');
  if(sa)
   *sa = 0;
  return(ret);
 }
 #endif

 ret = (char *)malloc(1);
 ret[0] = 0;
 return(ret);
}

static const int gtevents_size = 2048; // Must be a power of 2.
static volatile SDL_Event gtevents[gtevents_size];
static volatile int gte_read = 0;
static volatile int gte_write = 0;

/* This function may also be called by the main thread if a game is not loaded. */
/*
 This function may be called from MDFND_MidSync(), so make sure that it doesn't call directly nor indirectly
 any MDFNI_* functions that shouldn't be called.
*/
static void GameThread_HandleEvents(void)
{
 SDL_Event gtevents_temp[gtevents_size];
 unsigned int numevents = 0;

 while(gte_read != gte_write)
 {
  memcpy(&gtevents_temp[numevents], (void *)&gtevents[gte_read], sizeof(SDL_Event));

  numevents++;
  gte_read = (gte_read + 1) & (gtevents_size - 1);
 }

 for(unsigned int i = 0; i < numevents; i++)
 {
  SDL_Event *event = &gtevents_temp[i];

  switch(event->type)
  {
   case SDL_USEREVENT:
		switch(event->user.code)
		{
		 case CEVT_SET_INPUT_FOCUS:
			MDFNDHaveFocus = (bool)((char*)event->user.data1 - (char*)0);
			//printf("%u\n", MDFNDHaveFocus);
			break;
		}
		break;
  }

  Input_Event(event);
 }
}

void PauseGameLoop(bool p)
{
 GameLoopPaused = p;
}


void SendCEvent(unsigned int code, void *data1, void *data2)
{
 SDL_Event evt;
 evt.user.type = SDL_USEREVENT;
 evt.user.code = code;
 evt.user.data1 = data1;
 evt.user.data2 = data2;
 SDL_PushEvent(&evt);
}

void SendCEvent_to_GT(unsigned int code, void *data1, void *data2)
{
 SDL_Event evt;
 evt.user.type = SDL_USEREVENT;
 evt.user.code = code;
 evt.user.data1 = data1;
 evt.user.data2 = data2;

 memcpy((void *)&gtevents[gte_write], &evt, sizeof(SDL_Event));
 gte_write = (gte_write + 1) & (gtevents_size - 1);
}

void SDL_MDFN_ShowCursor(int toggle)
{
 int *toog = (int *)malloc(sizeof(int));
 *toog = toggle;

 SDL_Event evt;
 evt.user.type = SDL_USEREVENT;
 evt.user.code = CEVT_SHOWCURSOR;
 evt.user.data1 = toog;
 SDL_PushEvent(&evt);

}

void GT_ToggleFS(void)
{
}

bool GT_ReinitVideo(void)
{
	return(true);	// FIXME!
}

bool GT_ReinitSound(void)
{
	bool ret = true;

	Sound_Kill();
	sound_active = 0;

	if(MDFN_GetSettingB("sound"))
	{
		sound_active = Sound_Init(CurGame);
		if(!sound_active)
			ret = false;
	}
	return(ret);
}

static bool krepeat = 0;
void PumpWrap(void)
{
	SDL_Event event;
	SDL_Event gtevents_temp[gtevents_size];
	int numevents = 0;

	if(krepeat)
	SDL_EnableKeyRepeat(0, 0);
	krepeat = 0;

	#if defined(HAVE_SIGNAL) || defined(HAVE_SIGACTION)
	if(SignalSafeExitWanted)
		NeedExitNow = true;
	#endif

	while(SDL_PollEvent(&event))
	{
		/* Handle the event, and THEN hand it over to the GUI. Order is important due to global variable mayhem(CEVT_TOGGLEFS. */
		switch(event.type)
		{
			case SDL_ACTIVEEVENT:
				if(event.active.state & SDL_APPINPUTFOCUS)
					SendCEvent_to_GT(CEVT_SET_INPUT_FOCUS, (char*)0 + (bool)event.active.gain, NULL);
			break;
			case SDL_SYSWMEVENT: break;
			case SDL_VIDEOEXPOSE: break;
			case SDL_QUIT: 
				NeedExitNow = 1;
			break;
			case SDL_USEREVENT:
			switch(event.user.code)
			{
				case CEVT_SET_STATE_STATUS: 
					MT_SetStateStatus((StateStatusStruct *)event.user.data1);
				break;
				case CEVT_SET_MOVIE_STATUS: 
					MT_SetMovieStatus((StateStatusStruct *)event.user.data1);
				break;
				case CEVT_WANT_EXIT:
					NeedExitNow = 1;
				break;
				case CEVT_SET_GRAB_INPUT:
					SDL_WM_GrabInput(*(uint8 *)event.user.data1 ? SDL_GRAB_ON : SDL_GRAB_OFF);
					free(event.user.data1);
				break;
				case CEVT_SHOWCURSOR:
					SDL_ShowCursor(*(int *)event.user.data1);
					free(event.user.data1);
				break;
				case CEVT_DISP_MESSAGE:
					VideoShowMessage((char*)event.user.data1);
				break;
				default: 
					if(numevents < gtevents_size)
					{
						memcpy(&gtevents_temp[numevents], &event, sizeof(SDL_Event));
						numevents++;
					}
				break;
			}
			break;
			default: 
			   if(numevents < gtevents_size)
			   {
				memcpy(&gtevents_temp[numevents], &event, sizeof(SDL_Event));
				numevents++;
			   }
		   break;
		}
	}

	if(numevents > 0)
	{
		for(int i = 0; i < numevents; i++)
		{
			memcpy((void *)&gtevents[gte_write], &gtevents_temp[i], sizeof(SDL_Event));
			gte_write = (gte_write + 1) & (gtevents_size - 1);
		}
	}

	if(!CurGame) GameThread_HandleEvents();
}

void RefreshThrottleFPS(double multiplier)
{
	CurGameSpeed = multiplier;
}

void PrintCompilerVersion(void)
{
 #if defined(__GNUC__)
  MDFN_printf(_("Compiled with gcc %s\n"), __VERSION__);
 #endif

 #ifdef HAVE___MINGW_GET_CRT_INFO
  MDFN_printf(_("Running with %s\n"), __mingw_get_crt_info());
 #endif
}


void PrintSDLVersion(void)
{
 const SDL_version *sver = SDL_Linked_Version();

 MDFN_printf(_("Compiled against SDL %u.%u.%u, running with SDL %u.%u.%u\n"), SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL, sver->major, sver->minor, sver->patch);
}

#ifdef HAVE_LIBSNDFILE
 #include <sndfile.h>
#endif

void PrintLIBSNDFILEVersion(void)
{
 #ifdef HAVE_LIBSNDFILE
  MDFN_printf(_("Running with %s\n"), sf_version_string());
 #endif
}

#include <zlib.h>
void PrintZLIBVersion(void)
{
 #ifdef ZLIB_VERSION
  MDFN_printf(_("Compiled against zlib %s, running with zlib %s(flags=0x%08lx)\n"), ZLIB_VERSION, zlibVersion(), (unsigned long)zlibCompileFlags());
 #endif
}

void PrintLIBICONVVersion(void)
{
}

static bool HandleVideoChange(void)
{
 if(NeedVideoChange == -1)
 {
  try
  {
   Video_Init(CurGame);
  }
  catch(std::exception &e)
  {
   MDFND_PrintError(e.what());
   return(false);
  }
 }
 else
 {
  bool original_fs_setting = MDFN_GetSettingB("video.fs");

  try
  {
   MDFNI_SetSettingB("video.fs", !original_fs_setting);
   Video_Init(CurGame);
  }
  catch(std::exception &e)
  {
   MDFND_PrintError(e.what());

   try
   {
    MDFNI_SetSettingB("video.fs", original_fs_setting);
    Video_Init(CurGame);	    
   }
   catch(std::exception &ne)
   {
    MDFND_PrintError(ne.what());
    return(false);
   }
  }
 }

 return(true);
}


int main(int argc, char *argv[])
{
	char *needie = NULL;

        // Place before calls to SDL_Init()
	putenv(strdup("SDL_DISABLE_LOCK_KEYS=1"));
        //

	MDFNDHaveFocus = false;

	DrBaseDirectory=GetBaseDirectory();

	#ifdef ENABLE_NLS
	setlocale(LC_ALL, "");

	#ifdef WIN32
        bindtextdomain(PACKAGE, DrBaseDirectory);
	#else
	bindtextdomain(PACKAGE, LOCALEDIR);
	#endif

	bind_textdomain_codeset(PACKAGE, "UTF-8");
	textdomain(PACKAGE);
	#endif
	
	MDFNI_printf(_("Starting Mednafen %s\n"), MEDNAFEN_VERSION);
	MDFN_indent(1);

	MDFN_printf(_("Build information:\n"));
	MDFN_indent(2);
	PrintCompilerVersion();
	//PrintGLIBCXXInfo();
	PrintZLIBVersion();
	PrintLIBICONVVersion();
	PrintSDLVersion();
	PrintLIBSNDFILEVersion();
	MDFN_indent(-2);

	MDFN_printf(_("Base directory: %s\n"), DrBaseDirectory);

	if(SDL_Init(SDL_INIT_VIDEO)) /* SDL_INIT_VIDEO Needed for (joystick config) event processing? */
	{
		fprintf(stderr, "Could not initialize SDL: %s\n", SDL_GetError());
		MDFNI_Kill();
		return(-1);
	}
	SDL_JoystickEventState(SDL_IGNORE);
	
	if(!MDFNI_InitializeModules())
		return(-1);

	for(unsigned int x = 0; x < sizeof(DriverSettings) / sizeof(MDFNSetting); x++)
		NeoDriverSettings.push_back(DriverSettings[x]);

	MakeDebugSettings(NeoDriverSettings);
	Video_MakeSettings(NeoDriverSettings);
	MakeInputSettings(NeoDriverSettings);

	if(!MDFNI_Initialize(DrBaseDirectory, NeoDriverSettings))
		return(-1);

	SDL_EnableUNICODE(1);

	#if defined(HAVE_SIGNAL) || defined(HAVE_SIGACTION)
		SetSignals(CloseStuff);
	#endif

	try
	{
		CreateDirs();
	}
	catch(std::exception &e)
	{
		MDFN_PrintError(_("Error creating directories: %s\n"), e.what());
		MDFNI_Kill();
		return(-1);
	}

	MakeMednafenArgsStruct();

	if(!DoArgs(argc,argv, &needie))
	{
		MDFNI_Kill();
		DeleteInternalArgs();
		KillInputSettings();
		return(-1);
	}

	/* Now the fun begins! */
	/* Run the video and event pumping in the main thread, and create a 
	   secondary thread to run the game in(and do sound output, since we use
	   separate sound code which should be thread safe(?)).
	*/
	int ret = 0;

	//Video_Init(NULL);

	joy_manager = new JoystickManager();
	joy_manager->SetAnalogThreshold(MDFN_GetSettingF("analogthreshold") / 100);


	VTReady.store(-1, std::memory_order_release);
	NeedVideoChange = -1;

	NeedExitNow = 0;

	if(LoadGame(force_module_arg, needie))
	{
		uint16 pitch16 = CurGame->fb_width; 
		//uint32 pitch32 = round_up_pow2(CurGame->fb_width);
		MDFN_PixelFormat nf(MDFN_COLORSPACE_RGB, 0, 5, 11, 16);

		for(uint32_t i = 0; i < 2; i++)
		{
			SoftFB[i].surface.reset(new MDFN_Surface(NULL, CurGame->fb_width, CurGame->fb_height, pitch16, nf));
			SoftFB[i].lw.reset(new int32[CurGame->fb_height]);
			SoftFB[i].surface->Fill(0, 0, 0, 0);
			SoftFB[i].rect.w = std::min<int32>(16, SoftFB[i].surface->w);
			SoftFB[i].rect.h = std::min<int32>(16, SoftFB[i].surface->h);
			SoftFB[i].lw[0] = ~0;
		}
		NeedVideoChange = -1;
		FPS_Init();
	}
	else
	{
		ret = -1;
		NeedExitNow = 1;
	}

	while(MDFN_LIKELY(!NeedExitNow))
	{
		bool DidVideoChange = false;

		if(MDFN_UNLIKELY(NeedVideoChange))
		{
			Video_Kill();

			if(!HandleVideoChange())
			{
				ret = -1;
				NeedExitNow = 1;
				NeedVideoChange = 0;
				break;
			}
			DidVideoChange = true;
			NeedVideoChange = 0;
		}

		const int vtr = VTReady.load(std::memory_order_acquire);

		if(vtr >= 0)
		{
			BlitScreen(SoftFB[vtr].surface.get(), &SoftFB[vtr].rect, SoftFB[vtr].lw.get(), SoftFB[vtr].field, VTSSnapshot);
			VTReady.store(-1, std::memory_order_release);	// Set to -1 after we're done blitting everything(including on-screen display stuff), and NOT just the emulated system's video surface.
		}
          
		GameLoop();
		PumpWrap();
		if(DidVideoChange)	// Do it after PumpWrap() in case there are stale SDL_ActiveEvent in the SDL event queue.
			SendCEvent_to_GT(CEVT_SET_INPUT_FOCUS, (char*)0 + (bool)(SDL_GetAppState() & SDL_APPINPUTFOCUS), NULL);
	}

	CloseGame();

	for(uint32_t i = 0; i < 2; i++)
	{
		SoftFB[i].surface.reset(nullptr);
		SoftFB[i].lw.reset(nullptr);
	}

	#if defined(HAVE_SIGNAL) || defined(HAVE_SIGACTION)
	SetSignals(SIG_IGN);
	#endif

	MDFNI_Kill();

	delete joy_manager;
	joy_manager = NULL;

	Video_Kill();

	SDL_Quit();

	DeleteInternalArgs();
	KillInputSettings();

	return(ret);
}


static uint32 last_btime = 0;
static void UpdateSoundSync(int16 *Buffer, uint32 Count)
{
 if(Count)
 {
  if(ffnosound && CurGameSpeed != 1)
  {
   for(uint32 x = 0; x < Count * CurGame->soundchan; x++)
    Buffer[x] = 0;
  }
  //
  //
  //
  const uint32 cw = Sound_CanWrite();
  bool NeedETtoRT = (Count >= (cw * 0.95));

  if(NoWaiting && Count > cw)
  {
   //printf("NW C to M; count=%d, max=%d\n", Count, max);
   Count = cw;
  }


  Sound_Write(Buffer, Count);

  if(NeedETtoRT)
   ers.SetETtoRT();
 }
 else
 {
  bool nothrottle = MDFN_GetSettingB("nothrottle");

  if(!NoWaiting && !nothrottle && GameThreadRun)
   ers.Sync();
 }
}

void MDFND_MidSync(const EmulateSpecStruct *espec)
{
 ers.AddEmuTime((espec->MasterCycles - espec->MasterCyclesALMS) / CurGameSpeed, false);

 UpdateSoundSync(espec->SoundBuf + (espec->SoundBufSizeALMS * CurGame->soundchan), espec->SoundBufSize - espec->SoundBufSizeALMS);

 GameThread_HandleEvents(); // Should be safe, but be careful about future changes.
 MDFND_UpdateInput(true, false);
}

static bool PassBlit(const int WhichVideoBuffer)
{
 if(WhichVideoBuffer < 0)
  return false;

 while(VTReady.load(std::memory_order_acquire) >= 0)
 {
  /* If it's been > 100ms since the last blit, assume that the blit
     thread is being time-slice starved, and let it run.  This is especially necessary
     for fast-forwarding to respond well(since keyboard updates are
     handled in the main thread) on slower systems or when using a higher fast-forwarding speed ratio.
  */
  if(!GameThreadRun || ((last_btime + 100) >= Time::MonoMS() && !pending_ssnapshot))
   return false;
  else
   Time::SleepMS(1);
 }

 VTSSnapshot = pending_ssnapshot;
 //
 VTReady.store(WhichVideoBuffer, std::memory_order_release);
 //
 //
 //
 pending_ssnapshot = false;
 last_btime = Time::MonoMS();
 FPS_IncBlitted();


 return true;
}


//
// Called from game thread.  Pass -1 for WhichVideoBuffer when the frame is skipped.
//
static bool MDFND_Update(int WhichVideoBuffer, int16 *Buffer, int Count)
{
 bool ret = false;

 if(WhichVideoBuffer >= 0)
 {

  //
  // Save any pending screen snapshots, save states, and movies before any potential calls to PassBlit().
  //
  MDFN_Surface* surface = SoftFB[WhichVideoBuffer].surface.get();
  MDFN_Rect* rect = &SoftFB[WhichVideoBuffer].rect;
  int32* lw = SoftFB[WhichVideoBuffer].lw.get();

  if(pending_snapshot)
   MDFNI_SaveSnapshot(surface, rect, lw);

  if(pending_save_state)
   MDFNI_SaveState(NULL, NULL, surface, rect, lw);

  pending_save_movie = pending_snapshot = pending_save_state = false;
 }

 if(false == sc_blit_timesync)
 {
  //puts("ABBYNORMAL");
  ret |= PassBlit(WhichVideoBuffer);
 }

 UpdateSoundSync(Buffer, Count);

 GameThread_HandleEvents();
 MDFND_UpdateInput();

 if(true == sc_blit_timesync)
 {
  //puts("NORMAL");
  ret |= PassBlit(WhichVideoBuffer);
 }

 return(ret);
}

void MDFND_DispMessage(char* text)
{
 SendCEvent(CEVT_DISP_MESSAGE, text, NULL);
}

void MDFND_SetStateStatus(StateStatusStruct *status) noexcept
{
 SendCEvent(CEVT_SET_STATE_STATUS, status, NULL);
}

void MDFND_SetMovieStatus(StateStatusStruct *status) noexcept
{
 SendCEvent(CEVT_SET_MOVIE_STATUS, status, NULL);
}


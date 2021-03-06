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

#include <string.h>

#include "input.h"
#include "input-config.h"
#include "video.h"
#include "Joystick.h"

extern JoystickManager *joy_manager;

int32 DTestMouseAxis(ButtConfig &bc, const uint8* KeyState, const uint32* MouseData, const bool axis_hint)	// UNDOCUMENTED INCOMPLETE FUN
{
 /*if(bc.ButtType == BUTTC_JOYSTICK)	// HAXY
 {
  int32 ret = 0;
  ButtConfig tmp_bc = bc;

  tmp_bc.ButtonNum ^= 0x4000;

  ret = joy_manager->TestAnalogButton(bc);
  ret -= joy_manager->TestAnalogButton(tmp_bc);
  ret += 32768;

  ret = Video_PtoV_J(ret, axis_hint, (bool)(tmp_bc.ButtonNum & (1 << 18)));

  return(ret);
 }
 else if(bc.ButtType == BUTTC_MOUSE)
 {
  if(bc.DeviceNum == 0)
  {
   if(bc.ButtonNum & 0x8000)
    return(MouseData[bc.ButtonNum & 1]);
   else
    return(0);
  }
  else
   return(0);
 }
 else		*/			// UNSUPPORTED
  return(0);
}

static bool DTestButtonMouse(const ButtConfig &bc, const uint32 *MouseData)
{
 if(bc.ButtonNum & 0x8000)
 {
  if(bc.ButtonNum&1) // Y
  {
   if(bc.ButtonNum & 0x4000)
   {
    if(MouseData[1] < 128)
     return(true);
   }
   else
   {
    if(MouseData[1] >= 128)
     return(true);
   }
  }
  else			// X
  {
   if(bc.ButtonNum & 0x4000)
   {
    if(MouseData[0] < 128)
     return(true);
   }
   else
   {
    if(MouseData[0] >= 128)
     return(true);
   }
  }   
 }
 else 
 {
  if(MouseData[2] & (1U << bc.ButtonNum))
   return(true);
 }
 return(false);
}

bool DTestButton(const std::vector<ButtConfig>& bc, const uint8* KeyState, const uint32* MouseData, bool AND_Mode)
{
 unsigned match_count = 0;

 for(auto const& bce : bc)
 {
  switch(bce.ButtType)
  {
   case BUTTC_KEYBOARD:
	match_count += (bool)KeyState[bce.ButtonNum];
	break;

   case BUTTC_JOYSTICK:
	match_count += (bool)joy_manager->TestButton(bce);
	break;

   case BUTTC_MOUSE:
	match_count += (bool)DTestButtonMouse(bce, MouseData);
	break;
  }
 }

 return match_count && (!AND_Mode || match_count == bc.size());
}

int32 DTestAnalogButton(const std::vector<ButtConfig>& bc, const uint8* KeyState, const uint32* MouseData)
{
 const int maxv = 32767;
 int32 ret = 0;	// Will obviously break if 10s of thousands of physical buttons are assigned to one emulated analog button. ;)

 for(auto const& bce : bc)
 {
  switch(bce.ButtType)
  {
   case BUTTC_KEYBOARD:
	if(KeyState[bce.ButtonNum])
	 ret += maxv;
	break;

   case BUTTC_JOYSTICK:
	ret += joy_manager->TestAnalogButton(bce);
	break;

   case BUTTC_MOUSE:
	if(DTestButtonMouse(bce, MouseData))
	 ret += maxv;
	break;
  }
 }

 return std::min<int32>(ret, maxv);
}

#define ICSS_ALT	1
#define ICSS_SHIFT	2
#define ICSS_CTRL	4

/* Used for command keys */
int DTestButtonCombo(std::vector<ButtConfig> &bc, const uint8 *KeyState, const uint32 *MouseData, bool AND_Mode)
{
 unsigned int x;
 unsigned int ss = 0;
 unsigned match_count = 0;

 if(KeyState[MKK(LALT)] || KeyState[MKK(RALT)]) ss |= ICSS_ALT;
 if(KeyState[MKK(LSHIFT)] || KeyState[MKK(RSHIFT)]) ss |= ICSS_SHIFT;
 if(KeyState[MKK(LCTRL)] || KeyState[MKK(RCTRL)]) ss |= ICSS_CTRL;

 for(x = 0; x < bc.size(); x++)
 {
  if(bc[x].ButtType == BUTTC_KEYBOARD)
  {
   uint32 b = bc[x].ButtonNum;

   if(KeyState[b & 0xFFFF] && ((b >> 24) == ss))
    match_count++;
  }
  else if(bc[x].ButtType == BUTTC_JOYSTICK)
  {
   if(joy_manager->TestButton(bc[x]))
    match_count++;
  }
  else if(bc[x].ButtType == BUTTC_MOUSE)
  {
   if(DTestButtonMouse(bc[x], MouseData))
    match_count++;
  }
 }

 if(match_count > 0)
 {
  if(!AND_Mode || match_count == bc.size())
   return(1);
 }

 return(0);
}

int DTestButtonCombo(ButtConfig &bc, const uint8* KeyState, const uint32 *MouseData, bool AND_Mode)
{
 std::vector<ButtConfig> neobc;
 neobc.push_back(bc);

 return(DTestButtonCombo(neobc, KeyState, MouseData, AND_Mode));
}


static ButtConfig efbc;
static int volatile efbcdone;
static int volatile efck;

static int LastMouseX;
static int LastMouseY;

static int EventFilter(const SDL_Event *event)
{
 if(efbcdone)
  return(1);

 switch(event->type)
 {
   case SDL_KEYDOWN:    if(!efck || (event->key.keysym.sym != MKK(LALT) && event->key.keysym.sym != MKK(RALT) &&
                         event->key.keysym.sym != MKK(LSHIFT) && event->key.keysym.sym != MKK(RSHIFT) &&
			 event->key.keysym.sym != MKK(LCTRL) && event->key.keysym.sym != MKK(RCTRL)))
                        {
                                efbc.ButtType = BUTTC_KEYBOARD;
                                efbc.DeviceNum = 0;
                                efbc.ButtonNum = event->key.keysym.sym;

				if(0 == event->key.keysym.sym)
				 printf("*** NULL KEYSYM! ***\n");

				//printf("%u\n", event->key.keysym.sym);

                                if(efck)
                                        efbc.ButtonNum |= ((event->key.keysym.mod & KMOD_ALT) ? (ICSS_ALT<<24):0) | ((event->key.keysym.mod & KMOD_SHIFT) ? (ICSS_SHIFT<<24):0) | ((event->key.keysym.mod & KMOD_CTRL) ? (ICSS_CTRL<<24):0);
				efbcdone = 1;
                                return(1);
                        }
                        break;

  case SDL_MOUSEBUTTONDOWN: efbc.ButtType = BUTTC_MOUSE;
			    efbc.DeviceNum = 0;
			    efbc.ButtonNum = event->button.button - SDL_BUTTON_LEFT;
  		   	    efbcdone = 1;
			    return(1);

 }

 return(1);
}


int DTryButtonBegin(ButtConfig *bc, int commandkey)
{
 memcpy(&efbc, bc, sizeof(ButtConfig));
 efck = commandkey;
 efbcdone = 0;

 //SDL_MDFN_ShowCursor(1);
 SDL_GetMouseState(&LastMouseX, &LastMouseY);

 MainSetEventHook(EventFilter);
 //SDL_SetEventFilter(EventFilter);
 return(1);
}

int DTryButton(void)
{
 return(efbcdone);
}

int DTryButtonEnd(ButtConfig *bc)
{
 MainSetEventHook(EventFilter);
 memcpy(bc, &efbc, sizeof(ButtConfig));

 return(1);
}


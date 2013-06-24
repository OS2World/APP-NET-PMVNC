//  Copyright (C) 2002-2003 RealVNC Ltd. All Rights Reserved.
//  Copyright (C) 1999 AT&T Laboratories Cambridge. All Rights Reserved.
//
//  This file is part of the VNC system.
//
//  The VNC system is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
//  USA.
//
// If the source code for the program is not available from the place from
// which you received this file, check http://www.realvnc.com/ or contact
// the authors on info@realvnc.com for information on obtaining it.

// vncKeymap.cpp

// This code originally just mapped between X keysyms and local Windows
// virtual keycodes.  Now it actually does the local-end simulation of
// key presses, to keep this messy code on one place!

#include "stdhdrs.h"
#include "vncKeymap.h"
#include <rdr/types.h>
#define XK_MISCELLANY
#include "keysymdef.h"

#include <map>

// Mapping of X keysyms to windows VK codes.  Ordering here is the same as
// keysymdef.h to make checking easier

struct keymap_t {
  rdr::U32 keysym;
  rdr::U8 vk;
  bool extended;
};

static keymap_t keymap[] = {

  // TTY functions

  { XK_BackSpace,        VK_BACKSPACE, 0 },
  { XK_Tab,              VK_TAB, 0 },
  { XK_Clear,            VK_CLEAR, 0 },
  { XK_Return,           VK_ENTER, 0 },
  { XK_Pause,            VK_PAUSE, 0 },
  { XK_Escape,           VK_ESC, 0 },
  { XK_Delete,           VK_DELETE, 1 },

  // Japanese stuff - almost certainly wrong...
  { XK_Kanji,            0, 0 },
  { XK_Kana_Shift,       0, 0 },

  // Cursor control & motion

  { XK_Home,             VK_HOME, 1 },
  { XK_Left,             VK_LEFT, 1 },
  { XK_Up,               VK_UP, 1 },
  { XK_Right,            VK_RIGHT, 1 },
  { XK_Down,             VK_DOWN, 1 },
  { XK_Page_Up,          VK_PAGEUP, 1 },
  { XK_Page_Down,        VK_PAGEDOWN, 1 },
  { XK_End,              VK_END, 1 },

  // Misc functions

  //{ XK_Select,           VK_SELECT, 0 },
  //{ XK_Print,            VK_SNAPSHOT, 0 },
  //{ XK_Execute,          VK_EXECUTE, 0 },
  { XK_Insert,           VK_INSERT, 1 },
  //{ XK_Help,             VK_HELP, 0 },
  //{ XK_Break,            VK_CANCEL, 1 },

  // Keypad Functions, keypad numbers

  { XK_KP_Space,         VK_SPACE, 0 },
  { XK_KP_Tab,           VK_TAB, 0 },
  { XK_KP_Enter,         VK_ENTER, 1 },
  { XK_KP_F1,            VK_F1, 0 },
  { XK_KP_F2,            VK_F2, 0 },
  { XK_KP_F3,            VK_F3, 0 },
  { XK_KP_F4,            VK_F4, 0 },
  { XK_KP_Home,          VK_HOME, 0 },
  { XK_KP_Left,          VK_LEFT, 0 },
  { XK_KP_Up,            VK_UP, 0 },
  { XK_KP_Right,         VK_RIGHT, 0 },
  { XK_KP_Down,          VK_DOWN, 0 },
  { XK_KP_End,           VK_END, 0 },
  { XK_KP_Page_Up,       VK_PAGEUP, 0 },
  { XK_KP_Page_Down,     VK_PAGEDOWN, 0 },
  //{ XK_KP_Begin,         VK_CLEAR, 0 },
  { XK_KP_Insert,        VK_INSERT, 0 },
  { XK_KP_Delete,        VK_DELETE, 0 },
  // XXX XK_KP_Equal should map in the same way as ascii '='
  //{ XK_KP_Multiply,      VK_MULTIPLY, 0 },
  //{ XK_KP_Add,           VK_ADD, 0 },
  //{ XK_KP_Separator,     VK_SEPARATOR, 0 },
  //{ XK_KP_Subtract,      VK_SUBTRACT, 0 },
  //{ XK_KP_Decimal,       VK_DECIMAL, 0 },
  //{ XK_KP_Divide,        VK_DIVIDE, 1 },

  //{ XK_KP_0,             VK_NUMPAD0, 0 },
  //{ XK_KP_1,             VK_NUMPAD1, 0 },
  //{ XK_KP_2,             VK_NUMPAD2, 0 },
  //{ XK_KP_3,             VK_NUMPAD3, 0 },
  //{ XK_KP_4,             VK_NUMPAD4, 0 },
  //{ XK_KP_5,             VK_NUMPAD5, 0 },
  //{ XK_KP_6,             VK_NUMPAD6, 0 },
  //{ XK_KP_7,             VK_NUMPAD7, 0 },
  //{ XK_KP_8,             VK_NUMPAD8, 0 },
  //{ XK_KP_9,             VK_NUMPAD9, 0 },

  // Auxilliary Functions

  { XK_F1,               VK_F1, 0 },
  { XK_F2,               VK_F2, 0 },
  { XK_F3,               VK_F3, 0 },
  { XK_F4,               VK_F4, 0 },
  { XK_F5,               VK_F5, 0 },
  { XK_F6,               VK_F6, 0 },
  { XK_F7,               VK_F7, 0 },
  { XK_F8,               VK_F8, 0 },
  { XK_F9,               VK_F9, 0 },
  { XK_F10,              VK_F10, 0 },
  { XK_F11,              VK_F11, 0 },
  { XK_F12,              VK_F12, 0 },
  { XK_F13,              VK_F13, 0 },
  { XK_F14,              VK_F14, 0 },
  { XK_F15,              VK_F15, 0 },
  { XK_F16,              VK_F16, 0 },
  { XK_F17,              VK_F17, 0 },
  { XK_F18,              VK_F18, 0 },
  { XK_F19,              VK_F19, 0 },
  { XK_F20,              VK_F20, 0 },
  { XK_F21,              VK_F21, 0 },
  { XK_F22,              VK_F22, 0 },
  { XK_F23,              VK_F23, 0 },
  { XK_F24,              VK_F24, 0 },

    // Modifiers

  { XK_Shift_L,          VK_SHIFT, 0 },
  { XK_Shift_R,          VK_SHIFT, 0 },
  { XK_Control_L,        VK_CTRL, 0 },
  { XK_Control_R,        VK_CTRL, 1 },
  { XK_Alt_L,            VK_ALT, 0 },
  //{ XK_Alt_R,            VK_RMENU, 1 },
};


static HAB lhab = NULLHANDLE;
// doKeyboardEvent wraps the system keybd_event function and attempts to find
// the appropriate scancode corresponding to the supplied virtual keycode.

inline void doKeyboardEvent( BYTE vkCode, BYTE chCode, DWORD flags) {
    keyb_event( lhab , vkCode, chCode, flags);
}

void unBlank();

static void SimulateCtrlAltDel( HAB hab )
{
	unBlank();
	
    HSWITCH hs = WinQuerySwitchHandle( NULLHANDLE , getpid() );
    WinSwitchToProgram( hs );
}


// Keymapper - a single instance of this class is used to generate Windows key
// events.

static bool ctrlPressed; // for CAD support in fullscreen
static bool altPressed;

class Keymapper
{

  public:

    Keymapper()
    {
        for (int i = 0; i < sizeof(keymap) / sizeof(keymap_t); i++)
        {
            vkMap[keymap[i].keysym] = keymap[i].vk;
            extendedMap[keymap[i].keysym] = keymap[i].extended;
        }
        ctrlPressed = false;
        altPressed  = false;
    }

    void keyEvent(HAB hab, rdr::U32 keysym, bool down)
    {
        lhab = hab;
        if ((keysym >= 32) && (keysym <= 255))
        {
            // ordinary Latin-1 character

            vnclog.Print(LL_INTINFO,
                       "latin-1 key: keysym %d(0x%x) down %d\n",
                       keysym, keysym, down);

            doKeyboardEvent(0,keysym, down ? 0 : KEYEVENTF_KEYUP);
        }
        else
        {
            // see if it's a recognised keyboard key, otherwise ignore it
            if (vkMap.find(keysym) == vkMap.end())
            {
                vnclog.Print(LL_INTWARN, "ignoring unknown keysym %d\n",keysym);
                return;
            }
            BYTE vkCode = vkMap[keysym];
            DWORD flags = 0;
            //if (extendedMap[keysym]) flags |= KEYEVENTF_EXTENDEDKEY;
            if (!down) flags |= KEYEVENTF_KEYUP;

            vnclog.Print(LL_INTINFO,
                         "keyboard key: keysym %d(0x%x) vkCode 0x%x ext %d down %d\n",
                         keysym, keysym, vkCode, extendedMap[keysym], down);

            if ( vkCode == VK_CTRL )  ctrlPressed = down;
            if ( vkCode == VK_ALT  )  altPressed  = down;

            if ( down && (vkCode == VK_DELETE) && ctrlPressed && ctrlPressed )
            {
                vnclog.Print(LL_INTINFO,"Ctrl-Alt-Del pressed\n");
                SimulateCtrlAltDel( hab );
                return;
            }

            doKeyboardEvent(vkCode,0, flags);
        }
    }

private:
  std::map<rdr::U32,rdr::U8> vkMap;
  std::map<rdr::U32,bool> extendedMap;
} key_mapper;

void vncKeymap::keyEvent(HAB hab,CARD32 keysym, bool down)
{
  key_mapper.keyEvent(hab, keysym, down);
}


void vncKeymap::ClearShiftKeys()
{
    // Clear the shift key states
    BYTE KeyState[256];
    WinSetKeyboardStateTable( HWND_DESKTOP , KeyState , FALSE );
    KeyState[ VK_ALT ]   = 0;
    KeyState[ VK_CTRL ]  = 0;
    KeyState[ VK_SHIFT ] = 0;
    WinSetKeyboardStateTable( HWND_DESKTOP , KeyState , TRUE );
}


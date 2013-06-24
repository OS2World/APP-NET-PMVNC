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
// If the source code for the VNC system is not available from the place
// whence you received this file, check http://www.uk.research.att.com/vnc or contact
// the authors on vnc@uk.research.att.com for information on obtaining it.


// vncMenu

// This class handles creation of a system-tray icon & menu

class vncMenu;

#if (!defined(_PMVNC_VNCMENU))
#define _PMVNC_VNCMENU

#include "stdhdrs.h"
#include "vncServer.h"
#include "vncProperties.h"


// SysTray support
#define WM_TRAYADDME (WM_USER+1)
#define WM_TRAYDELME (WM_USER+2)
#define WM_TRAYICON  (WM_USER+3)
#define WM_TRAYEXIT  (0xCD20)

#define SZAPP       "SystrayServer"
#define SZTOPIC     "TRAY"

static HWND hwndTrayServer = 0;

inline BOOL InitializeTrayApi(HWND hwnd)
{
    WinDdeInitiate(hwnd,(PSZ)SZAPP,(PSZ)SZTOPIC,NULL);
    return TRUE;
}

inline BOOL AnswerTrayApiDdeAck(MPARAM mp1)
{
    hwndTrayServer = (HWND)mp1;
    return TRUE;
}

inline BOOL AddTrayIcon(HWND hwnd,HPOINTER hptr) // hptr unused now and must be zero
{
    if(!hwndTrayServer)  return FALSE; // api not initialized
    WinPostMsg(hwndTrayServer,WM_TRAYADDME,(MPARAM)hwnd,(MPARAM)hptr);
    return TRUE;
}

inline BOOL ChangeTrayIcon(HWND hwnd,HPOINTER hptr) // hptr must be zero
{
    if(!hwndTrayServer)  return FALSE; // api not initialized
    WinPostMsg(hwndTrayServer,WM_TRAYICON,(MPARAM)hwnd,(MPARAM)hptr);
    return TRUE;
}

inline BOOL DeleteTrayIcon(HWND hwnd)
{
    if(!hwndTrayServer)  return FALSE; // api not initialized
    WinPostMsg(hwndTrayServer,WM_TRAYDELME,(MPARAM)hwnd,(MPARAM)0L);
    return TRUE;
}
// SysTray support - end


// Constants
extern const UINT MENU_PROPERTIES_SHOW;
extern const UINT MENU_DEFAULT_PROPERTIES_SHOW;
extern const UINT MENU_ABOUTBOX_SHOW;
extern const UINT MENU_SERVICEHELPER_MSG;
extern const UINT MENU_ADD_CLIENT_MSG;
extern const char *MENU_CLASS_NAME;

// The tray menu class itself
class vncMenu
{
public:
    vncMenu(vncServer *server);
    ~vncMenu();
    HWND GetHWND()  { return m_hwnd; };
protected:
    // Tray icon handling
    void FlashTrayIcon(BOOL flash);
    void vncMenu::showMenu();

    // Message handler for the tray window
    static MRESULT EXPENTRY WndProc(HWND hwnd, ULONG iMsg, MPARAM mp1, MPARAM mp2);

    // Fields
protected:
    // Check that the password has been set
    void CheckPassword();

    // The server that this tray icon handles
    vncServer       *m_server;

    // Properties object for this server
    vncProperties m_properties;

    HWND            m_hwnd;
    HWND            m_hmenu;

    // The icon handles
    HPOINTER        m_pmvnc_icon;
    HPOINTER        m_flash_icon;
};


#endif  // _PMVNC_VNCMENU



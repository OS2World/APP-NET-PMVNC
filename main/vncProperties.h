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


// vncProperties

// Object implementing the Properties dialog for WinVNC.
// The Properties dialog is displayed whenever the user selects the
// Properties option from the system tray menu.
// The Properties dialog also takes care of loading the program
// settings and saving them on exit.

class vncProperties;

#if (!defined(_PMVNC_VNCPROPERTIES))
#define _PMVNC_VNCPROPERTIES

// Includes
#include "stdhdrs.h"
#include "vncServer.h"

// The vncProperties class itself
class vncProperties
{
public:
    // Constructor/destructor
    vncProperties();
    ~vncProperties();

    // Initialisation
    BOOL Init(vncServer *server);

    // The dialog box window proc
    static MRESULT EXPENTRY DialogProc(HWND hwnd, ULONG uMsg, MPARAM mp1, MPARAM mp2);
    static MRESULT EXPENTRY AdvPageDlgProc(HWND hwnd, ULONG uMsg, MPARAM mp1, MPARAM mp2);
    static MRESULT EXPENTRY GenPageDlgProc(HWND hwnd, ULONG uMsg, MPARAM mp1, MPARAM mp2);
    static MRESULT EXPENTRY UHPageDlgProc(HWND hwnd, ULONG uMsg, MPARAM mp1, MPARAM mp2);
    static MRESULT EXPENTRY MiscPageDlgProc(HWND hwnd, ULONG uMsg, MPARAM mp1, MPARAM mp2);

    // Display the properties dialog
    void Show(BOOL show);

    // Loading & saving of preferences
    void Load();
    void Save();

    // TRAY ICON MENU SETTINGS
    BOOL AllowProperties() {return m_allowproperties;};
    BOOL AllowShutdown() {return m_allowshutdown;};
    BOOL AllowEditClients() {return m_alloweditclients;};

    // Password handling
    static void LoadPassword(char *buffer);
    static void SavePassword(char *buffer);

    // String handling
    static char *LoadString(char *k);
    static void SaveString(char *k, const char *buffer);

    // Manipulate the registry settings
    static LONG LoadInt(char *key, LONG defval);
    static void SaveInt(char *key, LONG val);

    // Implementation
protected:
    // The server object to which this properties object is attached.
    vncServer *         m_server;

    // Tray icon menu settings
    BOOL                m_allowproperties;
    BOOL                m_allowshutdown;
    BOOL                m_alloweditclients;

    // Loading/saving all the user prefs
    void LoadUserPrefs();
    void SaveUserPrefs();

    // Making the loaded user prefs active
    void ApplyUserPrefs();

    BOOL m_returncode_valid;
    BOOL m_dlgvisible;

    // STORAGE FOR THE PROPERTIES PRIOR TO APPLICATION
    BOOL m_pref_SockConnect;
    BOOL m_pref_HTTPConnect;
    BOOL m_pref_AutoPortSelect;
    LONG m_pref_PortNumber;
    char m_pref_passwd[MAXPWLEN];
    UINT m_pref_QuerySetting;
    UINT m_pref_QueryTimeout;
    UINT m_pref_IdleTimeout;
    BOOL m_pref_EnableRemoteInputs;
    int m_pref_LockSettings;
    BOOL m_pref_PollUnderCursor;
    BOOL m_pref_PollForeground;
    BOOL m_pref_PollFullScreen;
    BOOL m_pref_PollConsoleOnly;
    BOOL m_pref_PollOnEventOnly;
    BOOL m_pref_UseTimer;
    BOOL m_pref_UseDeferred;
    int  m_pref_Colordepth15bit;
};

#endif // _PMVNC_VNCPROPERTIES

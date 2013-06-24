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
// If the source code for the VNC system is not available from the place
// whence you received this file, check http://www.uk.research.att.com/vnc or contact
// the authors on vnc@uk.research.att.com for information on obtaining it.


// vncProperties.cpp

// Implementation of the Properties dialog!

#include "stdhdrs.h"
#include <stdlib.h>

#include "PMVNC.h"
#include "vncProperties.h"
#include "vncServer.h"
#include "vncPasswd.h"

#include "res.h"

extern HAB hab;
extern HMQ hmq;

const char PMVNC_REGISTRY_KEY[] = "ER_PMVNCD";
const char NO_PASSWORD_WARN [] = "WARNING : Running PMVNC without setting a password is "
                                "a dangerous security risk!\n"
                                "Until you set a password, PMVNC will not accept incoming connections.";
const char NO_OVERRIDE_ERR [] = "This machine has been preconfigured with PMVNC settings, "
                                "which cannot be overridden by individual users.  "
                                "The preconfigured settings may be modified only by a System Administrator.";
const char NO_PASSWD_NO_OVERRIDE_ERR [] =
                                "No password has been set & this machine has been "
                                "preconfigured to prevent users from setting their own.\n"
                                "You must contact a System Administrator to configure PMVNC properly.";
const char NO_PASSWD_NO_OVERRIDE_WARN [] =
                                "WARNING : This machine has been preconfigured to allow un-authenticated\n"
                                "connections to be accepted and to prevent users from enabling authentication.";
const char NO_CURRENT_USER_ERR [] = "The PMVNC settings for the current user are unavailable at present.";


// Constructor & Destructor
vncProperties::vncProperties()
{
    m_alloweditclients = TRUE;
    m_allowproperties  = TRUE;
    m_allowshutdown    = TRUE;
    m_dlgvisible       = FALSE;
}

vncProperties::~vncProperties()
{
}

// Initialisation
BOOL vncProperties::Init(vncServer *server)
{
    // Save the server pointer
    m_server = server;

    // Load the settings from the registry
    Load();

    // If the password is empty then always show a dialog
    char passwd[MAXPWLEN];
    m_server->GetPassword(passwd);
    {
        vncPasswd::ToText plain(passwd);
        if (strlen(plain) == 0)
        {
            if (!m_allowproperties)
            {
                if(m_server->AuthRequired())
                {
                    WinMessageBox( HWND_DESKTOP , HWND_DESKTOP , (PSZ)NO_PASSWD_NO_OVERRIDE_ERR,
                                   (PSZ)"PMVNC Error", 10001 , MB_ERROR | MB_OK | MB_MOVEABLE );
                    WinPostQueueMsg( hmq , WM_QUIT , MPVOID , MPVOID );
                }
                else
                {
                    WinMessageBox( HWND_DESKTOP , HWND_DESKTOP , (PSZ)NO_PASSWD_NO_OVERRIDE_WARN,
                            (PSZ)"PMVNC Error", 10001 , MB_ERROR | MB_OK | MB_MOVEABLE );
                }
            }
            else
            {
                // If null passwords are not allowed, ensure that one is entered!
                if (m_server->AuthRequired())
                {
                    Show(TRUE);
                }
            }
        }
    }

    return TRUE;
}

// Dialog box handling functions
void vncProperties::Show(BOOL show)
{
    if (show)
    {
        if (!m_allowproperties)
        {
            // If the user isn't allowed to override the settings then tell them
            WinMessageBox( HWND_DESKTOP , HWND_DESKTOP , (PSZ)NO_OVERRIDE_ERR,
                           (PSZ)"PMVNC Error", 10001 , MB_ERROR | MB_OK | MB_MOVEABLE );
            return;
        }

        // Now, if the dialog is not already displayed, show it!
        if (!m_dlgvisible)
        {
            vnclog.Print(LL_INTINFO, VNCLOG("show Properties\n"));

            // Load in the settings relevant to the user or system
            Load();

            for (;;)
            {
                m_returncode_valid = FALSE;

                // Do the dialog box
                ULONG result = WinDlgBox( HWND_DESKTOP , HWND_DESKTOP , DialogProc ,
                                          NULLHANDLE , IDD_PROPERTIES , (PVOID)this );

                if (!m_returncode_valid)
                    result = DID_CANCEL;

                vnclog.Print(LL_INTINFO, VNCLOG("dialog result = %d\n"), result);

                if (result == DID_ERROR)
                {
                    // Dialog box failed, so quit
                    WinPostQueueMsg( hmq , WM_QUIT , MPVOID , MPVOID );
                    return;
                }

                // We're allowed to exit if the password is not empty
                char passwd[MAXPWLEN];
                m_server->GetPassword(passwd);
                {
                    vncPasswd::ToText plain(passwd);
                    if ((strlen(plain) != 0) || !m_server->AuthRequired())
                    break;
                }

                vnclog.Print(LL_INTERR, VNCLOG("warning - empty password\n"));

                // The password is empty, so if OK was used then redisplay the box,
                // otherwise, if CANCEL was used, close down WinVNC
                if (result == DID_CANCEL)
                {
                    vnclog.Print(LL_INTERR, VNCLOG("no password - QUITTING\n"));
                    WinPostQueueMsg( hmq , WM_QUIT , MPVOID , MPVOID );
                    return;
                }

                // If we reached here then OK was used & there is no password!
                WinMessageBox( HWND_DESKTOP , HWND_DESKTOP , (PSZ)NO_PASSWORD_WARN,
                      (PSZ)"PMVNC Warning", 10001 , MB_ICONEXCLAMATION | MB_OK | MB_MOVEABLE );
            }

            // Load in all the settings
            Load();
        }
    }
}


static HWND generalPage;
static HWND uhPage;
static HWND advPage;
static HWND miscPage;

static char *ah_txt = "This setting is used to specify a set of IP address "\
                      "templates which incoming connections must match in order "\
                      "to be accepted. By default, the template is empty and "\
                      "connections from all hosts are accepted. The template is "\
                      "of the form:\r+[ip-address-template]\r?[ip-address-template]"\
                      "\r-[ip-address-template]\rIn the above, [ip-address-template] "\
                      "represents the leftmost bytes of the desired stringified "\
                      "IP-address. For example, +158.97 would match both 158.97.12.10 "\
                      "and 158.97.14.2. Multiple match terms may be specified, delimited "\
                      "by the \":\" character. Terms appearing later in the template "\
                      "take precedence over earlier ones. e.g. -:+158.97: would filter "\
                      "out all incoming connections except those beginning with 158.97. "\
                      "Terms beginning with the \"?\" character are treated "\
                      "as indicating hosts from whom connections must be accepted at "\
                      "the server side via a dialog box.";

const char noPass[] = "Running PMVNC without a password authentication is "
                      "a dangerous security risk!\nYou are warned.";

static char font_helv[] = "8.Helv";
static char font_warpsans[] = "9.WarpSans";

MRESULT EXPENTRY vncProperties::AdvPageDlgProc( HWND hwnd ,ULONG uMsg, MPARAM mp1, MPARAM mp2 )
{
    vncProperties *_this = (vncProperties *)WinQueryWindowULong(hwnd, QWL_USER);
    switch (uMsg)
    {
    case WM_INITDLG:
        {
            WinSetWindowULong(hwnd, QWL_USER, (ULONG)mp2);
            _this = (vncProperties *)mp2;

            WinSendDlgItemMsg(hwnd, IDC_AUTHREQUIRED, BM_SETCHECK,
                              MPFROMSHORT( _this->m_server->AuthRequired() ), MPVOID);

            HWND ah = WinWindowFromID( hwnd , IDC_AUTHHOSTS );
            WinSendMsg(ah, EM_SETTEXTLIMIT,MPFROMSHORT( 200 ), MPVOID);
            if ( _this->m_server->AuthHosts() != NULL )
                WinSetWindowText(ah, (PSZ)_this->m_server->AuthHosts());

            HWND ah_note = WinWindowFromID( hwnd , IDC_AUTHHOSTS_HELP );
            WinSetPresParam( ah_note , PP_FONTNAMESIZE , sizeof( font_helv ) , (PVOID)font_helv );
            LONG ah_note_clrback = SYSCLR_DIALOGBACKGROUND;
            WinSetPresParam( ah_note , PP_BACKGROUNDCOLORINDEX , sizeof( ah_note_clrback ) , (PVOID)&ah_note_clrback );
            WinSendMsg( ah_note , MLM_SETTEXTLIMIT , MPFROMLONG( 1000 ) , MPVOID );
            WinSendMsg( ah_note , MLM_SETIMPORTEXPORT , MPFROMP( ah_txt ) , MPFROMLONG( strlen(ah_txt) ) );
            IPT iptOffset = 0;
            WinSendMsg( ah_note , MLM_IMPORT , MPFROMP( &iptOffset ) , MPFROMLONG( strlen(ah_txt) ) );

            WinSendDlgItemMsg(hwnd, IDC_USELOG, BM_SETCHECK, MPFROMSHORT( vnclog.GetLoggingOn() ), MPVOID);
            WinSendDlgItemMsg(hwnd, IDC_LOGLEVEL, EM_SETTEXTLIMIT, MPFROMSHORT( 2 ), MPVOID );
            WinSetDlgItemShort(hwnd, IDC_LOGLEVEL, vnclog.GetLevel(), FALSE );
            WinSendDlgItemMsg(hwnd, IDC_LOGFILENAME, EM_SETTEXTLIMIT, MPFROMSHORT( CCHMAXPATH ), MPVOID );
            WinSetDlgItemText(hwnd, IDC_LOGFILENAME, (PSZ)vnclog.GetFilename() );
            WinSendDlgItemMsg(hwnd, IDC_LOGAPPEND, BM_SETCHECK, MPFROMSHORT( vnclog.GetAppend() ), MPVOID);
            return (MRESULT)FALSE;
        }
    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1))
        {

        case DID_OK:
            {
                _this->m_server->SetAuthRequired( WinQueryButtonCheckstate(hwnd, IDC_AUTHREQUIRED) );

                char ah[201];
                if (WinQueryDlgItemText(hwnd, IDC_AUTHHOSTS, sizeof(ah), (PSZ)ah )!=0)
                    _this->m_server->SetAuthHosts(ah);
                else
                    _this->m_server->SetAuthHosts(NULL);

                bool useLog = WinQueryButtonCheckstate(hwnd, IDC_USELOG);
                SHORT v = 0;
                WinQueryDlgItemShort( hwnd , IDC_LOGLEVEL , &v , FALSE );
                vnclog.SetLevel( v );
                char lfn[ CCHMAXPATH ];
                WinQueryDlgItemText( hwnd, IDC_LOGFILENAME, sizeof( lfn ), (PSZ)lfn );
                if ( strcmp( lfn , "" ) == 0 )  strcpy( lfn , "pmvnc.log" );
                bool logAppend = WinQueryButtonCheckstate(hwnd, IDC_LOGAPPEND);
                if ( ( strcmp( vnclog.GetFilename() , lfn ) != 0 ) ||
                     ( vnclog.GetAppend() != logAppend ) )
                {
                    vnclog.SetFile( lfn , logAppend );
                }
                if ( vnclog.GetLoggingOn() != useLog )
                {
                    vnclog.SetLoggingOn( useLog );
                }
                return (MRESULT)FALSE;
            }

        case DID_CANCEL:
            return (MRESULT)FALSE;
        }
        break;
    case WM_CONTROL:
        switch (SHORT1FROMMP(mp1))
        {
        case IDC_AUTHREQUIRED:
            if ((SHORT2FROMMP(mp1)==BN_CLICKED) || (SHORT2FROMMP(mp1)==BN_DBLCLICKED))
            {
                if (!WinQueryButtonCheckstate(hwnd, IDC_AUTHREQUIRED))
                {
                    WinMessageBox( HWND_DESKTOP , hwnd , (PSZ)noPass,
                       (PSZ)"PMVNC Warning", 10001 , MB_ICONEXCLAMATION | MB_OK | MB_MOVEABLE );
                }
            }
            return (MRESULT)FALSE;
        case IDC_USELOG:
            if ((SHORT2FROMMP(mp1)==BN_CLICKED) || (SHORT2FROMMP(mp1)==BN_DBLCLICKED))
            {
                BOOL enableLog = WinQueryButtonCheckstate(hwnd, IDC_USELOG);
                WinEnableControl( hwnd , IDC_LOGLEVELLABEL , enableLog );
                WinEnableControl( hwnd , IDC_LOGLEVEL , enableLog );
                WinEnableControl( hwnd , IDC_LOGFILENAMELABEL , enableLog );
                WinEnableControl( hwnd , IDC_LOGFILENAME , enableLog );
                WinEnableControl( hwnd , IDC_LOGAPPEND , enableLog );
            }
            return (MRESULT)FALSE;
        }
        break;
    }
    return WinDefDlgProc( hwnd , uMsg , mp1 , mp2 );
}

MRESULT EXPENTRY vncProperties::GenPageDlgProc( HWND hwnd ,ULONG uMsg, MPARAM mp1, MPARAM mp2 )
{
    vncProperties *_this = (vncProperties *)WinQueryWindowULong(hwnd, QWL_USER);
    switch (uMsg)
    {
    case WM_INITDLG:
        {
            WinSetWindowULong(hwnd, QWL_USER, (ULONG)mp2);
            _this = (vncProperties *)mp2;

            // Initialise the properties controls
            WinSendDlgItemMsg(hwnd, IDC_CONNECT_SOCK, BM_SETCHECK,
                              MPFROMSHORT( _this->m_server->SockConnected() ), MPVOID);

            WinSendDlgItemMsg(hwnd, IDC_CONNECT_HTTP, BM_SETCHECK,
                              MPFROMSHORT( _this->m_server->HTTPConnectEnabled() ), MPVOID);

            WinSendDlgItemMsg(hwnd, IDC_AUTO_DISPLAY_NO, BM_SETCHECK,
                              MPFROMSHORT( _this->m_server->AutoPortSelect() ), MPVOID);

            WinEnableControl( hwnd, IDC_AUTO_DISPLAY_NO , _this->m_server->SockConnected() );

            HWND hPortNo = WinWindowFromID(hwnd, IDC_PORTNO);
            char disp[ 6 ];
            _itoa( PORT_TO_DISPLAY(_this->m_server->GetPort()) , disp , 10 );
            WinSetWindowText(hPortNo, (PSZ)disp);
            WinEnableWindow(hPortNo, _this->m_server->SockConnected()
                                     && !_this->m_server->AutoPortSelect());

            HWND hPassword = WinWindowFromID(hwnd, IDC_PASSWORD);
            WinSendMsg(hPassword, EM_SETTEXTLIMIT,MPFROMSHORT( MAXPWLEN ), MPVOID);
            WinEnableWindow( hPassword , _this->m_server->SockConnected() );

            // Get the password
            {
                char plain[MAXPWLEN+1];
                _this->m_server->GetPassword(plain);
                {
                    vncPasswd::ToText plainpwd(plain);
                    int length = strlen(plainpwd);
                    for (int i=0; i<length; i++)  plain[i] = i+1;
                    plain[length]=0;
                }
                WinSetWindowText(hPassword, (PSZ)plain);
            }

            // Remote input settings
            WinSendDlgItemMsg(hwnd, IDC_DISABLE_INPUTS, BM_SETCHECK,
                              MPFROMSHORT( !(_this->m_server->RemoteInputsEnabled()) ), MPVOID);

            // Lock settings
            HWND hLockSetting;
            switch (_this->m_server->LockSettings())
            {
                case 1:
                    hLockSetting = WinWindowFromID(hwnd, IDC_LOCKSETTING_LOCK);
                    break;
                case 2:
                    hLockSetting = WinWindowFromID(hwnd, IDC_LOCKSETTING_LOGOFF);
                    break;
                default:
                    hLockSetting = WinWindowFromID(hwnd, IDC_LOCKSETTING_NOTHING);
            };
            WinSendMsg(hLockSetting, BM_SETCHECK, MPFROMSHORT( TRUE ), MPVOID);

            return (MRESULT)FALSE;
        }
    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1))
        {

        case DID_OK:
            {
                // Save the password
                char passwd[MAXPWLEN+1];
                if (WinQueryDlgItemText(hwnd, IDC_PASSWORD,MAXPWLEN+1, (PSZ)passwd) == 0)
                {
                    vncPasswd::FromClear crypt;
                    _this->m_server->SetPassword(crypt);
                }
                else
                {
                    char current_pwd[MAXPWLEN+1];
                    _this->m_server->GetPassword(current_pwd);
                    vncPasswd::ToText current(current_pwd);

                    BOOL password_changed = FALSE;
                    for (int i=0; i<MAXPWLEN; i++)
                    {
                        if (passwd[i] != i+1) password_changed = TRUE;
                        if ((passwd[i] >= 1) && (passwd[i] <= MAXPWLEN)) passwd[i] = current[passwd[i]-1];
                    }
                    if (password_changed)
                    {
                        vnclog.Print(LL_INTINFO, VNCLOG("password changed\n"));
                        vncPasswd::FromText crypt(passwd);
                        _this->m_server->SetPassword(crypt);
                    }
                }

                // Save the new settings to the server
                _this->m_server->SetAutoPortSelect(
                                   WinQueryButtonCheckstate(hwnd, IDC_AUTO_DISPLAY_NO) );

                // only save the port number if we're not auto selecting!
                if (!_this->m_server->AutoPortSelect())
                {
                    char disp[ 6 ];
                    WinQueryDlgItemText(hwnd, IDC_PORTNO, sizeof(disp), (PSZ)disp );
                    UINT portno = atoi( disp );
                    _this->m_server->SetPort(DISPLAY_TO_PORT(portno));
                }

                _this->m_server->SockConnect(WinQueryButtonCheckstate(hwnd, IDC_CONNECT_SOCK));

                _this->m_server->EnableHTTPConnect(WinQueryButtonCheckstate(hwnd, IDC_CONNECT_HTTP));

                // Remote input stuff
                _this->m_server->EnableRemoteInputs(!WinQueryButtonCheckstate(hwnd, IDC_DISABLE_INPUTS));

                // Lock settings handling
                if (WinQueryButtonCheckstate(hwnd, IDC_LOCKSETTING_LOCK)) {
                    _this->m_server->SetLockSettings(1);
                } else if (WinQueryButtonCheckstate(hwnd, IDC_LOCKSETTING_LOGOFF)) {
                    _this->m_server->SetLockSettings(2);
                } else {
                    _this->m_server->SetLockSettings(0);
                }

                return (MRESULT)FALSE;
            }

        case DID_CANCEL:
            return (MRESULT)FALSE;
        }
        break;
    case WM_CONTROL:
        switch (SHORT1FROMMP(mp1))
        {
        case IDC_CONNECT_SOCK:
            // The user has clicked on the socket connect tickbox
            {
                BOOL connectsockon = WinQueryButtonCheckstate(hwnd, IDC_CONNECT_SOCK);

                WinEnableControl(hwnd, IDC_AUTO_DISPLAY_NO, connectsockon);

                WinEnableControl(hwnd, IDC_PORTNO,
                       connectsockon && !WinQueryButtonCheckstate(hwnd, IDC_AUTO_DISPLAY_NO) );

                WinEnableControl(hwnd, IDC_PASSWORD, connectsockon);
            }
            return (MRESULT)FALSE;

        case IDC_AUTO_DISPLAY_NO:
            // User has toggled the Auto Port Select feature.
            // If this is in use, then we don't allow the Display number field
            // to be modified!
            {
                // Get the auto select button
                // Should the portno field be modifiable?
                BOOL enable = !WinQueryButtonCheckstate(hwnd, IDC_AUTO_DISPLAY_NO);

                // Set the state
                WinEnableControl(hwnd, IDC_PORTNO, enable);
            }
            return (MRESULT)FALSE;

        }
        break;
    }
    return WinDefDlgProc( hwnd , uMsg , mp1 , mp2 );
}

MRESULT EXPENTRY vncProperties::UHPageDlgProc( HWND hwnd ,ULONG uMsg, MPARAM mp1, MPARAM mp2 )
{
    vncProperties *_this = (vncProperties *)WinQueryWindowULong(hwnd, QWL_USER);
    switch (uMsg)
    {
    case WM_INITDLG:
        {
            WinSetWindowULong(hwnd, QWL_USER, (ULONG)mp2);
            _this = (vncProperties *)mp2;


            // Set the polling options
            WinSendDlgItemMsg(hwnd, IDC_POLL_FULLSCREEN, BM_SETCHECK,
                              MPFROMSHORT( _this->m_server->PollFullScreen() ), MPVOID);

            WinSendDlgItemMsg(hwnd, IDC_POLL_FOREGROUND, BM_SETCHECK,
                              MPFROMSHORT( _this->m_server->PollForeground() ), MPVOID);

            WinSendDlgItemMsg(hwnd, IDC_POLL_UNDER_CURSOR, BM_SETCHECK,
                              MPFROMSHORT( _this->m_server->PollUnderCursor() ), MPVOID);

            HWND hPollConsoleOnly = WinWindowFromID(hwnd, IDC_CONSOLE_ONLY);
            WinSendMsg(hPollConsoleOnly, BM_SETCHECK,
                       MPFROMSHORT( _this->m_server->PollConsoleOnly() ), MPVOID);
            WinEnableWindow(hPollConsoleOnly,
                _this->m_server->PollUnderCursor() || _this->m_server->PollForeground() );

            HWND hPollOnEventOnly = WinWindowFromID(hwnd, IDC_ONEVENT_ONLY);
            WinSendMsg(hPollOnEventOnly, BM_SETCHECK,
                       MPFROMSHORT( _this->m_server->PollOnEventOnly() ), MPVOID);
            WinEnableWindow(hPollOnEventOnly,
                _this->m_server->PollUnderCursor() || _this->m_server->PollForeground());

            WinSendDlgItemMsg(hwnd, IDC_USETIMER, BM_SETCHECK,
                              MPFROMSHORT( _this->m_server->UseTimer() ), MPVOID);
            WinSendDlgItemMsg(hwnd, IDC_USEDEFERRED, BM_SETCHECK,
                              MPFROMSHORT( _this->m_server->DeferredUpdates() ), MPVOID);
            return (MRESULT)FALSE;
        }
    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1))
        {

        case DID_OK:
            {
                // Handle the polling stuff
                _this->m_server->PollFullScreen(WinQueryButtonCheckstate(hwnd, IDC_POLL_FULLSCREEN));
                _this->m_server->PollForeground(WinQueryButtonCheckstate(hwnd, IDC_POLL_FOREGROUND));
                _this->m_server->PollUnderCursor(WinQueryButtonCheckstate(hwnd, IDC_POLL_UNDER_CURSOR));
                _this->m_server->PollConsoleOnly(WinQueryButtonCheckstate(hwnd, IDC_CONSOLE_ONLY));
                _this->m_server->PollOnEventOnly(WinQueryButtonCheckstate(hwnd, IDC_ONEVENT_ONLY));
                _this->m_server->SetUseTimer(WinQueryButtonCheckstate(hwnd, IDC_USETIMER));
                _this->m_server->SetDeferredUpdates(WinQueryButtonCheckstate(hwnd, IDC_USEDEFERRED));
                return (MRESULT)FALSE;
            }

        case DID_CANCEL:
            return (MRESULT)FALSE;
        }
        break;
    case WM_CONTROL:
        switch (SHORT1FROMMP(mp1))
        {
        case IDC_POLL_FOREGROUND:
        case IDC_POLL_UNDER_CURSOR:
            // User has clicked on one of the polling mode buttons
            // affected by the pollconsole and pollonevent options
            {
                // Get the poll-mode buttons and determine whether to enable
                // the modifier options
                BOOL enabled = WinQueryButtonCheckstate(hwnd, IDC_POLL_FOREGROUND) ||
                                WinQueryButtonCheckstate(hwnd, IDC_POLL_UNDER_CURSOR);

                WinEnableControl(hwnd, IDC_CONSOLE_ONLY, enabled);

                WinEnableControl(hwnd, IDC_ONEVENT_ONLY, enabled);
            }
            return (MRESULT)FALSE;
        }
        break;
    }
    return WinDefDlgProc( hwnd , uMsg , mp1 , mp2 );
}

MRESULT EXPENTRY vncProperties::MiscPageDlgProc( HWND hwnd ,ULONG uMsg, MPARAM mp1, MPARAM mp2 )
{
    vncProperties *_this = (vncProperties *)WinQueryWindowULong(hwnd, QWL_USER);
    switch (uMsg)
    {
    case WM_INITDLG:
        {
            WinSetWindowULong(hwnd, QWL_USER, (ULONG)mp2);
            _this = (vncProperties *)mp2;

            WinSetDlgItemShort(hwnd, IDC_IDLETIMEOUT, _this->m_server->AutoIdleDisconnectTimeout()/60, FALSE);
            WinSendDlgItemMsg(hwnd, IDC_15BITWA, BM_SETCHECK, MPFROMSHORT( _this->m_server->Colordepth15bit() ), MPVOID);

            return (MRESULT)FALSE;
        }
    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1))
        {

        case DID_OK:
            {
                SHORT r = 0;
                WinQueryDlgItemShort(hwnd, IDC_IDLETIMEOUT, &r, FALSE);
                _this->m_server->SetAutoIdleDisconnectTimeout( r*60 );
                _this->m_server->SetColordepth15bit(WinQueryButtonCheckstate(hwnd, IDC_15BITWA));
                return (MRESULT)FALSE;
            }

        case DID_CANCEL:
            return (MRESULT)FALSE;
        }
        break;
    }
    return WinDefDlgProc( hwnd , uMsg , mp1 , mp2 );
}

MRESULT EXPENTRY vncProperties::DialogProc( HWND hwnd ,ULONG uMsg, MPARAM mp1, MPARAM mp2 )
{
    // We use the dialog-box's USERDATA to store a _this pointer
    // This is set only once WM_INITDLG has been recieved, though!
    vncProperties *_this = (vncProperties *)WinQueryWindowULong(hwnd, QWL_USER);

    switch (uMsg)
    {

    case WM_INITDLG:
        {
            // Retrieve the Dialog box parameter and use it as a pointer
            // to the calling vncProperties object
            WinSetWindowULong(hwnd, QWL_USER, (ULONG)mp2);
            _this = (vncProperties *)mp2;
            _this->m_dlgvisible = TRUE;

            HWND ntbhwnd = WinWindowFromID( hwnd , IDC_SETNTB );
            WinSendMsg( ntbhwnd , BKM_SETDIMENSIONS , MPFROM2SHORT( 100 , 24 ) , (MPARAM)BKA_MAJORTAB );
            WinSendMsg( ntbhwnd , BKM_SETDIMENSIONS , MPFROM2SHORT( 0 , 0 ) , (MPARAM)BKA_MINORTAB );
            WinSendMsg( ntbhwnd , BKM_SETDIMENSIONS , MPFROM2SHORT( 24 , 24 ) , (MPARAM)BKA_PAGEBUTTON );
            WinSendMsg( ntbhwnd , BKM_SETNOTEBOOKCOLORS , (MPARAM)SYSCLR_DIALOGBACKGROUND , (MPARAM)BKA_BACKGROUNDMAJORCOLORINDEX );
            WinSendMsg( ntbhwnd , BKM_SETNOTEBOOKCOLORS , (MPARAM)SYSCLR_DIALOGBACKGROUND , (MPARAM)BKA_BACKGROUNDPAGECOLORINDEX );
            // if OS/2 ver 4 or higher set notebook font to WarpSans
            struct os2version { ULONG major; ULONG minor; };
            os2version v;
            DosQuerySysInfo( QSV_VERSION_MAJOR , QSV_VERSION_MINOR , &v , sizeof( v ) );
            if ( ( v.major > 20 ) || ( ( v.major == 20 ) && ( v.minor >= 40 ) ) )
            {
                WinSetPresParam( ntbhwnd , PP_FONTNAMESIZE , sizeof( font_warpsans ) , (PVOID)font_warpsans );
            }


            ULONG pid;
            // --- General ---
            generalPage = WinLoadDlg( ntbhwnd, ntbhwnd, GenPageDlgProc, NULLHANDLE, IDP_GENERAL, _this );
            pid = (ULONG)WinSendMsg( ntbhwnd , BKM_INSERTPAGE , (MPARAM)0 , MPFROM2SHORT( BKA_AUTOPAGESIZE | BKA_MAJOR | BKA_STATUSTEXTON , BKA_LAST ) );
            WinSendMsg( ntbhwnd , BKM_SETTABTEXT , (MPARAM)pid , (MPARAM)"General" );
            WinSendMsg( ntbhwnd , BKM_SETSTATUSLINETEXT , (MPARAM)pid , (MPARAM)"General settings" );
            WinSendMsg( ntbhwnd , BKM_SETPAGEWINDOWHWND , (MPARAM)pid , (MPARAM)generalPage );
            // --- Update handling ---
            uhPage = WinLoadDlg( ntbhwnd, ntbhwnd, UHPageDlgProc, NULLHANDLE, IDP_UPDHAN, _this );
            pid = (ULONG)WinSendMsg( ntbhwnd , BKM_INSERTPAGE , (MPARAM)0 , MPFROM2SHORT( BKA_AUTOPAGESIZE | BKA_MAJOR | BKA_STATUSTEXTON , BKA_LAST ) );
            WinSendMsg( ntbhwnd , BKM_SETTABTEXT , (MPARAM)pid , (MPARAM)"Update handling" );
            WinSendMsg( ntbhwnd , BKM_SETSTATUSLINETEXT , (MPARAM)pid , (MPARAM)"Update handling" );
            WinSendMsg( ntbhwnd , BKM_SETPAGEWINDOWHWND , (MPARAM)pid , (MPARAM)uhPage );
            // --- Advanced ---
            advPage = WinLoadDlg( ntbhwnd, ntbhwnd, AdvPageDlgProc, NULLHANDLE, IDP_ADV, _this );
            pid = (ULONG)WinSendMsg( ntbhwnd , BKM_INSERTPAGE , (MPARAM)0 , MPFROM2SHORT( BKA_AUTOPAGESIZE | BKA_MAJOR | BKA_STATUSTEXTON , BKA_LAST ) );
            WinSendMsg( ntbhwnd , BKM_SETTABTEXT , (MPARAM)pid , (MPARAM)"Advanced" );
            WinSendMsg( ntbhwnd , BKM_SETSTATUSLINETEXT , (MPARAM)pid , (MPARAM)"Advanced settings" );
            WinSendMsg( ntbhwnd , BKM_SETPAGEWINDOWHWND , (MPARAM)pid , (MPARAM)advPage );
            // --- Misc ---
            miscPage = WinLoadDlg( ntbhwnd, ntbhwnd, MiscPageDlgProc, NULLHANDLE, IDP_MISC, _this );
            pid = (ULONG)WinSendMsg( ntbhwnd , BKM_INSERTPAGE , (MPARAM)0 , MPFROM2SHORT( BKA_AUTOPAGESIZE | BKA_MAJOR | BKA_STATUSTEXTON , BKA_LAST ) );
            WinSendMsg( ntbhwnd , BKM_SETTABTEXT , (MPARAM)pid , (MPARAM)"Misc" );
            WinSendMsg( ntbhwnd , BKM_SETSTATUSLINETEXT , (MPARAM)pid , (MPARAM)"Miscellaneous settings" );
            WinSendMsg( ntbhwnd , BKM_SETPAGEWINDOWHWND , (MPARAM)pid , (MPARAM)miscPage );

            return (MRESULT)FALSE;
        }
    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1))
        {

        case DID_OK:
            {
                WinSendMsg( generalPage , WM_COMMAND , MPFROMSHORT( DID_OK ) , MPFROM2SHORT( CMDSRC_OTHER , TRUE ) );
                WinSendMsg( uhPage , WM_COMMAND , MPFROMSHORT( DID_OK ) , MPFROM2SHORT( CMDSRC_OTHER , TRUE ) );
                WinSendMsg( advPage , WM_COMMAND , MPFROMSHORT( DID_OK ) , MPFROM2SHORT( CMDSRC_OTHER , TRUE ) );
                WinSendMsg( miscPage , WM_COMMAND , MPFROMSHORT( DID_OK ) , MPFROM2SHORT( CMDSRC_OTHER , TRUE ) );
                // And to the registry
                _this->Save();

                vnclog.Print(LL_INTINFO, VNCLOG("enddialog (OK)\n"));
                _this->m_returncode_valid = TRUE;
                WinDismissDlg( hwnd , DID_OK );
                _this->m_dlgvisible = FALSE;
                return (MRESULT)FALSE;
            }

        case DID_CANCEL:
            vnclog.Print(LL_INTINFO, VNCLOG("enddialog (CANCEL)\n"));
            _this->m_returncode_valid = TRUE;
            WinDismissDlg( hwnd , DID_CANCEL );
            _this->m_dlgvisible = FALSE;
            return (MRESULT)FALSE;
        }
        break;
    }
    return WinDefDlgProc( hwnd , uMsg , mp1 , mp2 );
}

// Functions to load & save the settings
LONG vncProperties::LoadInt(char *key, LONG defval)
{
    return PrfQueryProfileInt(HINI_USERPROFILE, (PSZ)PMVNC_REGISTRY_KEY, (PSZ)key, defval);
}

void vncProperties::LoadPassword(char *buffer)
{
    ULONG maxpw = MAXPWLEN;
    PrfQueryProfileData(HINI_USERPROFILE, (PSZ)PMVNC_REGISTRY_KEY,
             (PSZ)"Password", (PSZ)buffer, &maxpw );
}

#define PRFMAXSLEN 4096
char *vncProperties::LoadString(char *k)
{
    char *tmpbuf = new char[ PRFMAXSLEN ];
    ULONG l = PrfQueryProfileString(HINI_USERPROFILE, (PSZ)PMVNC_REGISTRY_KEY,
                                    (PSZ)k, (PSZ)"", (PSZ)tmpbuf, PRFMAXSLEN);
    if ( l < 2 )
    {
        delete tmpbuf;
        return NULL;
    }

    char *buffer = new char[ l ];
    strcpy( buffer , tmpbuf );
    delete tmpbuf;

    return buffer;
}

void vncProperties::Load()
{
    if (m_dlgvisible) {
        vnclog.Print(LL_INTWARN, VNCLOG("service helper invoked while Properties panel displayed\n"));
        return;
    }

    // Logging/debugging prefs
    vnclog.Print(LL_INTINFO, VNCLOG("loading settings\n"));
    //vnclog.SetMode(LoadInt( "DebugMode", 0));
    //vnclog.SetLevel(LoadInt( "DebugLevel", 0));

    // Authentication required, loopback allowed, loopbackOnly
    m_server->SetLoopbackOnly(LoadInt( "LoopbackOnly", false));
    if (m_server->LoopbackOnly())
        m_server->SetLoopbackOk(true);
    else
        m_server->SetLoopbackOk(LoadInt( "AllowLoopback", false));
    m_server->SetAuthRequired(LoadInt( "AuthRequired", true));
    m_server->SetConnectPriority(LoadInt( "ConnectPriority", 0));
    if (!m_server->LoopbackOnly())
    {
        char *authhosts = LoadString("AuthHosts");
        if (authhosts != 0) {
            m_server->SetAuthHosts(authhosts);
            delete [] authhosts;
        } else {
            m_server->SetAuthHosts(0);
        }
    } else {
        m_server->SetAuthHosts(0);
    }

    // LOAD THE USER PREFERENCES

    // Set the default user prefs
    vnclog.Print(LL_INTINFO, VNCLOG("clearing user settings\n"));
    m_pref_HTTPConnect = TRUE;
    m_pref_AutoPortSelect=TRUE;
    m_pref_PortNumber=5900;
    m_pref_SockConnect=TRUE;
    {
        vncPasswd::FromClear crypt;
        memcpy(m_pref_passwd, crypt, MAXPWLEN);
    }
    m_pref_QuerySetting=2;
    m_pref_QueryTimeout=10;
    m_pref_IdleTimeout=60*60; // 60 minutes by default
    m_pref_EnableRemoteInputs=TRUE;
    m_pref_LockSettings=-1;
    m_pref_PollUnderCursor=FALSE;
    m_pref_PollForeground=TRUE;
    m_pref_PollFullScreen=FALSE;
    m_pref_PollConsoleOnly=TRUE;
    m_pref_PollOnEventOnly=FALSE;
    m_pref_UseTimer = TRUE;
    m_pref_UseDeferred = FALSE;
    m_pref_Colordepth15bit = FALSE;

    m_alloweditclients = TRUE;
    m_allowshutdown = TRUE;
    m_allowproperties = TRUE;

    // Load the local prefs for this user
    vnclog.Print(LL_INTINFO, VNCLOG("loading local settings\n"));
    LoadUserPrefs();
    m_allowshutdown = LoadInt( "AllowShutdown", m_allowshutdown);
    m_allowproperties = LoadInt( "AllowProperties   ", m_allowproperties);
    m_alloweditclients = LoadInt( "AllowEditClients", m_alloweditclients);

    // Make the loaded settings active..
    ApplyUserPrefs();
}

void vncProperties::LoadUserPrefs()
{
    // LOAD USER PREFS FROM THE SELECTED KEY

    // Connection prefs
    m_pref_SockConnect=LoadInt( "SocketConnect", m_pref_SockConnect);
    m_pref_HTTPConnect=LoadInt( "HTTPConnect", m_pref_HTTPConnect);
    m_pref_AutoPortSelect=LoadInt( "AutoPortSelect", m_pref_AutoPortSelect);
    m_pref_PortNumber=LoadInt( "PortNumber", m_pref_PortNumber);
    m_pref_IdleTimeout=LoadInt( "IdleTimeout", m_pref_IdleTimeout);

    // Connection querying settings
    m_pref_QuerySetting=LoadInt( "QuerySetting", m_pref_QuerySetting);
    m_pref_QueryTimeout=LoadInt( "QueryTimeout", m_pref_QueryTimeout);

    // Load the password
    LoadPassword(m_pref_passwd);

    // Remote access prefs
    m_pref_EnableRemoteInputs=LoadInt( "InputsEnabled", m_pref_EnableRemoteInputs);
    m_pref_LockSettings=LoadInt( "LockSetting", m_pref_LockSettings);

    // Polling prefs
    m_pref_PollUnderCursor=LoadInt( "PollUnderCursor", m_pref_PollUnderCursor);
    m_pref_PollForeground=LoadInt( "PollForeground", m_pref_PollForeground);
    m_pref_PollFullScreen=LoadInt( "PollFullScreen", m_pref_PollFullScreen);
    m_pref_PollConsoleOnly=LoadInt( "OnlyPollConsole", m_pref_PollConsoleOnly);
    m_pref_PollOnEventOnly=LoadInt( "OnlyPollOnEvent", m_pref_PollOnEventOnly);
    m_pref_UseTimer=LoadInt( "UseTimer", m_pref_UseTimer);
    m_pref_UseDeferred=LoadInt( "UseDeferred", m_pref_UseDeferred);

    m_pref_Colordepth15bit=LoadInt( "Colordepth15bit", m_pref_Colordepth15bit);
}

void vncProperties::ApplyUserPrefs()
{
    // APPLY THE CACHED PREFERENCES TO THE SERVER

    // Update the connection querying settings
    m_server->SetQuerySetting(m_pref_QuerySetting);
    m_server->SetQueryTimeout(m_pref_QueryTimeout);
    m_server->SetAutoIdleDisconnectTimeout(m_pref_IdleTimeout);

    // Is the listening socket closing?
    if (!m_pref_SockConnect)
        m_server->SockConnect(m_pref_SockConnect);
    m_server->EnableHTTPConnect(m_pref_HTTPConnect);

    // Are inputs being disabled?
    if (!m_pref_EnableRemoteInputs)
        m_server->EnableRemoteInputs(m_pref_EnableRemoteInputs);

    // Update the password
    m_server->SetPassword(m_pref_passwd);

    // Now change the listening port settings
    m_server->SetAutoPortSelect(m_pref_AutoPortSelect);
    if (!m_pref_AutoPortSelect)
        m_server->SetPort(m_pref_PortNumber);
    m_server->SockConnect(m_pref_SockConnect);

    // Remote access prefs
    m_server->EnableRemoteInputs(m_pref_EnableRemoteInputs);
    m_server->SetLockSettings(m_pref_LockSettings);

    // Polling prefs
    m_server->PollUnderCursor(m_pref_PollUnderCursor);
    m_server->PollForeground(m_pref_PollForeground);
    m_server->PollFullScreen(m_pref_PollFullScreen);
    m_server->PollConsoleOnly(m_pref_PollConsoleOnly);
    m_server->PollOnEventOnly(m_pref_PollOnEventOnly);

    m_server->SetUseTimer(m_pref_UseTimer);
    m_server->SetDeferredUpdates(m_pref_UseDeferred);
    m_server->SetColordepth15bit(m_pref_Colordepth15bit);
}

void vncProperties::SaveInt(char *key, LONG val)
{
    char tmp[ 20 ];
    _itoa( val , tmp , 10 );
    PrfWriteProfileString(HINI_USERPROFILE, (PSZ)PMVNC_REGISTRY_KEY, (PSZ)key, (PSZ)tmp);
}

void vncProperties::SaveString(char *k, const char *buffer)
{
    PrfWriteProfileString(HINI_USERPROFILE, (PSZ)PMVNC_REGISTRY_KEY, (PSZ)k, (PSZ)buffer);
}

void vncProperties::SavePassword( char *buffer)
{
    PrfWriteProfileData(HINI_USERPROFILE, (PSZ)PMVNC_REGISTRY_KEY, (PSZ)"Password", (PSZ)buffer, MAXPWLEN);
}

void vncProperties::Save()
{
    if (!m_allowproperties)
        return;

    // SAVE PER-USER PREFS IF ALLOWED
    SaveUserPrefs();
}

void vncProperties::SaveUserPrefs()
{
    // SAVE THE PER USER PREFS
    vnclog.Print(LL_INTINFO, VNCLOG("saving current settings to registry\n"));

    // Connection prefs
    SaveInt( "SocketConnect", m_server->SockConnected());
    SaveInt( "HTTPConnect", m_server->HTTPConnectEnabled());
    SaveInt( "AutoPortSelect", m_server->AutoPortSelect());
    if (!m_server->AutoPortSelect())
        SaveInt( "PortNumber", m_server->GetPort());
    SaveInt( "InputsEnabled", m_server->RemoteInputsEnabled());
    SaveInt( "IdleTimeout", m_server->AutoIdleDisconnectTimeout());

    // Connection querying settings
    SaveInt( "QuerySetting", m_server->QuerySetting());
    SaveInt( "QueryTimeout", m_server->QueryTimeout());

    // Lock settings
    SaveInt( "LockSetting", m_server->LockSettings());

    // Save the password
    char passwd[MAXPWLEN];
    m_server->GetPassword(passwd);
    SavePassword( passwd);

    // Polling prefs
    SaveInt( "PollUnderCursor", m_server->PollUnderCursor());
    SaveInt( "PollForeground", m_server->PollForeground());
    SaveInt( "PollFullScreen", m_server->PollFullScreen());

    SaveInt( "OnlyPollConsole", m_server->PollConsoleOnly());
    SaveInt( "OnlyPollOnEvent", m_server->PollOnEventOnly());

    SaveInt( "UseTimer", m_server->UseTimer());
    SaveInt( "UseDeferred", m_server->DeferredUpdates());

    // Advanced
    SaveInt( "AuthRequired", m_server->AuthRequired());
    SaveString( "AuthHosts", m_server->AuthHosts());

    // Logging
    SaveInt("LogLevel", vnclog.GetLevel());
    SaveString("LogFilename", vnclog.GetFilename());
    SaveInt("LogAppend", vnclog.GetAppend());
    SaveInt("Logging", vnclog.GetLoggingOn());

    SaveInt( "Colordepth15bit", m_server->Colordepth15bit());
}


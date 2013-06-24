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


// vncMenu

// Implementation of a system tray icon & menu for PMVNC

#include "stdhdrs.h"
#include <netdb.h>

#include "res.h"
#include "PMVNC.h"
#include "vncConnDialog.h"

// Header
#include "vncMenu.h"

extern HAB hab;
extern HMQ hmq;

// Constants
const char *MENU_CLASS_NAME = "PMVNC.Tray.Icon";

void GetIPAddrString(char *buffer, int buflen);
extern "C" void setLinkPointer( HPOINTER hp );
extern "C" void toLink( HWND hwnd );

// Implementation

vncMenu::vncMenu(vncServer *server)
{
    // Save the server pointer
    m_server = server;

    // Create a dummy window to handle tray icon messages
    WinRegisterClass( hab , (PSZ)MENU_CLASS_NAME , vncMenu::WndProc , 0 , sizeof( ULONG ) );
    ULONG fcf = FCF_TITLEBAR;
    HWND mframe = WinCreateStdWindow( HWND_DESKTOP , 0 , &fcf , (PSZ)MENU_CLASS_NAME ,
                                      (PSZ)"" , 0 , NULLHANDLE , 0 , &m_hwnd );

    if ( mframe == NULLHANDLE )
    {
        vnclog.Print(LL_INTERR, VNCLOG("unable to WinCreateStdWindow:%x\n"), WinGetLastError(hab));
        WinPostQueueMsg( hmq , WM_QUIT , MPVOID , MPVOID );
        return;
    }

    // Timer to trigger icon updating
    WinStartTimer(hab , m_hwnd, 1, 5000);

    // record which client created this window
    WinSetWindowULong(m_hwnd, QWL_USER, (ULONG)this);

    // Ask the server object to notify us of stuff
    server->AddNotify(m_hwnd);

    // Initialise the properties dialog object
    if (!m_properties.Init(m_server))
    {
        vnclog.Print(LL_INTERR, VNCLOG("unable to initialise Properties dialog\n"));
        WinPostQueueMsg( hmq , WM_QUIT , MPVOID , MPVOID );
        return;
    }

    // Load the icons for the tray
    m_pmvnc_icon = WinLoadPointer( HWND_DESKTOP , NULLHANDLE , IDI_PMVNC );
    m_flash_icon = WinLoadPointer( HWND_DESKTOP , NULLHANDLE , IDI_FLASH );
    WinSendMsg( mframe , WM_SETICON , (MPARAM)m_pmvnc_icon , MPVOID );

    char tooltip[ 256 ];
    strcpy( tooltip , "PMVNC: " );
    GetIPAddrString( tooltip + 7 , 255-7 );
    WinSetWindowText( mframe , (PSZ)tooltip );

    // Load the popup menu
    m_hmenu = WinLoadMenu(m_hwnd, NULLHANDLE , IDR_TRAYMENU );

    // Install the tray icon!
    InitializeTrayApi( m_hwnd );
}

vncMenu::~vncMenu()
{
    // Remove the tray icon
    DeleteTrayIcon( m_hwnd );

    // Tell the server to stop notifying us!
    if (m_server != NULLHANDLE)
        m_server->RemNotify(m_hwnd);
}

void vncMenu::FlashTrayIcon(BOOL flash)
{
    HWND frame = WinQueryWindow( m_hwnd , QW_PARENT );
    HPOINTER ico = flash ? m_flash_icon : m_pmvnc_icon;
    WinSendMsg( frame , WM_SETICON , (MPARAM)ico , MPVOID );
    ChangeTrayIcon( m_hwnd , NULLHANDLE );
}

void vncMenu::showMenu()
{
    POINTL ptl;
    WinQueryPointerPos( HWND_DESKTOP , &ptl );
    WinMapWindowPoints( HWND_DESKTOP , m_hwnd , &ptl , 1 );
    WinPopupMenu( m_hwnd , m_hwnd , m_hmenu , ptl.x , ptl.y , 0 ,
                  PU_HCONSTRAIN | PU_VCONSTRAIN | PU_MOUSEBUTTON1 |
                  PU_MOUSEBUTTON2 | PU_KEYBOARD );
}


// Get the local ip addresses as a human-readable string.
// If more than one, then with \n between them.
// If not available, then gets a message to that effect.
void GetIPAddrString(char *buffer, int buflen)
{
    char namebuf[256];

    if (gethostname(namebuf, 256) != 0) {
        strncpy(buffer, "Host name unavailable", buflen);
        return;
    };

    hostent *ph = gethostbyname(namebuf);
    if (!ph) {
        strncpy(buffer, "IP address unavailable", buflen);
        return;
    };

    *buffer = '\0';
    char digtxt[5];
    for (int i = 0; ph->h_addr_list[i]; i++) {
        for (int j = 0; j < ph->h_length; j++) {
            sprintf(digtxt, "%d.", (unsigned char) ph->h_addr_list[i][j]);
            strncat(buffer, digtxt, (buflen-1)-strlen(buffer));
        }
        buffer[strlen(buffer)-1] = '\0';
        if (ph->h_addr_list[i+1] != 0)
            strncat(buffer, ", ", (buflen-1)-strlen(buffer));
    }
}

class addClntThrdM : public omni_thread
{
    public:
        addClntThrdM( vncServer *server , unsigned long connip );
    private:
        vncServer *m_server;
        unsigned long m_connip;

        void run(void *);
};

addClntThrdM::addClntThrdM( vncServer *server , unsigned long connip )
{
    m_server = server;
    m_connip = connip;
}

void addClntThrdM::run( void * )
{
    // Get the IP address stringified
    struct in_addr address;
    address.s_addr = m_connip;
    char *name = inet_ntoa(address);
    if (name != 0)
    {
        char *nameDup = strdup(name);
        if (nameDup != NULL)
        {
            // Attempt to create a new socket
            VSocket *tmpsock;
            tmpsock = new VSocket;
            if (tmpsock)
            {
                // Connect out to the specified host on the VNCviewer listen port
                tmpsock->Create();
                if (tmpsock->Connect(nameDup, INCOMING_PORT_OFFSET))
                {
                    // Add the new client to this server
                    m_server->AddClient(tmpsock, TRUE, TRUE);
                }
                else  delete tmpsock;
            }
            // Free the duplicate name
            free(nameDup);
        }
    }
}

static char font_warpsans[] = "9.WarpSans";

static HPOINTER handPtr = NULLHANDLE;

static MRESULT EXPENTRY AboutDlgProc( HWND hwnd , ULONG msg , MPARAM mp1 , MPARAM mp2 )
{
    switch ( msg )
    {
        case WM_INITDLG:
            {
                // if OS/2 ver 4 or higher set font to WarpSans
                struct os2version { ULONG major; ULONG minor; };
                os2version v;
                DosQuerySysInfo( QSV_VERSION_MAJOR , QSV_VERSION_MINOR , &v , sizeof( v ) );
                if ( ( v.major > 20 ) || ( ( v.major == 20 ) && ( v.minor >= 40 ) ) )
                {
                    WinSetPresParam( hwnd , PP_FONTNAMESIZE , sizeof( font_warpsans ) , (PVOID)font_warpsans );
                }
            
                setLinkPointer( handPtr );
                toLink( WinWindowFromID( hwnd , 101 ) );
                toLink( WinWindowFromID( hwnd , 102 ) );
                toLink( WinWindowFromID( hwnd , 103 ) );
            }
            return (MRESULT)FALSE;
        default:
            return WinDefDlgProc( hwnd , msg , mp1 , mp2 );
    }
    return (MRESULT)FALSE;
}

// Process window messages
MRESULT EXPENTRY vncMenu::WndProc(HWND hwnd, ULONG iMsg, MPARAM mp1, MPARAM mp2)
{
    // This is a static method, so we don't know which instantiation we're
    // dealing with. We use Allen Hadden's (ahadden@taratec.com) suggestion
    // from a newsgroup to get the pseudo-this.
    vncMenu *_this = (vncMenu *)WinQueryWindowULong(hwnd, QWL_USER);

    switch (iMsg)
    {
        case WM_DDE_INITIATEACK:
            AnswerTrayApiDdeAck( mp1 );
            AddTrayIcon( hwnd , NULLHANDLE );
            return (MRESULT)TRUE;

        case ( WM_BUTTON2CLICK | 0x2000 ):
            _this->showMenu();
            return (MRESULT)TRUE;

        case ( WM_BUTTON1DBLCLK | 0x2000 ):
            WinPostMsg( hwnd , WM_COMMAND , MPFROMSHORT( CM_PROPERTIES ) , MPVOID );
            return (MRESULT)TRUE;

        // Every five seconds, a timer message causes the icon to update
        case WM_TIMER:
            // Update the icon
            _this->FlashTrayIcon(_this->m_server->AuthClientCount() != 0);
            break;

        // DEAL WITH NOTIFICATIONS FROM THE SERVER:
        case WM_SRV_CLIENT_AUTHENTICATED:
        case WM_SRV_CLIENT_DISCONNECT:
            // Adjust the icon accordingly
            _this->FlashTrayIcon(_this->m_server->AuthClientCount() != 0);
            return (MRESULT)FALSE;

        case WM_COMMAND:
            switch ( SHORT1FROMMP( mp1 ) )
            {
                case CM_PROPERTIES:
                    _this->m_properties.Show( TRUE );
                    break;
                case CM_ABOUT:
                    handPtr = WinLoadPointer( HWND_DESKTOP , NULLHANDLE , IDP_HAND );
                    WinDlgBox( HWND_DESKTOP , HWND_DESKTOP , AboutDlgProc , NULLHANDLE , IDD_ABOUT , NULL );
                    WinDestroyPointer( handPtr );
                    break;
                case CM_CLOSE:
                    WinPostMsg( WinQueryWindow( hwnd , QW_PARENT ) , WM_CLOSE , NULL , NULL );
                    break;
                case CM_OUTGOING_CONN:
                case CM_OUTGOING_CONNIP:
                    {
                        // Add Client message.  This message includes an IP address
                        // of a listening client, to which we should connect.

                        // If there is no IP address then show the connection dialog
                        if ( SHORT1FROMMP( mp1 ) == CM_OUTGOING_CONN )
                        {
                            vncConnDialog *newconn = new vncConnDialog(_this->m_server);
                            if (newconn) newconn->DoDialog();
                            return (MRESULT)FALSE;
                        }
                        addClntThrdM *t = new addClntThrdM( _this->m_server , (unsigned long)mp2 );
                        t->start();
                    }
                    break;
                case CM_KILLCLIENTS:
                    // Kill all connected clients
                    _this->m_server->KillAuthClients();
                    break;
            }
            return (MRESULT)FALSE;
    }

    // Message not recognised
    return WinDefWindowProc(hwnd, iMsg, mp1, mp2);
}



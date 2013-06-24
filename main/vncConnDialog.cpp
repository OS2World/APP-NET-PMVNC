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


// vncConnDialog.cpp: implementation of the vncConnDialog class, used
// to forge outgoing connections to VNC-viewer

#include "stdhdrs.h"
#include "vncConnDialog.h"
#include "PMVNC.h"

#include "res.h"

// Constructor
vncConnDialog::vncConnDialog(vncServer *server)
{
    m_server = server;
}

// Destructor
vncConnDialog::~vncConnDialog()
{
}

// Routine called to activate the dialog and, once it's done, delete it

void vncConnDialog::DoDialog()
{
    WinDlgBox(HWND_DESKTOP, HWND_DESKTOP, vncConnDlgProc,
              NULLHANDLE, IDD_OUTGOING_CONN, this);
    delete this;
}

class addClntThrdCD : public omni_thread
{
    public:
        addClntThrdCD( vncServer *server , const char *host , VCard port );
        ~addClntThrdCD() { free( m_host ); };
    private:
        vncServer *m_server;
        char *m_host;
        VCard m_port;

        void run(void *);
};

addClntThrdCD::addClntThrdCD( vncServer *server , const char *host , VCard port )
{
    m_server = server;
    m_host = strdup( host );
    m_port = port;
}

void addClntThrdCD::run( void * )
{
    HAB thab = WinInitialize( 0 );
    HMQ hmq = WinCreateMsgQueue( thab , 0 );

    // Attempt to create a new socket
    VSocket *tmpsock;
    tmpsock = new VSocket;
    if (tmpsock!=NULL)
    {
        // Connect out to the specified host on the VNCviewer listen port
        // To be really good, we should allow a display number here but
        // for now we'll just assume we're connecting to display zero
        tmpsock->Create();
        if (tmpsock->Connect(m_host, m_port))
        {
            // Add the new client to this server
            m_server->AddClient(tmpsock, TRUE, TRUE);
        }
        else
        {
            // Print up an error message
            WinMessageBox( HWND_DESKTOP , HWND_DESKTOP ,
                (PSZ)"Failed to connect to listening VNC viewer",
                (PSZ)"Outgoing Connection", 10001 , MB_ICONEXCLAMATION | MB_OK );
            delete tmpsock;
        }
    }

    WinDestroyMsgQueue( hmq );
    WinTerminate( thab );
}

// Callback function - handles messages sent to the dialog box
MRESULT EXPENTRY vncConnDialog::vncConnDlgProc( HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    // This is a static method, so we don't know which instantiation we're
    // dealing with. But we can get a pseudo-this from the parameter to
    // WM_INITDIALOG, which we therafter store with the window and retrieve
    // as follows:
    vncConnDialog *_this = (vncConnDialog *)WinQueryWindowULong(hwnd, QWL_USER);

    switch (msg) {

        // Dialog has just been created
    case WM_INITDLG:
        {
            // Save the lParam into our user data so that subsequent calls have
            // access to the parent C++ object
            WinSetWindowULong(hwnd, QWL_USER, (ULONG)mp2);
            vncConnDialog *_this = (vncConnDialog *)mp2;

            // Make the text entry box active
            WinSetFocus(HWND_DESKTOP, WinWindowFromID(hwnd, IDC_HOSTNAME));

            WinSendDlgItemMsg( hwnd, IDC_HOSTNAME, EM_SETTEXTLIMIT, MPFROMSHORT( 256 ) , MPVOID );
            // Return success!
            return (MRESULT)FALSE;
        }

        // Dialog has just received a command
    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1))
            {

            // User clicked OK or pressed return
            case DID_OK:
            {
                char viewer[256];
                char hostname[256];
                VCard display_or_port;

                // Get the viewer to connect to
                WinQueryDlgItemText(hwnd, IDC_HOSTNAME, 256, (PSZ)viewer);

                // Process the supplied viewer address
                int result = sscanf(viewer, "%255[^:]:%u", hostname, &display_or_port);
                if (result == 1) {
                    display_or_port = 0;
                    result = 2;
                }
                if (result == 2) {
                    // Correct a display number to a port number if required
                    if (display_or_port < 100) {
                        display_or_port += INCOMING_PORT_OFFSET;
                    }

                    addClntThrdCD *t = new addClntThrdCD( _this->m_server , hostname , display_or_port );
                    t->start();
                    WinDismissDlg( hwnd , 0 );
                } else {
                    // We couldn't process the machine specification
                    WinMessageBox( HWND_DESKTOP , HWND_DESKTOP ,
                        (PSZ)"Unable to process specified hostname and display/port",
                        (PSZ)"Outgoing Connection", 10001, MB_OK | MB_ICONEXCLAMATION);
                }
            }
            return (MRESULT)FALSE;

            // Cancel the dialog
            case DID_CANCEL:
                WinDismissDlg( hwnd , 0 );
                return (MRESULT)FALSE;
        };
        break;
    }
    return WinDefDlgProc( hwnd , msg , mp1 , mp2 );
}


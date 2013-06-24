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


// vncAcceptDialog.cpp: implementation of the vncAcceptDialog class, used
// to query whether or not to accept incoming connections.

#include "stdhdrs.h"
#include "vncAcceptDialog.h"
#include "PMVNC.h"

#include "res.h"

#include <string.h>

extern HAB hab;

// Constructor

vncAcceptDialog::vncAcceptDialog(UINT timeoutSecs, const char *ipAddress)
{
    m_timeoutSecs = timeoutSecs;
    m_ipAddress = strdup(ipAddress);
}

// Destructor

vncAcceptDialog::~vncAcceptDialog()
{
    if (m_ipAddress)
        free(m_ipAddress);
}

// Routine called to activate the dialog and, once it's done, delete it

BOOL vncAcceptDialog::DoDialog()
{
    int retVal = WinDlgBox(HWND_DESKTOP, HWND_DESKTOP, vncAcceptDlgProc,
                           NULLHANDLE, IDD_ACCEPT_CONN, this);
    delete this;
    return retVal == IDC_ACCEPT;
}

// Callback function - handles messages sent to the dialog box
MRESULT EXPENTRY vncAcceptDialog::vncAcceptDlgProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    // This is a static method, so we don't know which instantiation we're
    // dealing with. But we can get a pseudo-this from the parameter to
    // WM_INITDIALOG, which we therafter store with the window and retrieve
    // as follows:
    vncAcceptDialog *_this = (vncAcceptDialog *)WinQueryWindowULong(hwnd, QWL_USER);

    switch (msg) {

        // Dialog has just been created
    case WM_INITDLG:
        {
            // Save the lParam into our user data so that subsequent calls have
            // access to the parent C++ object
            WinSetWindowULong(hwnd, QWL_USER, (ULONG)mp2);
            vncAcceptDialog *_this = (vncAcceptDialog *)mp2;

            // Set the IP-address string
            WinSetDlgItemText(hwnd, IDC_ACCEPT_IP, (PSZ)_this->m_ipAddress);
            WinStartTimer( hab, hwnd, 1 , 1000 );
            _this->m_timeoutCount = _this->m_timeoutSecs;

            WinFlashWindow(hwnd, TRUE);
            return (MRESULT)FALSE;
        }

        // Timer event
    case WM_TIMER:
        if ( SHORT1FROMMP(mp1) == 1 )
        {
            if ((_this->m_timeoutCount) == 0)
                WinDismissDlg(hwnd, IDC_REJECT);
            _this->m_timeoutCount--;

            // Update the displayed count
            char temp[256];
            sprintf(temp, "AutoReject:%u", (_this->m_timeoutCount));
            WinSetDlgItemText(hwnd, IDC_ACCEPT_TIMEOUT, (PSZ)temp);
        }
        break;
        // Dialog has just received a command
    case WM_COMMAND:
        switch (SHORT1FROMMP(mp1))
        {
            // User clicked Accept or pressed return
        case IDC_ACCEPT:
        case DID_OK:
            WinDismissDlg(hwnd, IDC_ACCEPT);
            return (MRESULT)FALSE;

        case IDC_REJECT:
        case DID_CANCEL:
            WinDismissDlg(hwnd, IDC_REJECT);
            return (MRESULT)FALSE;
        };
        break;
    }
    return WinDefDlgProc( hwnd , msg , mp1 , mp2 );
}


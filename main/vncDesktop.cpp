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

// vncDesktop implementation

// System headers
#include <assert.h>
#include "stdhdrs.h"

#include <mmioos2.h>
#include <dive.h>
#include <fourcc.h>

// Custom headers
#include <omnithread.h>
#include "PMVNC.h"
#include "vncServer.h"
#include "vncKeymap.h"
#include "rfbRegion.h"
#include "rfbRect.h"
#include "vncDesktop.h"

// Constants
const char szDesktopSink[]     = "PMVNC.desktop.sink";

void ActivateHooks( HMQ thmq );
void DeactivateHooks();

// The desktop handler thread
// This handles the messages posted by RFBLib to the vncDesktop window

class vncDesktopThread : public omni_thread
{
public:
    vncDesktopThread() {m_returnsig = NULL;};
protected:
    ~vncDesktopThread() {if (m_returnsig != NULL) delete m_returnsig;};
public:
    virtual BOOL Init(vncDesktop *desktop, vncServer *server);
    virtual void *run_undetached(void *arg);
    virtual void ReturnVal(BOOL result);
    void PollWindow(rfb::Region2D &rgn, HWND hwnd);
protected:
    vncServer *m_server;
    vncDesktop *m_desktop;

    omni_mutex m_returnLock;
    omni_condition *m_returnsig;
    BOOL m_return;
    BOOL m_returnset;
};

BOOL vncDesktopThread::Init(vncDesktop *desktop, vncServer *server)
{
    // Save the server pointer
    m_server = server;
    m_desktop = desktop;

    m_returnset = FALSE;
    m_returnsig = new omni_condition(&m_returnLock);

    // Start the thread
    start_undetached();

    // Wait for the thread to let us know if it failed to init
    {   omni_mutex_lock l(m_returnLock);

        while (!m_returnset)
        {
            m_returnsig->wait();
        }
    }

    return m_return;
}

void vncDesktopThread::ReturnVal(BOOL result)
{
    omni_mutex_lock l(m_returnLock);

    m_returnset = TRUE;
    m_return = result;
    m_returnsig->signal();
}

inline BOOL ErGetWindowRect( HWND hwnd , PRECTL r )
{
    if ( !WinQueryWindowRect(hwnd, r) ) return FALSE;
    if ( !WinMapWindowPoints(hwnd, HWND_DESKTOP, (PPOINTL)r, 2) ) return FALSE;
    return TRUE;
}

static BOOL isConsole( HWND hwnd )
{
    BOOL rval = FALSE;
    char pchBuffer[ 10 ];
    HWND cl;

    if ( WinQueryClassName( hwnd , sizeof( pchBuffer ) , (PSZ)pchBuffer ) == 2 )
    {
        if ( ( pchBuffer[ 0 ] == '#' ) && ( pchBuffer[ 1 ] == '1' ) )
        {
            if ( ( cl = WinWindowFromID( hwnd , FID_CLIENT ) ) != NULLHANDLE )
            {
                if ( WinQueryClassName( cl , sizeof( pchBuffer ) , (PSZ)pchBuffer ) != 0 )
                {
                    if ( ( strcmp( pchBuffer , "Shield" ) == 0 ) ||    // Console window
                         ( strncmp( pchBuffer , "OIRC" , 4 ) == 0 ) )  // OpenChat window
                    {
                        rval = TRUE;
                    }
                }
            }
        }
    }
    return rval;
}

void vncDesktopThread::PollWindow(rfb::Region2D &rgn, HWND hwnd)
{
    // Are we set to low-load polling?
    if (m_server->PollOnEventOnly())
    {
        // Yes, so only poll if the remote user has done something
        if (!m_server->RemoteEventReceived()) {
            return;
        }
    }

    // Does the client want us to poll only console windows?
    if (m_desktop->m_server->PollConsoleOnly())
    {
        if ( !isConsole( hwnd ) )  return;
    }

    RECTL rect;

    // Get the rectangle
    if (ErGetWindowRect(hwnd, &rect))
    {
        rfb::Rect wrect = rfb::Rect(rect).intersect(m_desktop->m_bmrect);
        if (!wrect.is_empty())
        {
            rgn = rgn.union_(rfb::Region2D(wrect));
        }
    }
}

void *vncDesktopThread::run_undetached(void *arg)
{
    // Attempt to initialise and return success or failure
    if (!m_desktop->Startup())
    {
        ReturnVal(FALSE);
        return NULL;
    }

  // Grab the initial display contents
  // *** m_desktop->m_buffer.GrabRegion(m_desktop->m_bmrect);
  // *** m_desktop->m_buffer.Clear(m_desktop->m_bmrect);

    // Succeeded to initialise ok
    ReturnVal(TRUE);

    // START PROCESSING DESKTOP MESSAGES

    // We set a flag inside the desktop handler here, to indicate it's now safe
    // to handle clipboard messages
    m_desktop->SetClipboardActive(TRUE);

    // All changes in the state of the display are stored in a local
    // UpdateTracker object, and are flushed to the vncServer whenever
    // client updates are about to be triggered
    rfb::SimpleUpdateTracker clipped_updates;
    rfb::ClippedUpdateTracker updates(clipped_updates, m_desktop->m_bmrect);
    clipped_updates.enable_copyrect(true);

    // Incoming update messages are collated into a single region cache
    // The region cache areas are checked for changes before an update
    // is triggered, and the changed areas are passed to the UpdateTracker
    rfb::Region2D rgncache = m_desktop->m_bmrect;

    // The previous cursor position is stored, to allow us to erase the
    // old instance whenever it moves.
    rfb::Point oldcursorpos;

    // Set the hook thread to a high priority
    // *** set_priority(omni_thread::PRIORITY_HIGH);

    BOOL idle_skip = TRUE;
    ULONG idle_skip_count = 0;
    QMSG msg;
    while (TRUE)
    {
        if (!WinPeekMsg(m_desktop->hab, &msg, NULLHANDLE, 0, 0, PM_REMOVE)) {
            //
            // - MESSAGE QUEUE EMPTY
            // Whenever the message queue becomes empty, we check to see whether
            // there are updates to be passed to clients.
            if (idle_skip) {
                idle_skip = FALSE;
                if (idle_skip_count++ < 4) {
                    DosSleep(1);
                    continue;
                }
            }
            idle_skip_count = 0;

            // Clear the triggered flag
            m_desktop->m_update_triggered = FALSE;

            //
            // CHECK SCREEN FORMAT
            // First, we must check that the screen hasnt changed too much.
            if (m_desktop->m_displaychanged)
            {
                rfbServerInitMsg oldscrinfo = m_desktop->m_scrinfo;
                m_desktop->m_displaychanged = FALSE;

                // Attempt to close the old hooks
                if (!m_desktop->Shutdown())
                {
                    m_server->KillAuthClients();
                    break;
                }

                // Now attempt to re-install them!
                if (!m_desktop->Startup())
                {
                    m_server->KillAuthClients();
                    break;
                }

                // Check that the screen info hasn't changed
                vnclog.Print(LL_INTINFO, VNCLOG("SCR: old screen format %dx%dx%d\n"),
                    oldscrinfo.framebufferWidth,
                    oldscrinfo.framebufferHeight,
                    oldscrinfo.format.bitsPerPixel);
                vnclog.Print(LL_INTINFO, VNCLOG("SCR: new screen format %dx%dx%d\n"),
                    m_desktop->m_scrinfo.framebufferWidth,
                    m_desktop->m_scrinfo.framebufferHeight,
                    m_desktop->m_scrinfo.format.bitsPerPixel);
                if ((m_desktop->m_scrinfo.framebufferWidth != oldscrinfo.framebufferWidth) ||
                    (m_desktop->m_scrinfo.framebufferHeight != oldscrinfo.framebufferHeight))
                {
                    m_server->KillAuthClients();
                    break;
                } else if (memcmp(&m_desktop->m_scrinfo.format, &oldscrinfo.format, sizeof(rfbPixelFormat)) != 0)
                {
                    m_server->UpdateLocalFormat();
                }

                // Adjust the UpdateTracker clip region
                updates.set_clip_region(m_desktop->m_bmrect);

                // Add a full screen update to all the clients
                rgncache = rgncache.union_(rfb::Region2D(m_desktop->m_bmrect));
                m_server->UpdatePalette();
            }

            //
            // CALCULATE CHANGES
            //

            if (m_desktop->m_server->UpdateWanted())
            {
                // POLL PROBLEM AREAS
                // We add specific areas of the screen to the region cache,
                // causing them to be fetched for processing.
                if (m_desktop->m_server->PollFullScreen())
                {
                    rfb::Rect rect = m_desktop->m_qtrscreen;
                    rect = rect.translate(rfb::Point(0, m_desktop->m_pollingcycle * m_desktop->m_qtrscreen.br.y));
                    rgncache = rgncache.union_(rfb::Region2D(rect));
                    m_desktop->m_pollingcycle = (m_desktop->m_pollingcycle + 1) % 4;
                }
                if (m_desktop->m_server->PollForeground())
                {
                    // Get the window rectangle for the currently selected window
                    HWND hwnd = WinQueryActiveWindow( HWND_DESKTOP );
                    if (hwnd != NULLHANDLE)
                        PollWindow(rgncache, hwnd);
                }
                if (m_desktop->m_server->PollUnderCursor())
                {
                    // Find the mouse position
                    POINTL mousepos;
                    if (WinQueryPointerPos(HWND_DESKTOP, &mousepos))
                    {
                        // Find the window under the mouse
                        HWND hwnd = WinWindowFromPoint(HWND_DESKTOP, &mousepos, FALSE);
                        if (hwnd != NULL)
                            PollWindow(rgncache, hwnd);
                    }
                }

                // PROCESS THE MOUSE POINTER
                // Some of the hard work is done in clients, some here
                // This code fetches the desktop under the old pointer position
                // but the client is responsible for actually encoding and sending
                // it when required.
                // This code also renders the pointer and saves the rendered position
                // Clients include this when rendering updates.
                // The code is complicated in this way because we wish to avoid
                // rendering parts of the screen the mouse moved through between
                // client updates, since in practice they will probably not have changed.

                // Re-render the mouse's old location if it's moved
                BOOL cursormoved = FALSE;
                POINTL cursorpos;
                if (WinQueryPointerPos(HWND_DESKTOP,&cursorpos) &&
                    ((cursorpos.x != oldcursorpos.x) ||
                    ((yscr-cursorpos.y) != oldcursorpos.y))) {
                    cursormoved = TRUE;
                    oldcursorpos = rfb::Point(cursorpos);
                }
                if (cursormoved) {
                    if (!m_desktop->m_cursorpos.is_empty())
                        rgncache = rgncache.union_(rfb::Region2D(m_desktop->m_cursorpos));
                }

                {
                    // Prevent any clients from accessing the Buffer
                    omni_mutex_lock l(m_desktop->m_update_lock);

                    // CHECK FOR COPYRECTS
                    // This actually just checks where the Foreground window is
                    m_desktop->CalcCopyRects(updates);

                    // GRAB THE DISPLAY
                    // Fetch data from the display to our display cache.
                    m_desktop->m_buffer.GrabRegion(rgncache);
                  // Render the mouse
                  m_desktop->m_buffer.GrabMouse();
                    if (cursormoved) {
                        // Inform clients that it has moved
                        m_desktop->m_server->UpdateMouse();
                        // Get the buffer to fetch the pointer bitmap
                        if (!m_desktop->m_cursorpos.is_empty())
                            rgncache = rgncache.union_(rfb::Region2D(m_desktop->m_cursorpos));
                    }

                    // SCAN THE CHANGED REGION FOR ACTUAL CHANGES
                    // The hooks return hints as to areas that may have changed.
                    // We check the suggested areas, and just send the ones that
                    // have actually changed.
                    // Note that we deliberately don't check the copyrect destination
                    // here, to reduce the overhead & the likelihood of corrupting the
                    // backbuffer contents.
                    rfb::Region2D checkrgn = rgncache.subtract(clipped_updates.get_copied_region());
                    rgncache = clipped_updates.get_copied_region();
                    rfb::Region2D changedrgn;
                    m_desktop->m_buffer.CheckRegion(changedrgn, checkrgn);

                    // FLUSH UPDATES TO CLIENTS
                    // Add the bits that have really changed to their update regions
                    // Note that the cursor is NOT included - they must do that
                    // themselves, for the reasons above.
                    // This call implicitly kicks clients to update themselves

                    updates.add_changed(changedrgn);
                    clipped_updates.get_update(m_server->GetUpdateTracker());
                }

                // Clear the update tracker and region cache
                clipped_updates.clear();
            }

            // Now wait for more messages to be queued
            if (!WinWaitMsg(m_desktop->hab,0,0)) {
                vnclog.Print(LL_INTERR, VNCLOG("WinWaitMsg() failed\n"));
                break;
            }
        } else if (msg.msg == RFB_SCREEN_UPDATE) {
            // Process an incoming update event

            // An area of the screen has changed
            rfb::Rect rect;
            rect.tl = rfb::Point(SHORT1FROMMP(msg.mp1), SHORT2FROMMP(msg.mp1));
            rect.br = rfb::Point(SHORT1FROMMP(msg.mp2), SHORT2FROMMP(msg.mp2));
            rect = rect.intersect(m_desktop->m_bmrect);
            if (!rect.is_empty()) {
                rgncache = rgncache.union_(rfb::Region2D(rect));
            }

            idle_skip = TRUE;
        } else if (msg.msg == RFB_MOUSE_UPDATE) {
            // Process an incoming mouse event
            // Save the cursor ID
            m_desktop->SetCursor((HPOINTER)msg.mp1);

            idle_skip = TRUE;
        } else if (msg.msg == WM_QUIT) {
            break;
        } else {
            // Process any other messages normally
            WinDispatchMsg(m_desktop->hab, &msg);

            idle_skip = TRUE;
        }
    }

    m_desktop->SetClipboardActive(FALSE);

    vnclog.Print(LL_INTINFO, VNCLOG("quitting desktop server thread\n"));

    // Clear all the hooks and close windows, etc.
    m_desktop->Shutdown();

    // Clear the shift modifier keys, now that there are no remote clients
    vncKeymap::ClearShiftKeys();

    return NULL;
}

// Implementation

vncDesktop::vncDesktop()
{
    m_thread = NULL;

    m_hwnd = NULLHANDLE;
    m_timerid = 0;
    m_hcursor = NULLHANDLE;

    m_displaychanged = FALSE;
    m_update_triggered = FALSE;

    hab = NULLHANDLE;
    hmq = NULLHANDLE;
    m_hmemdc = NULLHANDLE;
    m_hmemps = NULLHANDLE;
    m_hdive  = NULLHANDLE;
    m_membitmap = NULLHANDLE;

    m_initialClipBoardSeen = FALSE;

    m_foreground_window = NULLHANDLE;

    m_clipboard_active = FALSE;

    m_pollingcycle = 0;
}

vncDesktop::~vncDesktop()
{
    vnclog.Print(LL_INTINFO, VNCLOG("killing screen server\n"));

    // If we created a thread then here we delete it
    // The thread itself does most of the cleanup
    if(m_thread != NULL)
    {
        // Post a close message to quit our message handler thread
        WinPostMsg(Window(), WM_QUIT, MPVOID, MPVOID);

        // Join with the desktop handler thread
        void *returnval;
        try
        {
            m_thread->join(&returnval);
        }
        catch ( ... ) { /* just do nothing */ }
        m_thread = NULL;
    }

    // Let's call Shutdown just in case something went wrong...
    Shutdown();
}

// Tell the desktop hooks to grab & update a particular rectangle
void vncDesktop::QueueRect(const rfb::Rect &rect)
{
    MPARAM vwParam = MPFROM2SHORT(rect.tl.x, rect.tl.y);
    MPARAM vlParam = MPFROM2SHORT(rect.br.x, rect.br.y);

    WinPostMsg(Window(), RFB_SCREEN_UPDATE, vwParam, vlParam);
}

// Kick the desktop hooks to perform an update
void vncDesktop::TriggerUpdate()
{
    // Note that we should really lock the update lock here,
    // but there are periodic timer updates anyway, so
    // we don't actually need to.  Something to think about.
    if (!m_update_triggered) {
        m_update_triggered = TRUE;
        WinPostMsg(Window(), WM_TIMER, 0, MPVOID);
    }
}

// Routine to startup and install all the hooks and stuff
BOOL vncDesktop::Startup()
{
    hab = WinInitialize( 0 );
    hmq = WinCreateMsgQueue( hab , 0 );

    // Initialise the Desktop object
    if (!InitDesktop())
        return FALSE;

    if (!InitBitmap())
        return FALSE;

    if (!ThunkBitmapInfo())
        return FALSE;

    if (!SetPixFormat())
        return FALSE;

  if (!SetPixShifts())
        return FALSE;

    if (!SetPalette())
        return FALSE;

    if (!InitWindow())
        return FALSE;

    // Add the system hook
    ActivateHooks( hmq );

    // Start up the keyboard and mouse filters
    // ??? SetKeyboardFilterHook(m_server->LocalInputsDisabled());
    //SetMouseFilterHook(m_server->LocalInputsDisabled());

    // Start a timer to handle Polling Mode.  The timer will cause
    // an "idle" event once every second, which is necessary if Polling
    // Mode is being used, to cause TriggerUpdate to be called.
    m_timerid = WinStartTimer(hab, m_hwnd, 1, 1000 );

    // Initialise the buffer object
    m_buffer.SetDesktop(this);

    // Create the quarter-screen rectangle for polling
    m_qtrscreen = rfb::Rect(0, 0, m_bmrect.br.x, m_bmrect.br.y/4);

    // Everything is ok, so return TRUE
    return TRUE;
}

//static bool useDive = true;

// Routine to shutdown all the hooks and stuff
BOOL vncDesktop::Shutdown()
{
    // If we created a timer then kill it
    if (m_timerid != NULL)
        WinStopTimer(hab, m_hwnd, m_timerid);

    // If we created a window then kill it and the hooks
    if(m_hwnd != NULL)
    {
        // Remove the system hooks
        DeactivateHooks();

        // unset clipboard viewer
        WinSetClipbrdViewer( hab , NULLHANDLE );

        // Close the hook window
        WinDestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }

    // Now free all the bitmap stuff
    if (m_membitmap != NULLHANDLE)
    {
        // Release the custom bitmap, if any
        if (!GpiDeleteBitmap(m_membitmap))
        {
            vnclog.Print(LL_INTERR, VNCLOG("failed to GpiDeleteBitmap\n"));
        }
        m_membitmap = NULLHANDLE;
    }
    if (m_hmemps != NULLHANDLE)
    {
        // Release our device context
        if (!GpiDestroyPS(m_hmemps))
        {
            vnclog.Print(LL_INTERR, VNCLOG("failed to GpiDestroyPS\n"));
        }
        m_hmemps = NULLHANDLE;
    }
    if (m_hmemdc != NULLHANDLE)
    {
        // Release our device context
        if (DevCloseDC(m_hmemdc)==DEV_ERROR)
        {
            vnclog.Print(LL_INTERR, VNCLOG("failed to DevCloseDC\n"));
        }
        m_hmemdc = NULLHANDLE;
    }

    /*if ( useDive )
    {
        if (m_hdive != NULLHANDLE )
        {
            DiveClose( m_hdive );
        }
    } */

    if ( hmq != NULLHANDLE )
    {
        WinDestroyMsgQueue( hmq );
        hmq = NULLHANDLE;
    }
    if ( hab != NULLHANDLE )
    {
        WinTerminate( hab );
        hab = NULLHANDLE;
    }
    return TRUE;
}

// Routine to ensure we're on the correct NT desktop

BOOL vncDesktop::InitDesktop()
{
    return TRUE;
}

//DIVE_CAPS DiveCaps          = { 0 };
//FOURCC    fccFormats[ 100 ] = { 0 };

BOOL vncDesktop::InitBitmap()
{
    // Get the device context for the whole screen and find it's size
    HPS scrps = WinGetScreenPS( HWND_DESKTOP );
    if (scrps == NULLHANDLE) {
        vnclog.Print(LL_INTERR, VNCLOG("failed to WinGetScreenPS\n"));
        return FALSE;
    }
    ULONG ulFlags;
    SIZEL sizl;
    ulFlags = GpiQueryPS( scrps , &sizl );
    HDC m_hrootdc = GpiQueryDevice( scrps );
    WinReleasePS( scrps );

    if (m_hrootdc == NULLHANDLE) {
        vnclog.Print(LL_INTERR, VNCLOG("failed to get display context\n"));
        return FALSE;
    }

    m_bmrect = rfb::Rect(0, 0,
        WinQuerySysValue(HWND_DESKTOP, SV_CXSCREEN),
        WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN));
    vnclog.Print(LL_INTINFO, VNCLOG("bitmap dimensions are %d x %d\n"), m_bmrect.br.x, m_bmrect.br.y);

    // Create a compatible memory DC

    m_hmemdc = DevOpenDC( hab , OD_MEMORY , (PSZ)"*" ,  0L , NULL , NULLHANDLE );
    if (m_hmemdc == DEV_ERROR) {
        vnclog.Print(LL_INTERR, VNCLOG("failed to DevOpenDC(%x)\n"), WinGetLastError(hab));
        return FALSE;
    }

    sizl.cx = sizl.cy = 0;
    m_hmemps = GpiCreatePS( hab , m_hmemdc , &sizl , ulFlags | GPIA_ASSOC );
    if (m_hmemps == NULLHANDLE) {
        vnclog.Print(LL_INTERR, VNCLOG("failed to GpiCreatePS(%x)\n"), WinGetLastError(hab));
        return FALSE;
    }

    LONG cPlanes = 0;
    DevQueryCaps( m_hmemdc , CAPS_COLOR_PLANES , 1L , &cPlanes );
    // If we are using a memory bitmap then check how many planes it uses
    // The VNC code can only handle formats with a single plane (CHUNKY pixels)
    if (cPlanes != 1)
    {
        WinMessageBox( HWND_DESKTOP , HWND_DESKTOP ,
            (PSZ)"vncDesktop : current display is PLANAR, not CHUNKY!\n"
                 "PMVNC cannot be used with this graphics device driver",
            (PSZ)szAppName, 1 , MB_ERROR | MB_OK );
        return FALSE;
    }

    LONG cBitCount = 0, cOriginalBitcount = 0;
    DevQueryCaps( m_hmemdc , CAPS_COLOR_BITCOUNT , 1L , &cBitCount );
    cOriginalBitcount = cBitCount;

    // 15 bit colordepth workaround
    if ( m_server->Colordepth15bit() )  cBitCount = 24;

    vnclog.Print(LL_INTINFO, VNCLOG("system color bitcount %d, using bitcount %d\n"), cOriginalBitcount, cBitCount );

    memset(&m_bminfo, 0, sizeof(m_bminfo));
    m_bminfo.bmi.cbFix     = sizeof( BITMAPINFOHEADER );
    m_bminfo.bmi.cx        = (SHORT)( m_bmrect.br.x );
    m_bminfo.bmi.cy        = (SHORT)( m_bmrect.br.y );
    m_bminfo.bmi.cPlanes   = (SHORT)cPlanes;
    m_bminfo.bmi.cBitCount = (SHORT)cBitCount;

    // Create the bitmap to be compatible with the ROOT DC!!!
    m_membitmap = GpiCreateBitmap( m_hmemps , (PBITMAPINFOHEADER2)&m_bminfo.bmi , 0x0000 , NULL , NULL );

    if (m_membitmap == NULLHANDLE) {
        vnclog.Print(LL_INTERR, VNCLOG("failed to create memory bitmap(%x)\n"), WinGetLastError(hab));
        return FALSE;
    }
    vnclog.Print(LL_INTINFO, VNCLOG("created memory bitmap\n"));

    // Is the bitmap palette-based or truecolour?
    m_bminfo.truecolour = ( cBitCount > 8 );

    /*if ( useDive )
    {
        DiveCaps.pFormatData = fccFormats;
        DiveCaps.ulFormatLength = 120;
        DiveCaps.ulStructLen = sizeof( DIVE_CAPS );

        if ( DiveQueryCaps( &DiveCaps , DIVE_BUFFER_SCREEN ) )
        {
            vnclog.Print(LL_INTERR, VNCLOG("DiveQueryCaps error\n"));
            return FALSE;
        }

        if ( DiveCaps.ulDepth < 8 )
        {
            vnclog.Print(LL_INTERR, VNCLOG("DiveCaps.ulDepth < 8\n"));
            return FALSE;
        }

        // Get an instance of DIVE APIs.
        if ( DiveOpen( &m_hdive , FALSE , 0 ) )
        {
            vnclog.Print(LL_INTERR, VNCLOG("DiveOpen error\n"));
            return FALSE;
        }

        vnclog.Print(LL_INTINFO, VNCLOG("DIVE inited\n"));
    } */

    return TRUE;
}

BOOL vncDesktop::ThunkBitmapInfo()
{
    // Attempt to force the actual format into one we can handle
    // We can handle 8-bit-palette and 16/32-bit-truecolour modes
    switch (m_bminfo.bmi.cBitCount)
    {
    case 1:
    case 4:
    case 8:
        vnclog.Print(LL_INTINFO, VNCLOG("DBG:bits/planes = %d/%d\n"),
            (int)m_bminfo.bmi.cBitCount,
            (int)m_bminfo.bmi.cPlanes);

        // Correct the BITMAPINFO header to the format we actually want
        m_bminfo.bmi.cPlanes = 1;
        m_bminfo.bmi.cBitCount = 8;
        m_bminfo.truecolour = FALSE;
        break;

    case 24:
        // Update the bitmapinfo header
        m_bminfo.bmi.cBitCount = 32;
        m_bminfo.bmi.cPlanes = 1;
        break;
    }

    return TRUE;
}

BOOL vncDesktop::SetPixFormat()
{
    // Examine the bitmapinfo structure to obtain the current pixel format
    m_scrinfo.format.trueColour = m_bminfo.truecolour;
    m_scrinfo.format.bigEndian = 0;

    // Set up the native buffer width, height and format
    m_scrinfo.framebufferWidth = (CARD16) (m_bmrect.br.x - m_bmrect.tl.x);  // Swap endian before actually sending
    m_scrinfo.framebufferHeight = (CARD16) (m_bmrect.br.y - m_bmrect.tl.y); // Swap endian before actually sending
    m_scrinfo.format.bitsPerPixel = (CARD8) m_bminfo.bmi.cBitCount;
    m_scrinfo.format.depth        = (CARD8) m_bminfo.bmi.cBitCount;

    // Calculate the number of bytes per row
    m_bytesPerRow = m_scrinfo.framebufferWidth * m_scrinfo.format.bitsPerPixel / 8;

    return TRUE;
}

BOOL vncDesktop::SetPixShifts()
{
    // Sort out the colour shifts, etc.
    DWORD redMask=0, blueMask=0, greenMask = 0;

    switch (m_bminfo.bmi.cBitCount)
    {
        /*case 15:
            redMask = 0x001f; greenMask = 0x03e0; blueMask = 0x7c00;
            break;*/
        case 16:
            redMask = 0xF800; greenMask = 0x07e0; blueMask = 0x001f;
            break;
        case 24:
        case 32:
            redMask = 0xff0000; greenMask = 0xff00; blueMask = 0x00ff;
            break;

        default:
            // Other pixel formats are only valid if they're palette-based
            if (m_bminfo.truecolour)
            {
                vnclog.Print(LL_INTERR, "unsupported truecolour pixel format for setpixshifts\n");
                return FALSE;
            }
            return TRUE;
    }

    // Convert the data we just retrieved
    MaskToMaxAndShift(redMask, m_scrinfo.format.redMax, m_scrinfo.format.redShift);
    MaskToMaxAndShift(greenMask, m_scrinfo.format.greenMax, m_scrinfo.format.greenShift);
    MaskToMaxAndShift(blueMask, m_scrinfo.format.blueMax, m_scrinfo.format.blueShift);

    return TRUE;
}

BOOL vncDesktop::SetPalette()
{
    // Lock the current display palette into the memory DC we're holding
    if (!m_bminfo.truecolour)
    {
        HPS scrps = WinGetScreenPS( HWND_DESKTOP );
        UINT entries = 0;
        RGB2 palette[ 256 ];
        memset( palette , 0 , sizeof( palette ) );
        entries = GpiQueryRealColors(scrps, 0, 0, 256, (PLONG)palette);
        WinReleasePS( scrps );
        if ( entries == GPI_ALTERROR )
        {
            vnclog.Print(LL_INTERR, VNCLOG("unable to get system palette entries\n"));
            return FALSE;
        }

        vnclog.Print(LL_INTINFO, VNCLOG("got %u palette entries\n"), entries);

        HPAL pal = GpiCreatePalette( hab, 0, LCOLF_CONSECRGB, entries, (PULONG)palette);
        if (pal == NULLHANDLE)
        {
            vnclog.Print(LL_INTERR, VNCLOG("unable to create palette\n"));
            return FALSE;
        }
        HPAL hpalOld = GpiSelectPalette(m_hmemps, pal);
        if (hpalOld == PAL_ERROR)
        {
            vnclog.Print(LL_INTERR, VNCLOG("unable to select() palette\n"));
            GpiDeletePalette(pal);
            return FALSE;
        }
        if (hpalOld != NULLHANDLE)  GpiDeletePalette(hpalOld);
        vnclog.Print(LL_INTINFO, VNCLOG("initialised palette OK\n"));
        return TRUE;
    }

    // Not a palette based local screen - forget it!
    vnclog.Print(LL_INTWARN, VNCLOG("no palette data for truecolour display\n"));
    return TRUE;
}

MRESULT EXPENTRY DesktopWndProc(HWND hwnd, ULONG iMsg, MPARAM wParam, MPARAM lParam);

BOOL m_wndClass = FALSE;

BOOL vncDesktop::InitWindow()
{
    if (!m_wndClass) {
        // Create the window class
        m_wndClass = WinRegisterClass( hab , (PSZ)szDesktopSink, DesktopWndProc, 0, sizeof( PVOID ) );
        if (!m_wndClass) {
            vnclog.Print(LL_INTERR, VNCLOG("failed to register window class\n"));
            return FALSE;
        }
    }

    // And create a window
    m_hwnd = WinCreateWindow( HWND_DESKTOP, (PSZ)szDesktopSink, (PSZ)szAppName, 0, 0, 0, 0, 0,
                              NULLHANDLE, HWND_TOP, 0, NULL, NULL );
    if (m_hwnd == NULL) {
        vnclog.Print(LL_INTERR, VNCLOG("failed to create hook window\n"));
        return FALSE;
    }

    // Set the "this" pointer for the window
    WinSetWindowULong(m_hwnd, QWL_USER, (ULONG)this);

    // Enable clipboard hooking
    WinOpenClipbrd( hab );
    WinSetClipbrdViewer( hab, m_hwnd );
    WinCloseClipbrd( hab );

    return TRUE;
}

BOOL vncDesktop::Init(vncServer *server)
{
    vnclog.Print(LL_INTINFO, VNCLOG("initialising desktop handler\n"));

    // Save the server pointer
    m_server = server;

    // Load in the arrow cursor
    m_hdefcursor = WinQuerySysPointer(HWND_DESKTOP,SPTR_ARROW, FALSE);
    m_hcursor = m_hdefcursor;

    // Spawn a thread to handle that window's message queue
    vncDesktopThread *thread = new vncDesktopThread;
    if (thread == NULL) {
        vnclog.Print(LL_INTERR, VNCLOG("failed to start hook thread\n"));
        return FALSE;
    }
    m_thread = thread;
    return thread->Init(this, m_server);
}

int vncDesktop::ScreenBuffSize()
{
    return m_scrinfo.format.bitsPerPixel/8 *
        m_scrinfo.framebufferWidth *
        m_scrinfo.framebufferHeight;
}

void vncDesktop::FillDisplayInfo(rfbServerInitMsg *scrinfo)
{
    memcpy(scrinfo, &m_scrinfo, sz_rfbServerInitMsg);
}

/*static void toBuff( HDIVE hDive , PRECTL rcl , PBYTE b , ULONG lineSize )
{
    PBYTE pVidAddress;

    BOOL  fFirstLineInRect;
    ULONG ulRemLinesInBank;
    ULONG ulBankNumber;
    ULONG ulMoreLine = rcl->yTop - rcl->yBottom;

    fFirstLineInRect = TRUE;

    while ( ulMoreLine )
    {
        if ( fFirstLineInRect )
        {
            // Get VRAM start location
            DiveCalcFrameBufferAddress( hDive, rcl, &pVidAddress, &ulBankNumber, &ulRemLinesInBank );
            DiveSwitchBank( hDive, ulBankNumber );
            fFirstLineInRect = FALSE;
        }
        // Copy one Line to buffer
        memcpy( b, pVidAddress, lineSize );

        // Update source address
        b += DiveCaps.ulScanLineBytes;

        ulMoreLine--;
        ulRemLinesInBank--;
        rcl->yTop--;

        // Check if time to switch banks.
        if ( !ulRemLinesInBank )
        {
            DiveCalcFrameBufferAddress( hDive, rcl, &pVidAddress, &ulBankNumber, &ulRemLinesInBank );
            DiveSwitchBank( hDive, ulBankNumber );
        }
        else
        {
            // update destination address.
            pVidAddress += DiveCaps.ulScanLineBytes;
        }

    } // end while: go to next scan line
}*/

// Function to capture an area of the screen immediately prior to sending
// an update.
void vncDesktop::CaptureScreen(const rfb::Rect &rect, BYTE *scrBuff, UINT scrBuffSize)
{
    assert(rect.enclosed_by(m_bmrect));

    /*if ( useDive )
    {
        RECTL r = { rect.tl.x , yscr - rect.br.y , rect.br.x , yscr - rect.tl.y };
        ULONG lineSize = (rect.br.x - rect.tl.x) * (DiveCaps.ulScanLineBytes/m_bminfo.bmi.cx);
        PBYTE destbuffpos = scrBuff + (m_bytesPerRow * rect.tl.y) + (rect.tl.x*(DiveCaps.ulScanLineBytes/m_bminfo.bmi.cx));
        toBuff( m_hdive , &r , destbuffpos , lineSize );
    }
    else
    { */
        // Select the memory bitmap into the memory DC
        HBITMAP oldbitmap;
        if ((oldbitmap = GpiSetBitmap(m_hmemps, m_membitmap)) == HBM_ERROR)
            return;

        // Capture screen into bitmap
        HPS scrps = WinGetScreenPS( HWND_DESKTOP );
        POINTL aptl[ 3 ];
        aptl[ 0 ].x = rect.tl.x;
        aptl[ 0 ].y = yscr - rect.br.y;
        aptl[ 1 ].x = rect.br.x;
        aptl[ 1 ].y = yscr - rect.tl.y;
        aptl[ 2 ].x = rect.tl.x;
        aptl[ 2 ].y = yscr - rect.br.y;
        LONG lHits = GpiBitBlt( m_hmemps , scrps , 3, aptl, ROP_SRCCOPY, BBO_IGNORE);
        WinReleasePS( scrps );

        // Select the old bitmap back into the memory DC
        GpiSetBitmap(m_hmemps, oldbitmap);

        if ( lHits == GPI_OK ) {
            // Copy the new data to the screen buffer (CopyToBuffer optimises this if possible)
            CopyToBuffer(rect, scrBuff, scrBuffSize);
        }
    //}
}

// Add the mouse pointer to the buffer
void vncDesktop::CaptureMouse(BYTE *scrBuff, UINT scrBuffSize)
{
    POINTL CursorPos;
    POINTERINFO IconInfo = { 0 };
    BOOL rc = FALSE;
    SIZEL s;


    // If the mouse cursor handle is invalid then forget it
    if (m_hcursor == NULL)
        return;

    // Get the cursor position
    if (!WinQueryPointerPos(HWND_DESKTOP,&CursorPos))
        return;

    // Translate position for hotspot
    if (WinQueryPointerInfo(m_hcursor, &IconInfo))
    {
        CursorPos.x -= ((int)IconInfo.xHotspot);
        CursorPos.y -= ((int)IconInfo.yHotspot);
    }

    /*if ( useDive )
    {
        m_cursorpos.tl.x = CursorPos.x;
        m_cursorpos.tl.y = yscr-(CursorPos.y+WinQuerySysValue(HWND_DESKTOP,SV_CYPOINTER));
        m_cursorpos.br.x = CursorPos.x+WinQuerySysValue(HWND_DESKTOP,SV_CXPOINTER);
        m_cursorpos.br.y = yscr-CursorPos.y;

        // Clip the bounding rect to the screen
        // Copy the mouse cursor into the screen buffer, if any of it is visible
        m_cursorpos = m_cursorpos.intersect(m_bmrect);
        if (!m_cursorpos.is_empty()) {

            RECTL r = { m_cursorpos.tl.x , yscr - m_cursorpos.br.y , m_cursorpos.br.x , yscr - m_cursorpos.tl.y };
            ULONG lineSize = (m_cursorpos.br.x - m_cursorpos.tl.x) * (DiveCaps.ulScanLineBytes/m_bminfo.bmi.cx);
            PBYTE destbuffpos = scrBuff + (m_bytesPerRow * m_cursorpos.tl.y) + (m_cursorpos.tl.x*(DiveCaps.ulScanLineBytes/m_bminfo.bmi.cx));
            toBuff( m_hdive , &r , destbuffpos , lineSize );
        }
    }
    else
    { */

        // Select the memory bitmap into the memory DC
        HBITMAP oldbitmap;
        if ((oldbitmap = GpiSetBitmap(m_hmemps, m_membitmap)) == HBM_ERROR)
            return;

        // Draw the cursor
        rc = WinDrawPointer(m_hmemps, CursorPos.x, CursorPos.y, m_hcursor, DP_NORMAL);
        if ( !rc || !GpiQueryBitmapDimension( IconInfo.hbmPointer , &s ) )
        {
            HPOINTER hp = WinQuerySysPointer( HWND_DESKTOP, SPTR_ARROW, FALSE );
            WinDrawPointer(m_hmemps, CursorPos.x, CursorPos.y, hp, DP_NORMAL);
        }
        // Select the old bitmap back into the memory DC
        GpiSetBitmap(m_hmemps, oldbitmap);

        // Save the bounding rectangle
        m_cursorpos.tl.x = CursorPos.x;
        m_cursorpos.tl.y = yscr-(CursorPos.y+WinQuerySysValue(HWND_DESKTOP,SV_CYPOINTER));
        m_cursorpos.br.x = CursorPos.x+WinQuerySysValue(HWND_DESKTOP,SV_CXPOINTER);
        m_cursorpos.br.y = yscr-CursorPos.y;

        // Clip the bounding rect to the screen
        // Copy the mouse cursor into the screen buffer, if any of it is visible
        m_cursorpos = m_cursorpos.intersect(m_bmrect);
        if (!m_cursorpos.is_empty()) {
            CopyToBuffer(m_cursorpos, scrBuff, scrBuffSize);
        }
    //}
}

// Return the current mouse pointer position
rfb::Rect vncDesktop::MouseRect()
{
    return m_cursorpos;
}

void vncDesktop::SetCursor(HPOINTER cursor)
{
    if (cursor == NULL)
        m_hcursor = m_hdefcursor;
    else
        m_hcursor = cursor;
}

// Manipulation of the clipboard
void vncDesktop::SetClipText(char* rfbStr)
{
    int len = strlen(rfbStr);
    char* winStr = new char[len*2+1];

    int j = 0;
    for (int i = 0; i < len; i++)
    {
        if (rfbStr[i] == 10)
            winStr[j++] = 13;
        winStr[j++] = rfbStr[i];
    }
    winStr[j++] = 0;

    // Open the system clipboard
    if (WinOpenClipbrd(hab))
    {
        // Empty it
        WinEmptyClipbrd(hab);

        char  *shm = NULL;
        ULONG  flag = OBJ_GIVEABLE | OBJ_TILE | PAG_COMMIT | PAG_READ | PAG_WRITE;

        if (DosAllocSharedMem((PPVOID)&shm, NULL, j+1, flag) == 0)
        {
            // Get the data
            strcpy(shm, winStr);
            // Tell the clipboard
            WinSetClipbrdData(hab, (ULONG)shm, CF_TEXT, CFI_POINTER);
        }

        delete [] winStr;

        // Now close it
        WinCloseClipbrd(hab);
    }
}

// INTERNAL METHODS

inline void vncDesktop::MaskToMaxAndShift(DWORD mask, CARD16 &max, CARD8 &shift)
{
    for (shift = 0; (mask & 1) == 0; shift++)
        mask >>= 1;
    max = (CARD16) mask;
}


// Copy data from the memory bitmap into a buffer
void vncDesktop::CopyToBuffer(const rfb::Rect &rect, BYTE *destbuff, UINT destbuffsize)
{
    // Finish drawing anything in this thread
    // Wish we could do this for the whole system - maybe we should
    // do something with LockWindowUpdate here.

    int y_inv;
    BYTE * destbuffpos;

    // Calculate the scanline-ordered y position to copy from
    y_inv = m_scrinfo.framebufferHeight-rect.tl.y-(rect.br.y-rect.tl.y);

    // Calculate where in the output buffer to put the data
    destbuffpos = destbuff + (m_bytesPerRow * rect.tl.y);

    // Get the actual bits from the bitmap into the bit buffer

    int scans = rect.br.y-rect.tl.y;

    // Select the memory bitmap into the memory DC
    HBITMAP oldbitmap;
    if ((oldbitmap = GpiSetBitmap(m_hmemps, m_membitmap)) == HBM_ERROR)
        return;

    // for testing
    //RECTL r = { rect.tl.x , yscr-rect.br.y , rect.br.x , yscr-rect.tl.y };
    //WinDrawBorder( m_hmemps , &r , 1 , 1 , 0 , 0 , DB_AREAATTRS );

    for ( int i = 0 ; i < scans ; i++ )
    {
        PBYTE dbp = destbuffpos + (m_bytesPerRow * i);
        LONG lScansReturned = GpiQueryBitmapBits( m_hmemps , ((y_inv+scans) - i)-1 , 1,
                                        dbp , (PBITMAPINFO2)&m_bminfo.bmi );
        if ( (lScansReturned == 0) || (lScansReturned == GPI_ALTERROR) )
        {
            vnclog.Print(LL_INTERR, VNCLOG("vncDesktop : [1] GpiQueryBitmapBits failed! %x\n"), WinGetLastError(hab));
            vnclog.Print(LL_INTERR, VNCLOG("vncDesktop : thread = %d, DPS = %d, bitmap = %d\n"), omni_thread::self(), m_hmemps, m_membitmap);
            vnclog.Print(LL_INTERR, VNCLOG("vncDesktop : y = %d, height = %d, i = %d\n"), y_inv, (rect.br.y-rect.tl.y), i);
            vnclog.Print(LL_INTERR, VNCLOG("vncDesktop : m_bminfo.bmi.cx = %d, m_bminfo.bmi.cy = %d, m_bminfo.bmi.cPlanes = %d, m_bminfo.bmi.cBitCount = %d\n"), m_bminfo.bmi.cx, m_bminfo.bmi.cy, m_bminfo.bmi.cPlanes, m_bminfo.bmi.cBitCount);
            break;
        }
    }


    //  Testing and playing commented

    //GpiQueryBitmapBits( m_hmemps , 0 , yscr,
    //                    destbuff , (PBITMAPINFO2)&m_bminfo.bmi );

    /*LONG lScansReturned = GpiQueryBitmapBits( m_hmemps , y_inv, (rect.br.y-rect.tl.y),
                        destbuffpos , (PBITMAPINFO2)&m_bminfo.bmi );
    if ( (lScansReturned == 0) || (lScansReturned == GPI_ALTERROR) )
    {
        vnclog.Print(LL_INTERR, VNCLOG("vncDesktop : [1] GpiQueryBitmapBits failed! %x\n"), WinGetLastError(hab));
        vnclog.Print(LL_INTERR, VNCLOG("vncDesktop : thread = %d, DPS = %d, bitmap = %d\n"), omni_thread::self(), m_hmemps, m_membitmap);
        vnclog.Print(LL_INTERR, VNCLOG("vncDesktop : y = %d, height = %d\n"), y_inv, (rect.br.y-rect.tl.y));
        vnclog.Print(LL_INTERR, VNCLOG("vncDesktop : m_bminfo.bmi.cx = %d, m_bminfo.bmi.cy = %d, m_bminfo.bmi.cPlanes = %d, m_bminfo.bmi.cBitCount = %d\n"), m_bminfo.bmi.cx, m_bminfo.bmi.cy, m_bminfo.bmi.cPlanes, m_bminfo.bmi.cBitCount);
    } */

    // Select the old bitmap back into the memory DC
    GpiSetBitmap(m_hmemps, oldbitmap);
}

// Routine to find out which windows have moved
// If copyrect detection isn't perfect then this call returns
// the copyrect destination region, to allow the caller to check
// for mistakes
void vncDesktop::CalcCopyRects(rfb::UpdateTracker &tracker)
{
    HWND foreground = WinQueryActiveWindow( HWND_DESKTOP );
    RECTL foreground_rect;

    // Actually, we just compare the new and old foreground window & its position
    if (foreground != m_foreground_window) {
        m_foreground_window=foreground;
        // Is the window invisible or can we not get its rect?
        if (!WinIsWindowVisible(foreground) ||
            !ErGetWindowRect(foreground, &foreground_rect)) {
            m_foreground_window_rect.clear();
        } else {
            m_foreground_window_rect = foreground_rect;
        }
    } else {
        // Same window is in the foreground - let's see if it's moved
        RECTL destrect;
        rfb::Rect dest;
        rfb::Point source;

        // Get the window rectangle
        if (WinIsWindowVisible(foreground) && ErGetWindowRect(foreground, &destrect))
        {
            rfb::Rect old_foreground_window_rect = m_foreground_window_rect;
            source = m_foreground_window_rect.tl;
            m_foreground_window_rect = dest = destrect;
            if (!dest.is_empty() && !old_foreground_window_rect.is_empty())
            {
                // Got the destination position.  Now send to clients!
                if (!source.equals(dest.tl))
                {
                    rfb::Point delta = rfb::Point(dest.tl.x-source.x, dest.tl.y-source.y);

                    // Clip the destination rectangle
                    dest = dest.intersect(m_bmrect);
                    if (dest.is_empty()) return;

                    // Clip the source rectangle
                    dest = dest.translate(delta.negate()).intersect(m_bmrect);
                    dest = dest.translate(delta);
                    if (!dest.is_empty()) {
                        // Tell the buffer about the copyrect
                        m_buffer.CopyRect(dest, delta);

                        // Notify all clients of the copyrect
                        tracker.add_copied(dest, delta);
                    }
                }
            }
        } else {
            m_foreground_window_rect.clear();
        }
    }
}

// Window procedure for the Desktop window
MRESULT EXPENTRY DesktopWndProc(HWND hwnd, ULONG iMsg, MPARAM mp1, MPARAM mp2)
{
    vncDesktop *_this = (vncDesktop*)WinQueryWindowULong(hwnd, QWL_USER);

    switch (iMsg)
    {

        // GENERAL

    case WM_SYSCOLORCHANGE:
    case WM_REALIZEPALETTE:
        // The palette colours have changed, so tell the server

        // Get the system palette
        if (!_this->SetPalette())
            WinPostMsg( hwnd, WM_QUIT, MPVOID, MPVOID);

        // Update any palette-based clients, too
        _this->m_server->UpdatePalette();
        return 0;

    case WM_DRAWCLIPBOARD:
        // The clipboard contents have changed
        if( _this->m_initialClipBoardSeen && _this->m_clipboard_active )
        {
            char *cliptext = NULL;

            // Open the clipboard
            if (WinOpenClipbrd(hab))
            {
                // Get the clipboard data
                char *clipdata = (char *)WinQueryClipbrdData( hab , CF_TEXT );

                // Copy it into a new buffer
                if (clipdata == NULL)
                    cliptext = NULL;
                else
                    cliptext = strdup(clipdata);

                WinCloseClipbrd( hab );
            }

            if (cliptext != NULL)
            {
                int cliplen = strlen(cliptext);
                char *unixtext = (char *)malloc(cliplen+1);

                // Replace CR-LF with LF - never send CR-LF on the wire,
                // since Unix won't like it
                int unixpos=0;
                for (int x=0; x<cliplen; x++)
                {
                    if (cliptext[x] != '\x0d')
                    {
                        unixtext[unixpos] = cliptext[x];
                        unixpos++;
                    }
                }
                unixtext[unixpos] = 0;

                // Free the clip text
                free(cliptext);
                cliptext = NULL;

                // Now send the unix text to the server
                _this->m_server->UpdateClipText(unixtext);

                free(unixtext);
            }
        }

        _this->m_initialClipBoardSeen = TRUE;
        return (MRESULT)FALSE;

    default:
        return WinDefWindowProc(hwnd, iMsg, mp1, mp2);
    }
}


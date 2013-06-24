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


////////////////////////////
// System headers
#include "stdhdrs.h"

#include <string.h>
#include <ctype.h>
#include <netdb.h>

////////////////////////////
// Custom headers
#include "res.h"
#include "VSocket.h"
#include "PMVNC.h"

#include "vncServer.h"
#include "vncMenu.h"
#include "vncInstHandler.h"

// Standard command-line flag definitions
const char pmvncShowProperties[]   = "-settings";
const char pmvncShowAbout[]        = "-about";
const char pmvncKillRunningCopy[]  = "-kill";
const char pmvncAddNewClient[]     = "-connect";
const char pmvncShowHelp[]         = "-help";
// Usage string
const char pmvncUsageText[]        = "To send signal to running copy, use:\n" \
                                     "pmvnc [-kill] [-connect <host>] [-connect] [-settings] [-about]\n";

const char  *szAppName = "PMVNC";
HAB hab    = NULLHANDLE;
HMQ hmq    = NULLHANDLE;
SHORT yscr = 0;

ULONG RFB_SCREEN_UPDATE   = 0;
ULONG RFB_MOUSE_UPDATE    = 0;
ULONG VNC_DEFERRED_UPDATE = 0;

void PMVNCAppMain();

void sendCmd( USHORT cmd , MPARAM mp )
{
    HWND vncWindow = vncInstHandler::GetHWND();
    if ( vncWindow == NULLHANDLE )
    {
        WinMessageBox( HWND_DESKTOP , HWND_DESKTOP ,
                       (PSZ)"No existing instance of PMVNC could be contacted.",
                       (PSZ)szAppName, 10001 , MB_ERROR | MB_OK | MB_MOVEABLE );
    }
    else
    {
        WinPostMsg( vncWindow , WM_COMMAND , MPFROMSHORT( cmd ) , mp );
    }
}

// main parses the command line and either calls the main App routine.
int main( int argc , char *argv[] )
{
    //PPIB pib;
    //PTIB tib;
    //DosGetInfoBlocks(&tib, &pib);
    //pib->pib_ultype = 3;

    omni_thread::stacksize( 32768 );

    if ( ( hab = WinInitialize( 0 ) ) == NULLHANDLE )
    {
        DosBeep( 100 , 100 );
        return 1;
    }
    if ( ( hmq = WinCreateMsgQueue( hab , 0 ) ) == NULLHANDLE )
    {
        DosBeep( 100 , 100 );
        WinTerminate( hab );
        return 1;
    }

    // Make the command-line lowercase and parse it
    if ( argc > 1 )
    {
        int i;
        char szCmdLine[ 512 ];
        strcpy( szCmdLine , argv[ 1 ] );

        for ( i = 2 ; i < argc ; i++ )
        {
            strcat( szCmdLine , " " );
            strcat( szCmdLine , argv[ i ] );
        }

        for ( i = 0;  i < strlen( szCmdLine ) ; i++ )
        {
            szCmdLine[i] = tolower( szCmdLine[i] );
        }

        BOOL argfound = FALSE;
        for (i = 0; i < strlen(szCmdLine); i++)
        {
            if (szCmdLine[i] <= ' ')
                continue;
            argfound = TRUE;

            // Now check for command-line arguments
            if (strncmp(&szCmdLine[i], pmvncShowProperties, strlen(pmvncShowProperties)) == 0)
            {
                // Show the Properties dialog of an existing instance of PMVNC
                sendCmd( CM_PROPERTIES , MPVOID );
                i+=strlen(pmvncShowProperties);
                continue;
            }
            if (strncmp(&szCmdLine[i], pmvncShowAbout, strlen(pmvncShowAbout)) == 0)
            {
                // Show the About dialog of an existing instance of PMVNC
                sendCmd( CM_ABOUT , MPVOID );
                i+=strlen(pmvncShowAbout);
                continue;
            }
            if (strncmp(&szCmdLine[i], pmvncKillRunningCopy, strlen(pmvncKillRunningCopy)) == 0)
            {
                // Kill any already running copy of PMVNC
                sendCmd( CM_CLOSE , MPVOID );
                i+=strlen(pmvncKillRunningCopy);
                continue;
            }
            if (strncmp(&szCmdLine[i], pmvncAddNewClient, strlen(pmvncAddNewClient)) == 0)
            {
                // Add a new client to an existing copy of pmvnc
                i+=strlen(pmvncAddNewClient);

                // First, we have to parse the command line to get the filename to use
                int start, end;
                start=i;
                while (szCmdLine[start] <= ' ' && szCmdLine[start] != 0) start++;
                end = start;
                while (szCmdLine[end] > ' ') end++;

                // Was there a filename given?
                if (end-start > 0) {
                    char *name = new char[end-start+1];
                    if (name) {
                        strncpy(name, &(szCmdLine[start]), end-start);
                        name[end-start] = 0;
                        VCard32 address = VSocket::Resolve(name);
                        if (address != 0) {
                            // Post the IP address to the server
                            sendCmd( CM_OUTGOING_CONNIP , (MPARAM)address );
                        } else {
                            WinMessageBox( HWND_DESKTOP , HWND_DESKTOP ,
                                (PSZ)"Unable to resolve host name",
                                (PSZ)"Add New Client Error", 10001, MB_OK );
                        }
                        delete [] name;
                    }
                    i=end;
                } else {
                    // Tell the server to show the Add New Client dialog
                    sendCmd( CM_OUTGOING_CONN , MPVOID );
                }
                continue;
            }

            // Either the user gave the -help option or there is something odd on the cmd-line!

            // Show the usage dialog
            WinMessageBox(HWND_DESKTOP, HWND_DESKTOP, (PSZ)pmvncUsageText,
                          (PSZ)"PMVNC Usage", 10001, MB_OK | MB_INFORMATION | MB_MOVEABLE );
            break;
        }
    }


    if ( argc == 1 )
    {
        PMVNCAppMain();
    }

    WinDestroyMsgQueue( hmq );
    WinTerminate( hab );
    return 0;
}

typedef VOID (EXPENTRY * mouse_eventf)( HAB , ULONG , LONG , LONG );
static mouse_eventf dll_mouse_event;

typedef VOID (EXPENTRY *keyb_eventf)( HAB , BYTE , BYTE , ULONG );
static keyb_eventf dll_keyb_event;

typedef VOID (EXPENTRY * releaseMsgInputf)( HAB );
static releaseMsgInputf releaseMsgInput;

typedef VOID (EXPENTRY * SetHmqF)( HMQ );
static SetHmqF ER_SetHMQ;

typedef VOID (EXPENTRY * UnSetHmqF)();
static UnSetHmqF ER_UnSetHMQ;

typedef VOID (EXPENTRY * SetHooksPropsF)( ULONG , ULONG , ULONG , BOOL , BOOL );
static SetHooksPropsF ER_SetHooksProps;

static PFN ER_InputHook , ER_SendMsgHook;
static HMODULE hookDll = NULLHANDLE;
static BOOL hooksActive  = FALSE;

static vncServer *server = NULL;


//#define PMVNC_MSG_OFFSET  0xD462  // Just some random (hope unused by other)

omni_mutex *minput = NULL;

// BlackOut/ScreenSaver support
void unBlank()
{
    HEV hevSuspend = NULLHANDLE;
    HEV hevWait    = NULLHANDLE;
    HEV hevOff     = NULLHANDLE;
    HEV hevDpmsSs  = NULLHANDLE;
    HEV hevDpmsBl  = NULLHANDLE;

    // BlackOut
    if ( DosOpenEventSem((PCSZ)"\\SEM32\\BLACKOUT\\SUSPEND", &hevSuspend ) != 0 )
        hevSuspend = NULLHANDLE;
    if ( DosOpenEventSem((PCSZ)"\\SEM32\\BLACKOUT\\WAIT", &hevWait ) != 0 )
        hevWait = NULLHANDLE;
    if ( DosOpenEventSem((PCSZ)"\\SEM32\\BLACKOUT\\OFF", &hevOff ) != 0 )
        hevOff = NULLHANDLE;
    // ScreenSaver
    if ( DosOpenEventSem((PCSZ)"\\SEM32\\SSAVER\\DPMS", &hevDpmsSs ) != 0 )
        hevDpmsSs = NULLHANDLE;
    // Blanker
    if ( DosOpenEventSem((PCSZ)"\\SEM32\\BLANKER\\DPMS", &hevDpmsBl ) != 0 )
        hevDpmsBl = NULLHANDLE;

    if ( hevSuspend != NULLHANDLE )  DosPostEventSem( hevSuspend );
    if ( hevWait != NULLHANDLE )  DosPostEventSem( hevWait );
    if ( hevOff != NULLHANDLE )  DosPostEventSem( hevOff );
    if ( hevDpmsSs != NULLHANDLE )  DosPostEventSem( hevDpmsSs );
    if ( hevDpmsBl != NULLHANDLE )  DosPostEventSem( hevDpmsBl );

    if ( hevSuspend != NULLHANDLE )  DosCloseEventSem( hevSuspend );
    if ( hevWait != NULLHANDLE )  DosCloseEventSem( hevWait );
    if ( hevOff != NULLHANDLE )  DosCloseEventSem( hevOff );
    if ( hevDpmsSs != NULLHANDLE )  DosCloseEventSem( hevDpmsSs );
    if ( hevDpmsBl != NULLHANDLE )  DosCloseEventSem( hevDpmsBl );
}



void ActivateHooks( HMQ thmq )
{
    unBlank();

    if ( hooksActive )  return;

    HATOMTBL sysatoms   = WinQuerySystemAtomTable();
    //RFB_SCREEN_UPDATE   = PMVNC_MSG_OFFSET + 1;
    //RFB_MOUSE_UPDATE    = PMVNC_MSG_OFFSET + 2;
    //VNC_DEFERRED_UPDATE = PMVNC_MSG_OFFSET + 3;
    RFB_SCREEN_UPDATE   = WinAddAtom( sysatoms , (PSZ)"PMVNC.Update.DrawRect" );
    RFB_MOUSE_UPDATE    = WinAddAtom( sysatoms , (PSZ)"PMVNC.Update.Mouse" );
    VNC_DEFERRED_UPDATE = WinAddAtom( sysatoms , (PSZ)"PMVNCHooks.Deferred.UpdateMessage" );
    ER_SetHooksProps( RFB_SCREEN_UPDATE , RFB_MOUSE_UPDATE , VNC_DEFERRED_UPDATE ,
                      server->UseTimer() , server->DeferredUpdates() );
    WinSetHook( hab , NULLHANDLE , HK_INPUT , ER_InputHook , hookDll );
    WinSetHook( hab , NULLHANDLE , HK_SENDMSG , ER_SendMsgHook , hookDll );
    ER_SetHMQ( thmq );
    minput = new omni_mutex;
    hooksActive = TRUE;
}

void DeactivateHooks()
{
    if ( !hooksActive )  return;

    hooksActive = FALSE;
    delete minput;
    ER_UnSetHMQ();
    WinReleaseHook( hab , NULLHANDLE , HK_SENDMSG , ER_SendMsgHook , hookDll );
    WinReleaseHook( hab , NULLHANDLE , HK_INPUT , ER_InputHook , hookDll );
    HATOMTBL sysatoms = WinQuerySystemAtomTable();
    WinDeleteAtom( sysatoms , RFB_SCREEN_UPDATE);
    WinDeleteAtom( sysatoms , RFB_MOUSE_UPDATE );
    WinDeleteAtom( sysatoms , VNC_DEFERRED_UPDATE );
}


//
//  dll_*_event functions not reenterable and must be serialized
//
//  if ( hooksActive ), lock mutex, if ( hooksActive ) = safe-safe sex
//
void mouse_event( HAB h , ULONG f , LONG x , LONG y )
{
    if ( hooksActive )
    {
        omni_mutex_lock l( *minput );
        if ( hooksActive )  dll_mouse_event( h , f , x , y );
    }
}

void keyb_event( HAB h , BYTE vk , BYTE sc , ULONG flags )
{
    if ( hooksActive )
    {
        omni_mutex_lock l( *minput );
        if ( hooksActive )  dll_keyb_event( h , vk , sc , flags );
    }
}

static BOOL exitOk = FALSE;

VOID APIENTRY ExitProc( ULONG dummy )
{
    if ( !exitOk )
    {
        if ( hooksActive )
        {
            hooksActive = FALSE;
            ER_UnSetHMQ();
            WinReleaseHook( hab , NULLHANDLE , HK_SENDMSG , ER_SendMsgHook , hookDll );
            WinReleaseHook( hab , NULLHANDLE , HK_INPUT , ER_InputHook , hookDll );
        }

        DosFreeModule( hookDll );
        WinTerminate( hab );
    }
    DosExitList( EXLST_EXIT , NULL );
}

void PMVNCAppMain()
{
    setbuf(stderr, 0);

    // Configure the log file, in case one is required
    vnclog.SetLevel( vncProperties::LoadInt("LogLevel",1) );
    char *lfn = vncProperties::LoadString("LogFilename");
    if (lfn == NULL)
    {
        lfn = new char[ 10 ];
        strcpy( lfn , "pmvnc.log" );
    }
    vnclog.SetFile( lfn , vncProperties::LoadInt("LogAppend",0) );
    delete lfn;
    vnclog.SetLoggingOn( vncProperties::LoadInt("Logging",1) );

    vnclog.Print(LL_STATE, VNCLOG("Starting PMVNC...\n"));

    QMSG qmsg;

    // Check for previous instances of PMVNC!
    vncInstHandler instancehan;
    if (!instancehan.Init())
    {
        // We don't allow multiple instances!
        WinMessageBox( HWND_DESKTOP , HWND_DESKTOP ,
                       (PSZ)"Another instance of PMVNC is already running.",
                       (PSZ)szAppName, 10001 , MB_ERROR | MB_OK | MB_MOVEABLE );
        return;
    }

    if ( DosLoadModule( NULL , 0 , (PSZ)"PMVNCHK" , &hookDll ) != 0 )
    {
        WinMessageBox( HWND_DESKTOP , HWND_DESKTOP , (PSZ)"Error loading PMVNCHK.DLL" ,
                       (PSZ)szAppName, 10001 , MB_ERROR | MB_OK | MB_MOVEABLE );
        return;
    }

    hooksActive = FALSE;
    DosExitList( EXLST_ADD , ExitProc );

    DosQueryProcAddr( hookDll , 0 , (PSZ)"ER_SetHooksProps" , (PFN*)&ER_SetHooksProps );
    DosQueryProcAddr( hookDll , 0 , (PSZ)"ER_InputHook" , &ER_InputHook );
    DosQueryProcAddr( hookDll , 0 , (PSZ)"ER_SendMsgHook" , &ER_SendMsgHook );
    DosQueryProcAddr( hookDll , 0 , (PSZ)"ER_SetHMQ" , (PFN*)&ER_SetHMQ );
    DosQueryProcAddr( hookDll , 0 , (PSZ)"ER_UnSetHMQ" , (PFN*)&ER_UnSetHMQ );
    DosQueryProcAddr( hookDll , 0 , (PSZ)"ER_mouse_event" , (PFN *)&dll_mouse_event );
    DosQueryProcAddr( hookDll , 0 , (PSZ)"ER_keyb_event" , (PFN *)&dll_keyb_event );

    yscr = WinQuerySysValue(HWND_DESKTOP, SV_CYSCREEN);

    // CREATE SERVER
    server = new vncServer;

    // Set the name and port number
    server->SetName(szAppName);
    vnclog.Print(LL_STATE, VNCLOG("server created ok\n"));

    // Create tray icon & menu if we're running as an app
    vncMenu *menu = new vncMenu(server);
    if (menu == NULL)
    {
        vnclog.Print(LL_INTERR, VNCLOG("failed to create tray menu/service window\n"));
        WinPostQueueMsg( hmq , WM_QUIT , MPVOID , MPVOID );
    }
    else  instancehan.SetHWND( menu->GetHWND() );

    // Now enter the message handling loop until told to quit!
    while ( WinGetMsg( hab , &qmsg , 0 , 0 , 0 ) )
    {
        vnclog.Print(LL_INTINFO, VNCLOG("message %d received\n"), qmsg.msg);
        WinDispatchMsg( hab , &qmsg );
    }

    vnclog.Print(LL_STATE, VNCLOG("shutting down server\n"));

    if (menu != NULL)
      delete menu;

    delete server;

    exitOk = TRUE;
    if ( hooksActive )
    {
        hooksActive = FALSE;
        ER_UnSetHMQ();
        WinReleaseHook( hab , NULLHANDLE , HK_SENDMSG , ER_SendMsgHook , hookDll );
        WinReleaseHook( hab , NULLHANDLE , HK_INPUT , ER_InputHook , hookDll );
    }
    DosFreeModule( hookDll );
    return;
};

BOOL GetCompName( char *n , int nlen )
{
    BOOL rval = FALSE;
    if (gethostname(n, nlen) == 0)
    {
        if ( strcmp( n , "localhost" ) != 0 )  rval = TRUE;
    }
    return rval;
}


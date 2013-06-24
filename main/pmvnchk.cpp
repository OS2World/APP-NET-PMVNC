#define INCL_DOSDEVIOCTL
#define INCL_DOS
#define INCL_DOSERRORS
#define INCL_WIN
#include <os2.h>

#include "dgis.h"

// ********************************************************
// ***                 C RTL section                    ***
// ******************************************** BEGIN *****
void *memset(void *s, int c, unsigned n)
{
    for (unsigned i = 0; i < n; ++i)  ((char *)s)[i] = (char)c;
    return (s);
}
// ********************************************************
// ***                 C RTL section                    ***
// ********************************************** END *****


// ********************************************************
// ***               Defines section                    ***
// ******************************************** BEGIN *****
extern "C" HMODULE _os2hmod;

#define MOUSEEVENTF_MOVE        0x00000001
#define MOUSEEVENTF_LEFTDOWN    0x00000002
#define MOUSEEVENTF_LEFTUP      0x00000004
#define MOUSEEVENTF_RIGHTDOWN   0x00000008
#define MOUSEEVENTF_RIGHTUP     0x00000010
#define MOUSEEVENTF_MIDDLEDOWN  0x00000020
#define MOUSEEVENTF_MIDDLEUP    0x00000040

#define KEYEVENTF_KEYUP         0x00000001
// ********************************************************
// ***               Defines section                    ***
// ********************************************** END *****


// ********************************************************
// ***           Static variables section               ***
// ******************************************** BEGIN *****

static QMSG event;
static struct { USHORT ch, sc1, sc2, vk; ULONG flags; } kEvent;

static HEV  hookEnd;

static ULONG RFB_SCREEN_UPDATE;
static ULONG RFB_MOUSE_UPDATE;
static ULONG VNC_DEFERRED_UPDATE;

static BOOL use_Timer;
static BOOL use_Deferral;
static BOOL use_GetUpdateRect;

static HPOINTER old_cursor;
static HMQ hmq;
static SHORT yscr;
static PLINFOSEG locPtr;


void initStaticVars()
{
    memset( &event , 0 , sizeof( event ) );
    memset( &kEvent , 0 , sizeof( kEvent ) );
    hookEnd = NULLHANDLE;

    RFB_SCREEN_UPDATE   = 0;
    RFB_MOUSE_UPDATE    = 0;
    VNC_DEFERRED_UPDATE = 0;

    use_Timer         = TRUE;
    use_Deferral      = FALSE;   // If true, interferes with Odin-based apps.
    use_GetUpdateRect = TRUE;

    old_cursor = NULLHANDLE;
    hmq        = NULLHANDLE;
    yscr       = (SHORT)WinQuerySysValue( HWND_DESKTOP , SV_CYSCREEN );
    locPtr     = NULL;
}

// ********************************************************
// ***           Static variables section               ***
// ********************************************** END *****


// ********************************************************
// ***           Input simulation section               ***
// ******************************************** BEGIN *****

// KbdPacket structure from PMWIN.DLL
struct KBDPACKET
{
    BYTE monFlags;
    BYTE chScan1;      // scancode, on keyup | 0x80
    BYTE chChar;       // char, same as KBDKEYINFO.chChar
    BYTE chScan;       // scancode, same as KBDKEYINFO.chScan
    USHORT fbStatus;   // same as KBDKEYINFO.fbStatus
    USHORT fsState;    // same as KBDKEYINFO.fsState
    ULONG  time;       // same as KBDKEYINFO.time
    USHORT fsDD;       // same as KBDTRANS.fsDD  (KbdDDFlagWord)
};

extern KBDPACKET APIENTRY KbdPacket;

void makeKeybMsg();

#pragma argsused
BOOL EXPENTRY ER_MsgInputHook( HAB hab , PQMSG pQmsg , BOOL fSkip , PBOOL pfNoRecord )
{
    BOOL rc = FALSE;
    *pfNoRecord = TRUE;

    if ( fSkip )
    {
        event.msg = 0;
        WinReleaseHook( hab , NULLHANDLE , HK_MSGINPUT , (PFN)ER_MsgInputHook , _os2hmod );
        DosOpenEventSem( NULL , &hookEnd );
        DosPostEventSem( hookEnd );
        DosCloseEventSem( hookEnd );
    }
    else
    {
        if ( event.msg != 0 )
        {
            if ( event.msg == WM_VIOCHAR )  // Keyboard message
            {
                makeKeybMsg();
            }

            pQmsg->msg  = event.msg;
            pQmsg->mp1  = event.mp1;
            pQmsg->mp2  = event.mp2;
            pQmsg->time = WinGetCurrentTime( hab );

            if ( event.msg == WM_VIOCHAR )  // Keyboard message
            {
                // Windowed DOS sessions support
                KbdPacket.monFlags = 0;

                if ( ( kEvent.sc1 >= 0x60 ) && ( kEvent.sc1 <= 0x69 ) )
                {
                    // Cursor keys workaround
                    KbdPacket.chScan1  = kEvent.sc2;
                }
                else  KbdPacket.chScan1  = kEvent.sc1;
                if ( kEvent.flags & KEYEVENTF_KEYUP )  KbdPacket.chScan1 |= 0x80;
                KbdPacket.chChar  = kEvent.ch;
                KbdPacket.chScan  = kEvent.sc2;
                KbdPacket.time    = pQmsg->time;
                KbdPacket.fsDD    = 0;
                if ( kEvent.flags & KEYEVENTF_KEYUP )  KbdPacket.fsDD |= 0x40;
            }
            rc = TRUE;
        }
    }
    return rc;
}

// NOTE: Not reenterable! Access must be serialized!
extern "C" VOID EXPENTRY ER_mouse_event( HAB hab , ULONG flags , LONG x , LONG y )
{
    if ( flags & MOUSEEVENTF_MOVE )
    {
        WinSetPointerPos( HWND_DESKTOP , x , y );
    }

    flags &= ~MOUSEEVENTF_MOVE;

    event.mp1 = MPFROM2SHORT( x , y );
    event.mp2 = MPVOID;
    switch ( flags )
    {
        case MOUSEEVENTF_LEFTDOWN:
            event.msg = WM_BUTTON1DOWN;
            break;
        case MOUSEEVENTF_LEFTUP:
            event.msg = WM_BUTTON1UP;
            break;
        case MOUSEEVENTF_RIGHTDOWN:
            event.msg = WM_BUTTON2DOWN;
            break;
        case MOUSEEVENTF_RIGHTUP:
            event.msg = WM_BUTTON2UP;
            break;
        case MOUSEEVENTF_MIDDLEDOWN:
            event.msg = WM_BUTTON3DOWN;
            break;
        case MOUSEEVENTF_MIDDLEUP:
            event.msg = WM_BUTTON3UP;
            break;
        default:
            event.msg = 0;
    }

    if ( event.msg != 0 )
    {
        if ( hookEnd == NULLHANDLE )
            return;

        ULONG postCnt;
        DosResetEventSem( hookEnd , &postCnt );

        if ( !WinSetHook( hab , NULLHANDLE , HK_MSGINPUT , (PFN)ER_MsgInputHook , _os2hmod ) )
        {
            return;
        }
        WinCheckInput( hab );
        if ( DosWaitEventSem( hookEnd , 1000 ) == ERROR_TIMEOUT )
        {
            WinReleaseHook( hab , NULLHANDLE , HK_MSGINPUT , (PFN)ER_MsgInputHook , _os2hmod );
        }
        event.msg = 0;
    }
}

// Mapping from PM Virtual Key to scancode
// sc1 , sc2 , chpm , chvio / no mod, ctrl , alt , shift
static unsigned char vk2sc[][16] = {
/* 00 - undef        */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
/* 01 - VK_BUTTON1   */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
/* 02 - VK_BUTTON2   */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
/* 03 - VK_BUTTON3   */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
/* 04 - VK_BREAK     */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
/* 05 - VK_BACKSPACE */ {0x0e,0x0e,0x08,0x08, 0x0e,0x0e,0x08,0x7f, 0x0e,0x0e,0x08,0x08, 0x0e,0x0e,0x08,0x08},
/* 06 - VK_TAB       */ {0x0f,0x0f,0x09,0x09, 0x0f,0x94,0x09,0x00, 0x0f,0x0f,0x00,0x00, 0x0f,0x0f,0x09,0x00},
/* 07 - VK_BACKTAB   */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
/* 08 - VK_NEWLINE   */ {0x1c,0x1c,0x0d,0x0d, 0x1c,0x1c,0x0d,0x0a, 0x1c,0x1c,0x00,0x00, 0x1c,0x1c,0x0d,0x0d},
/* 09 - VK_SHIFT     */ {0x2a,0x00,0x00,0x00, 0x2a,0x00,0x00,0x00, 0x2a,0x00,0x00,0x00, 0x2a,0x00,0x00,0x00},
/* 0a - VK_CTRL      */ {0x1d,0x00,0x00,0x00, 0x1d,0x00,0x00,0x00, 0x1d,0x00,0x00,0x00, 0x1d,0x00,0x00,0x00},
/* 0b - VK_ALT       */ {0x38,0x00,0x00,0x00, 0x38,0x00,0x00,0x00, 0x38,0x00,0x00,0x00, 0x38,0x00,0x00,0x00},
/* 0c - VK_ALTGRAF   */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
/* 0d - VK_PAUSE     */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
/* 0e - VK_CAPSLOCK  */ {0x3a,0x00,0x00,0x00, 0x3a,0x00,0x00,0x00, 0x3a,0x00,0x00,0x00, 0x3a,0x00,0x00,0x00},
/* 0f - VK_ESC       */ {0x01,0x01,0x1b,0x1b, 0x01,0x01,0x00,0x00, 0x01,0x01,0x00,0x00, 0x01,0x01,0x1b,0x1b},
/* 10 - VK_SPACE     */ {0x39,0x39,0x20,0x20, 0x39,0x39,0x20,0x20, 0x39,0x39,0x20,0x20, 0x39,0x39,0x20,0x20},
/* 11 - VK_PAGEUP    */ {0x62,0x49,0x00,0xe0, 0x62,0x84,0x00,0xe0, 0x62,0x99,0x00,0x00, 0x62,0x49,0x00,0xe0},
/* 12 - VK_PAGEDOWN  */ {0x67,0x51,0x00,0xe0, 0x67,0x76,0x00,0xe0, 0x67,0xa1,0x00,0x00, 0x67,0x51,0x00,0xe0},
/* 13 - VK_END       */ {0x65,0x4f,0x00,0xe0, 0x65,0x75,0x00,0xe0, 0x65,0x9f,0x00,0x00, 0x65,0x4f,0x00,0xe0},
/* 14 - VK_HOME      */ {0x60,0x47,0x00,0xe0, 0x60,0x77,0x00,0xe0, 0x60,0x97,0x00,0x00, 0x60,0x47,0x00,0xe0},
/* 15 - VK_LEFT      */ {0x63,0x4b,0x00,0xe0, 0x63,0x73,0x00,0xe0, 0x63,0x9b,0x00,0x00, 0x63,0x4b,0x00,0xe0},
/* 16 - VK_UP        */ {0x61,0x48,0x00,0xe0, 0x61,0x8d,0x00,0xe0, 0x61,0x98,0x00,0x00, 0x61,0x48,0x00,0xe0},
/* 17 - VK_RIGHT     */ {0x64,0x4d,0x00,0xe0, 0x64,0x74,0x00,0xe0, 0x64,0x9d,0x00,0x00, 0x64,0x4d,0x00,0xe0},
/* 18 - VK_DOWN      */ {0x66,0x50,0x00,0xe0, 0x66,0x91,0x00,0xe0, 0x66,0xa0,0x00,0x00, 0x66,0x50,0x00,0xe0},
/* 19 - VK_PRINTSCRN */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
/* 1a - VK_INSERT    */ {0x68,0x52,0x00,0xe0, 0x68,0x92,0x00,0xe0, 0x68,0xa2,0x00,0x00, 0x68,0x52,0x00,0xe0},
/* 1b - VK_DELETE    */ {0x69,0x53,0x00,0xe0, 0x69,0x93,0x00,0xe0, 0x69,0xa3,0x00,0x00, 0x69,0x53,0x00,0xe0},
/* 1c - VK_SCRLLOCK  */ {0x46,0x00,0x00,0x00, 0x46,0x00,0x00,0x00, 0x46,0x00,0x00,0x00, 0x46,0x00,0x00,0x00},
/* 1d - VK_NUMLOCK   */ {0x45,0x00,0x00,0x00, 0x45,0x00,0x00,0x00, 0x45,0x00,0x00,0x00, 0x45,0x00,0x00,0x00},
/* 1e - VK_ENTER     */ {0x1c,0x1c,0x0d,0x0d, 0x1c,0x1c,0x0d,0x0a, 0x1c,0x1c,0x00,0x00, 0x1c,0x1c,0x0d,0x0d},
/* 1f - VK_SYSRQ     */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
/* 20 - VK_F1        */ {0x3b,0x3b,0x00,0x00, 0x3b,0x5e,0x00,0x00, 0x3b,0x68,0x00,0x00, 0x3b,0x54,0x00,0x00},
/* 21 - VK_F2        */ {0x3c,0x3c,0x00,0x00, 0x3c,0x5f,0x00,0x00, 0x3c,0x69,0x00,0x00, 0x3c,0x55,0x00,0x00},
/* 22 - VK_F3        */ {0x3d,0x3d,0x00,0x00, 0x3d,0x60,0x00,0x00, 0x3d,0x6a,0x00,0x00, 0x3d,0x56,0x00,0x00},
/* 23 - VK_F4        */ {0x3e,0x3e,0x00,0x00, 0x3e,0x61,0x00,0x00, 0x3e,0x6b,0x00,0x00, 0x3e,0x57,0x00,0x00},
/* 24 - VK_F5        */ {0x3f,0x3f,0x00,0x00, 0x3f,0x62,0x00,0x00, 0x3f,0x6c,0x00,0x00, 0x3f,0x58,0x00,0x00},
/* 25 - VK_F6        */ {0x40,0x40,0x00,0x00, 0x40,0x63,0x00,0x00, 0x40,0x6d,0x00,0x00, 0x40,0x59,0x00,0x00},
/* 26 - VK_F7        */ {0x41,0x41,0x00,0x00, 0x41,0x64,0x00,0x00, 0x41,0x6e,0x00,0x00, 0x41,0x5a,0x00,0x00},
/* 27 - VK_F8        */ {0x42,0x42,0x00,0x00, 0x42,0x65,0x00,0x00, 0x42,0x6f,0x00,0x00, 0x42,0x5b,0x00,0x00},
/* 28 - VK_F9        */ {0x43,0x43,0x00,0x00, 0x43,0x66,0x00,0x00, 0x43,0x70,0x00,0x00, 0x43,0x5c,0x00,0x00},
/* 29 - VK_F10       */ {0x44,0x44,0x00,0x00, 0x44,0x67,0x00,0x00, 0x44,0x71,0x00,0x00, 0x44,0x5d,0x00,0x00},
/* 2a - VK_F11       */ {0x57,0x85,0x00,0x00, 0x57,0x89,0x00,0x00, 0x57,0x8b,0x00,0x00, 0x57,0x87,0x00,0x00},
/* 2b - VK_F12       */ {0x58,0x86,0x00,0x00, 0x58,0x8a,0x00,0x00, 0x58,0x8c,0x00,0x00, 0x58,0x88,0x00,0x00}
};


// Mapping from charcode to scancode
static BYTE ch2sc[] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 0 - 31
                        0x39,0x02,0x28,0x04,0x05,0x06,0x08,0x28, // ' ' - '''
                        0x0a,0x0b,0x09,0x0d,0x33,0x0c,0x34,0x35, // '(' - '/'
                        0x0b,0x02,0x03,0x04,0x05,0x06,0x07,0x08, // '0' - '7'
                        0x09,0x0a,0x27,0x27,0x33,0x0d,0x34,0x35, // '8' - '?'
                        0x03,0x1e,0x30,0x2e,0x20,0x12,0x21,0x22, // '@' - 'G'
                        0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18, // 'H' - 'O'
                        0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11, // 'P' - 'W'
                        0x2d,0x15,0x2c,0x1a,0x2b,0x1b,0x07,0x0c, // 'X' - '_'
                        0x29,0x1e,0x30,0x2e,0x20,0x12,0x21,0x22, // '`' - 'g'
                        0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18, // 'h' - 'o'
                        0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11, // 'p' - 'w'
                        0x2d,0x15,0x2c,0x1a,0x2b,0x1b,0x29,0x00, // 'x' - 127
                        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };


static void makeKeybMsg()
{
    USHORT flgs = 0, chpm = 0, chvio = 0, sc1 = 0, sc2 = 0;

    if ( kEvent.flags & KEYEVENTF_KEYUP )  flgs |= KC_KEYUP;

    chpm = chvio = kEvent.ch;
    sc1  = sc2   = kEvent.sc2;

    if ( kEvent.vk != 0 )
    {
        sc1   = vk2sc[ kEvent.vk ][ 0 ];
        sc2   = vk2sc[ kEvent.vk ][ 1 ];
        chpm  = vk2sc[ kEvent.vk ][ 2 ];
        chvio = vk2sc[ kEvent.vk ][ 3 ];
    }

    if ( WinGetKeyState( HWND_DESKTOP, VK_CTRL ) & 0x8000 )
    {
        flgs |= KC_CTRL;
        if ( kEvent.vk != 0 )
        {
            sc1   = vk2sc[ kEvent.vk ][ 4 ];
            sc2   = vk2sc[ kEvent.vk ][ 5 ];
            chpm  = vk2sc[ kEvent.vk ][ 6 ];
            chvio = vk2sc[ kEvent.vk ][ 7 ];
        }
    }
    if ( WinGetKeyState( HWND_DESKTOP, VK_ALT ) & 0x8000 )
    {
        flgs |= KC_ALT;
        chpm = chvio = 0;
        if ( kEvent.vk != 0 )
        {
            sc1   = vk2sc[ kEvent.vk ][ 8 ];
            sc2   = vk2sc[ kEvent.vk ][ 9 ];
            chpm  = vk2sc[ kEvent.vk ][ 10 ];
            chvio = vk2sc[ kEvent.vk ][ 11 ];
        }
    }
    if ( WinGetKeyState( HWND_DESKTOP, VK_SHIFT ) & 0x8000 )
    {
        flgs |= KC_SHIFT;
        if ( kEvent.vk != 0 )
        {
            sc1   = vk2sc[ kEvent.vk ][ 12 ];
            sc2   = vk2sc[ kEvent.vk ][ 13 ];
            chpm  = vk2sc[ kEvent.vk ][ 14 ];
            chvio = vk2sc[ kEvent.vk ][ 15 ];
        }
    }

    if ( locPtr->typeProcess == 3 ) // PM
    {
        if ( ( kEvent.vk == VK_TAB ) && ( flgs == KC_SHIFT ) )  kEvent.vk = VK_BACKTAB;
        if ( kEvent.vk != 0 )  flgs |= KC_VIRTUALKEY;
        if ( sc1 != 0 )  flgs |= KC_SCANCODE;
        if ( kEvent.vk == 0 )  if ( chpm != 0 )  flgs |= KC_CHAR;
        if ( flgs & KC_CTRL )  flgs &= ~KC_CHAR;
        event.mp1 = MPFROMSH2CH( flgs , 1 , sc1 );
        if ( kEvent.vk != 0 )  event.mp2 = MPFROM2SHORT( ( sc2 << 8 ) | chpm , kEvent.vk );
        else  event.mp2 = MPFROM2SHORT( chpm , kEvent.vk );
    }
    else
    {
        kEvent.sc1 = sc1;
        kEvent.sc2 = sc2;
        if ( flgs & KC_CTRL )
        {
            if ( ( chvio >= 0x40 ) && ( chvio < 0x7f ) )
            {
                if ( chvio >= 0x60 )  chvio -= (unsigned char)0x20; // to upper
                chvio -= (unsigned char)0x40; // to ctrl char
            }
        }
        if ( sc1 != 0 )  flgs |= KC_SCANCODE;
        event.mp1 = MPFROMSH2CH( flgs , 1 , sc1 );

        USHORT v = 0;
        if ( ( kEvent.vk == VK_ALT ) ||
             ( kEvent.vk == VK_SHIFT ) ||
             ( kEvent.vk == VK_CTRL ) )
        {
            v = 0x07;
        }
        if ( ( kEvent.vk != 0 ) && ( chvio == 0xe0 ) )  v = 0xa0;
        if ( kEvent.flags & KEYEVENTF_KEYUP )  v |= 0x40;
        event.mp2 = MPFROM2SHORT( ( sc2 << 8 ) | chvio , v );
    }
}

// NOTE: Not reenterable! Access must be serialized!
extern "C" VOID EXPENTRY ER_keyb_event( HAB hab , BYTE vk , BYTE ck , ULONG flags )
{
    USHORT sc = 0;

    if ( vk > 0x2b )  return;
    if ( ck != 0 )  sc = ch2sc[ ck ];

    kEvent.ch    = ck;
    kEvent.sc1   = sc;
    kEvent.sc2   = sc;
    kEvent.vk    = vk;
    kEvent.flags = flags;

    event.mp1 = MPVOID;
    event.mp2 = MPVOID;
    event.msg = WM_VIOCHAR;

    if ( hookEnd == NULLHANDLE )
        return;

    ULONG postCnt;
    DosResetEventSem( hookEnd , &postCnt );

    if ( !WinSetHook( hab , NULLHANDLE , HK_MSGINPUT , (PFN)ER_MsgInputHook , _os2hmod ) )
    {
        return;
    }
    WinCheckInput( hab );
    if ( DosWaitEventSem( hookEnd , 1000 ) == ERROR_TIMEOUT )
    {
        WinReleaseHook( hab , NULLHANDLE , HK_MSGINPUT , (PFN)ER_MsgInputHook , _os2hmod );
    }
    event.msg = 0;
}

// ********************************************************
// ***           Input simulation section               ***
// ********************************************** END *****


// ********************************************************
// ***       Hooking display changes section            ***
// ******************************************** BEGIN *****

extern "C" void EXPENTRY ER_SetHMQ( HMQ h )
{
    hmq = h;

    SEL globalSeg , localSeg;
    DosGetInfoSeg( &globalSeg , &localSeg );
    locPtr = MAKEPLINFOSEG( localSeg );

    if ( hookEnd == NULLHANDLE )
    {
        if ( DosCreateEventSem( NULL , &hookEnd , DC_SEM_SHARED , FALSE ) != 0 )
        {
            hookEnd = NULLHANDLE;
        }
    }
}

extern "C" void EXPENTRY ER_UnSetHMQ()
{
    hmq = NULLHANDLE;
    if ( hookEnd != NULLHANDLE )
    {
        DosCloseEventSem( hookEnd );
        hookEnd = NULLHANDLE;
    }
}

inline void SendDeferredUpdateRect(HWND hWnd, SHORT x, SHORT y, SHORT x2, SHORT y2)
{
    MPARAM mp1 , mp2;

    mp1 = MPFROM2SHORT(x, y);
    mp2 = MPFROM2SHORT(x2, y2);

    if ( use_Deferral )
    {
        WinPostMsg( hWnd , VNC_DEFERRED_UPDATE , mp1 , mp2 );
    }
    else
    {
        // Send the update to PMVNC
        if ( !WinPostQueueMsg( hmq , RFB_SCREEN_UPDATE , mp1 , mp2 ) )
            hmq = NULLHANDLE;
    }
}

inline BOOL ErGetWindowRect( HWND hwnd , PRECTL r )
{
    if ( !WinQueryWindowRect(hwnd, r) ) return FALSE;
    if ( !WinMapWindowPoints(hwnd, HWND_DESKTOP, (PPOINTL)r, 2) ) return FALSE;
    return TRUE;
}

inline void SendDeferredWindowRect(HWND hWnd)
{
    RECTL wrect;

    // Get the rectangle position
    if ( WinIsWindowVisible(hWnd) && ErGetWindowRect(hWnd, &wrect) )
    {
        // Send the position
        SendDeferredUpdateRect(hWnd, (SHORT)wrect.xLeft, (SHORT)(yscr-wrect.yTop),
                                     (SHORT)wrect.xRight, (SHORT)(yscr-wrect.yBottom) );
    }
}

inline void wrkWmPaint( HWND hwnd )
{
    if ( use_GetUpdateRect )
    {
        RECTL r;
        if ( WinQueryUpdateRect( hwnd, &r) )
        {
            WinMapWindowPoints( hwnd , HWND_DESKTOP, (PPOINTL)&r, 2 );

            SendDeferredUpdateRect( hwnd ,
                            (SHORT)r.xLeft, (SHORT)(yscr-r.yTop),
                            (SHORT)r.xRight, (SHORT)(yscr-r.yBottom) );
        }
        else  SendDeferredWindowRect( hwnd );
    }
    else  SendDeferredWindowRect( hwnd );
}

#pragma argsused
extern "C" BOOL EXPENTRY ER_InputHook( HAB hab , PQMSG pQmsg , ULONG fs )
{
    ////////////////////////////////////////////////////////////////
    // HANDLE DEFERRED UPDATES

    // Is this a deferred-update message?
    if ( pQmsg->msg == VNC_DEFERRED_UPDATE )
    {
        // NOTE : NEVER use the SendDeferred- routines to send updates
        //      from here, or you'll get an infinite loop....!

        // NB : The format of DEFERRED_UPDATE matches that of UpdateRectMessage,
        //      so just send the exact same message data to PMVNC

        if ( hmq != NULLHANDLE )
        {
            if ( !WinPostQueueMsg( hmq , RFB_SCREEN_UPDATE , pQmsg->mp1 , pQmsg->mp2 ) )
                hmq = NULLHANDLE;
        }

        return TRUE;
    }


    if ( hmq == NULLHANDLE )  return FALSE;

    switch ( pQmsg->msg )
    {
        case WM_CHAR:
        case WM_VIOCHAR:
        case WM_BUTTON1UP:
        case WM_BUTTON2UP:
        case WM_BUTTON3UP:
        case WM_REALIZEPALETTE:
        case WM_USER:     // handle xCenter pulse widget update
            SendDeferredWindowRect( pQmsg->hwnd );
            break;

        case WM_TIMER:     // Note: may cause high CPU load
            if ( use_Timer )  SendDeferredWindowRect( pQmsg->hwnd );
            break;

        case WM_HSCROLL:
        case WM_VSCROLL:
            if ((SHORT2FROMMP(pQmsg->mp2) == SB_SLIDERPOSITION) ||
                (SHORT2FROMMP(pQmsg->mp2) == SB_ENDSCROLL))
            {
                SendDeferredWindowRect( pQmsg->hwnd );
            }
            break;

        case WM_PAINT:
            wrkWmPaint( pQmsg->hwnd );
            break;

        case WM_MOUSEMOVE:
            // Inform PMVNC that the mouse has moved and pass it the current cursor handle
            {
                HPOINTER new_cursor = WinQueryPointer( HWND_DESKTOP );
                if ( new_cursor != old_cursor )
                {
                    if ( !WinPostQueueMsg( hmq , RFB_MOUSE_UPDATE , (MPARAM)new_cursor , MPVOID ) )
                        hmq = NULLHANDLE;

                    old_cursor = new_cursor;
                }
            }
            break;
    }

    return FALSE;
}

#pragma argsused
extern "C" VOID EXPENTRY ER_SendMsgHook( HAB hab , PSMHSTRUCT psmh , BOOL fInterTask )
{
    if ( hmq == NULLHANDLE )  return;

    switch ( psmh->msg )
    {
        case EM_SETSEL:
        case BM_SETCHECK:
        case WM_ENABLE:
        case WM_REALIZEPALETTE:
        case WM_SETWINDOWPARAMS:
        case WM_WINDOWPOSCHANGED:
            SendDeferredWindowRect( psmh->hwnd );
            break;

        case WM_PAINT:
            wrkWmPaint( psmh->hwnd );
            break;

        case WM_MENUEND:
            {
                HWND h = (HWND)psmh->mp2;
                if ( h != NULLHANDLE )
                {
                    SendDeferredWindowRect( h );
                }
            }
            break;
    }
}

// ********************************************************
// ***       Hooking display changes section            ***
// ********************************************** END *****

// ********************************************************
// ***            Initialization section                ***
// ******************************************** BEGIN *****

extern "C" VOID EXPENTRY ER_SetHooksProps( ULONG scupd , ULONG mouupd , ULONG defupd ,
                                           BOOL useTimer , BOOL useDeferred )
{
    initStaticVars();

    RFB_SCREEN_UPDATE   = scupd;
    RFB_MOUSE_UPDATE    = mouupd;
    VNC_DEFERRED_UPDATE = defupd;

    use_Timer         = useTimer;
    use_Deferral      = useDeferred;
}

// ********************************************************
// ***            Initialization section                ***
// ********************************************** END *****


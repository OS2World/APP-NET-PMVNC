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


// VSocket.cpp

// The VSocket class provides a platform-independent socket abstraction
// with the simple functionality required for an RFB server.

class VSocket;

////////////////////////////////////////////////////////
// System includes

#include "stdhdrs.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>

////////////////////////////////////////////////////////
// Custom includes

#include "VTypes.h"

////////////////////////////////////////////////////////
// Socket implementation

#include "VSocket.h"

#define SD_BOTH 2
#define INADDR_LOOPBACK  (u_long)0x7f000001
#define SO_SNDTIMEO 0x1005      /* send timeout */
#define SO_RCVTIMEO 0x1006      /* receive timeout */

////////////////////////////

VSocket::VSocket()
{
  // Clear out the internal socket fields
  sock = -1;
}

////////////////////////////

VSocket::~VSocket()
{
  // Close the socket
  Close();
}

////////////////////////////

VBool VSocket::Create()
{
  const int one = 1;

  // Check that the old socket was closed
  if (sock >= 0)
    Close();

  // Create the socket
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
      vnclog.Print(LL_SOCKERR, VNCLOG("socket error %d\n"), sock_errno());
      return VFalse;
    }

  // Set the socket options:
  if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one)))
    {
      return VFalse;
    }
  if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(one)))
    {
      return VFalse;
    }

  return VTrue;
}

////////////////////////////

VBool VSocket::Close()
{
  if (sock >= 0)
    {
      vnclog.Print(LL_SOCKINFO, VNCLOG("closing socket\n"));

      shutdown(sock, SD_BOTH);
      soclose(sock);
      sock = -1;
    }
  return VTrue;
}

////////////////////////////

VBool VSocket::Shutdown()
{
  if (sock >= 0)
    {
      vnclog.Print(LL_SOCKINFO, VNCLOG("shutdown socket\n"));

      so_cancel(sock);
      shutdown(sock, SD_BOTH);
    }
  return VTrue;
}

////////////////////////////

VBool VSocket::Bind(const VCard port, const VBool localOnly)
{
  struct sockaddr_in addr;
  memset( &addr , 0 , sizeof( addr ) );

  // Check that the socket is open!
  if (sock < 0)
    return VFalse;

  // Set up the address to bind the socket to
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (localOnly)
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  else
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

  // And do the binding
  if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
      return VFalse;

  return VTrue;
}

////////////////////////////

VBool VSocket::Connect(const VString address, const VCard port)
{
  // Check the socket
  if (sock < 0)
    return VFalse;

  // Create an address structure and clear it
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));

  // Fill in the address if possible
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(address);

  // Was the string a valid IP address?
  if (addr.sin_addr.s_addr == -1)
    {
      // No, so get the actual IP address of the host name specified
      struct hostent *pHost;
      pHost = gethostbyname(address);
      if (pHost != NULL)
      {
          if (pHost->h_addr == NULL)
              return VFalse;
          addr.sin_addr.s_addr = ((struct in_addr *)pHost->h_addr)->s_addr;
      }
      else
        return VFalse;
    }

  // Set the port number in the correct format
  addr.sin_port = htons(port);

  // Actually connect the socket
  if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0)
    return VTrue;

  return VFalse;
}

////////////////////////////

VBool VSocket::Listen()
{
  // Check socket
  if (sock < 0)
    return VFalse;

    // Set it to listen
  if (listen(sock, 5) < 0)
    return VFalse;

  return VTrue;
}

////////////////////////////

VSocket *VSocket::Accept()
{
  const int one = 1;

  int new_socket_id;
  VSocket * new_socket;

  // Check this socket
  if (sock < 0)
    return NULL;

  // Accept an incoming connection
  if ((new_socket_id = accept(sock, NULL, 0)) < 0)
    return NULL;

  // Create a new VSocket and return it
  new_socket = new VSocket;
  if (new_socket != NULL)
    {
      new_socket->sock = new_socket_id;
    }
  else
    {
      shutdown(new_socket_id, SD_BOTH);
      soclose(new_socket_id);
    }

  // Attempt to set the new socket's options
  setsockopt(new_socket->sock, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(one));

  return new_socket;
}

////////////////////////////

VString VSocket::GetPeerName()
{
    struct sockaddr_in  sockinfo;
    struct in_addr      address;
    int                 sockinfosize = sizeof(sockinfo);
    VString             name;

    // Get the peer address for the client socket
    getpeername(sock, (struct sockaddr *)&sockinfo, &sockinfosize);
    memcpy(&address, &sockinfo.sin_addr, sizeof(address));

    name = inet_ntoa(address);
    if (name == NULL)
        return "<unavailable>";
    else
        return name;
}

////////////////////////////

VString VSocket::GetSockName()
{
    struct sockaddr_in  sockinfo;
    struct in_addr      address;
    int                 sockinfosize = sizeof(sockinfo);
    VString             name;

    // Get the peer address for the client socket
    getsockname(sock, (struct sockaddr *)&sockinfo, &sockinfosize);
    memcpy(&address, &sockinfo.sin_addr, sizeof(address));

    name = inet_ntoa(address);
    if (name == NULL)
        return "<unavailable>";
    else
        return name;
}

////////////////////////////

VCard32 VSocket::Resolve(const VString address)
{
  VCard32 addr;

  // Try converting the address as IP
  addr = inet_addr(address);

  // Was it a valid IP address?
  if (addr == 0xffffffff)
    {
      // No, so get the actual IP address of the host name specified
      struct hostent *pHost;
      pHost = gethostbyname(address);
      if (pHost != NULL)
      {
          if (pHost->h_addr == NULL)
              return 0;
          addr = ((struct in_addr *)pHost->h_addr)->s_addr;
      }
      else
          return 0;
    }

  // Return the resolved IP address as an integer
  return addr;
}

////////////////////////////

VBool VSocket::SetTimeout(VCard32 millisecs)
{
    struct timeval timeout;
    timeout.tv_sec=millisecs/1000;
    timeout.tv_usec=(millisecs%1000)*1000;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout)) == -1)
    {
        return VFalse;
    }
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout)) == -1)
    {
        return VFalse;
    }
    return VTrue;
}

////////////////////////////
#define MAXSENDSIZE 32767

inline int safesend(int socket, const void *buf, int len, int flags)
{
    if (len > MAXSENDSIZE) len = MAXSENDSIZE; 
    return send(socket, buf, len, flags);
}

VInt VSocket::Send(const char *buff, const VCard bufflen)
{
    int blen = bufflen;

    while (blen > 0)
    {
        int n = safesend(sock, buff, blen, 0);

        if (n <= 0)  return n;

        blen -= n;
        buff += n;
    }

    return bufflen;
}

////////////////////////////

VBool VSocket::SendExact(const char *buff, const VCard bufflen)
{
  return Send(buff, bufflen) == (VInt)bufflen;
}

////////////////////////////

VInt VSocket::Read(char *buff, const VCard bufflen)
{
  return recv(sock, buff, bufflen, 0);
}

////////////////////////////

VBool VSocket::ReadExact(char *buff, const VCard bufflen)
{
    int n;
    VCard currlen = bufflen;

    while (currlen > 0)
    {
        // Try to read some data in
        n = Read(buff, currlen);

        if (n > 0)
        {
            // Adjust the buffer position and size
            buff += n;
            currlen -= n;
        } else if (n == 0) {
            vnclog.Print(LL_SOCKERR, VNCLOG("zero bytes read\n"));

            return VFalse;
        } else {
            if (sock_errno() != EWOULDBLOCK)
            {
                vnclog.Print(LL_SOCKERR, VNCLOG("socket error %d\n"), sock_errno());
                return VFalse;
            }
        }
    }

    return VTrue;
}


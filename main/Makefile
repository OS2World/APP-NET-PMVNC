.autodepend

OBJDIR = \Projects\PMVNC\main\obj
EXEDIR = \Projects\PMVNC\main

.path.obj = $(OBJDIR)
.path.o   = $(OBJDIR)
.path.res = $(OBJDIR)
.path.exe = $(EXEDIR)

CC=gcc
AR=ar rc
RANLIB=ranlib

CFLAGS=-O3 -s -static -Zmtd -Zomf -I.. -I../omnithread -I../omnithread/omnithread

OBJS = pmvnc.o region.o \
  rfbRegion_x11.o rfbUpdateTracker.o stdhdrs.o \
  translate.o vncbuffer.o vncclient.o vncdesktop.o \
  vncencoder.o vncencodecorre.o vncencodehext.o vncencoderre.o \
  vnckeymap.o vnclog.o vncserver.o vncsockconnect.o vsocket.o \
  vnchttpconnect.o vncinsthandler.o vncauth.o d3des.o vncencodezrle.o \
  vncmenu.o vncproperties.o vncConnDialog.o vncAcceptDialog.o linklab.o

all: pmvnc.exe pmvnchk.dll vncpass.exe

res\pmvnc.res: res\pmvnc.rc res.h
    rc -r res\pmvnc.rc

pmvnc.exe: $(OBJS) res\pmvnc.res
    $(CC) $(CFLAGS) @&&|
$(OBJDIR)\pmvnc.o
$(OBJDIR)\rfbRegion_x11.o
$(OBJDIR)\rfbUpdateTracker.o
$(OBJDIR)\stdhdrs.o
$(OBJDIR)\translate.o
$(OBJDIR)\vncbuffer.o
$(OBJDIR)\vncclient.o
$(OBJDIR)\vncdesktop.o
$(OBJDIR)\vncencoder.o
$(OBJDIR)\vncencodecorre.o
$(OBJDIR)\vncencodehext.o
$(OBJDIR)\vncencoderre.o
$(OBJDIR)\vnckeymap.o
$(OBJDIR)\vnclog.o
$(OBJDIR)\vncserver.o
$(OBJDIR)\vncsockconnect.o
$(OBJDIR)\vsocket.o
$(OBJDIR)\vnchttpconnect.o
$(OBJDIR)\region.o
$(OBJDIR)\vncinsthandler.o
$(OBJDIR)\vncauth.o
$(OBJDIR)\d3des.o
$(OBJDIR)\vncencodezrle.o
$(OBJDIR)\vncmenu.o
$(OBJDIR)\vncproperties.o
$(OBJDIR)\vncConnDialog.o
$(OBJDIR)\vncAcceptDialog.o
$(OBJDIR)\linklab.o
pmvnc.def
res/pmvnc.res
-l..\omnithread\omnithread.lib
-l..\rdr\librdr.lib
-l..\zlib\libz.lib
-lstdc++
-l../../../gcc/lib/tcpipv4/libsocket.lib
| >NUL


.cpp.o:
    $(CC) $(CFLAGS) -c -o$@ $<

.c.o:
    $(CC) $(CFLAGS) -c -o$@ $<

pmvnc.o:             pmvnc.cpp stdhdrs.h VSocket.h PMVNC.h vncServer.h vncInstHandler.h vncMenu.h res.h
Region.o:            xregion/Region.c
rfbregion_x11.o:     rfbregion_x11.cpp stdhdrs.h
rfbupdatetracker.o:  rfbupdatetracker.cpp stdhdrs.h
stdhdrs.o:           stdhdrs.cpp stdhdrs.h
translate.o:         translate.cpp tableinitcmtemplate.cpp tableinittctemplate.cpp tabletranstemplate.cpp stdhdrs.h vncdesktop.h
vncauth.o:           vncauth.c
d3des.o:             d3des.c
vncbuffer.o:         vncbuffer.cpp stdhdrs.h vncdesktop.h
vncclient.o:         vncclient.cpp stdhdrs.h vncdesktop.h vncServer.h vncclient.h vncAcceptDialog.h
vncdesktop.o:        vncdesktop.cpp stdhdrs.h vncdesktop.h vncServer.h
vncencodecorre.o:    vncencodecorre.cpp
vncencodehext.o:     vncencodehext.cpp
vncencoder.o:        vncencoder.cpp
vncencoderre.o:      vncencoderre.cpp
vncencodezrle.o:     vncencodezrle.cpp
vnchttpconnect.o:    vnchttpconnect.cpp stdhdrs.h res.h vncServer.h
vncinsthandler.o:    vncinsthandler.cpp stdhdrs.h
vnckeymap.o:         vnckeymap.cpp stdhdrs.h vnckeymap.h
vnclog.o:            vnclog.cpp stdhdrs.h
vncserver.o:         vncserver.cpp stdhdrs.h vncServer.h vncclient.h
vncsockconnect.o:    vncsockconnect.cpp stdhdrs.h vncServer.h
vsocket.o:           vsocket.cpp stdhdrs.h
vncmenu.o:           vncmenu.cpp stdhdrs.h vncmenu.h res.h
vncproperties.o:     vncproperties.cpp stdhdrs.h vncproperties.h res.h vncServer.h
vncConnDialog.o:     vncConnDialog.cpp stdhdrs.h vncConnDialog.h res.h vncServer.h
vncAcceptDialog.o:   vncAcceptDialog.cpp stdhdrs.h vncAcceptDialog.h res.h
linklab.o:           linklab.c

#PMVNCHK.DLL: PMVNCHK.C
#  gcc -s -O3 -Zdll -Zomf PMVNCHK.C PMVNCHK.DEF -lstdc++

PMVNCHK.DLL: PMVNCHK.CPP
  BCC -c -x- -RT- -O2 -w+ -sd PMVNCHK.CPP
  TLINK /Oc /x /c /Tod /w-stk BCCDLL0 PMVNCHK,pmvnchk.dll,,OS2,PMVNCHK
  if exist PMVNCHK.OBJ DEL PMVNCHK.OBJ

  
vncpass.exe: vncpass.cpp vncPasswd.h vncauth.c vncAuth.h d3des.c
  bcc -x- -RT- -O2 -sm- -w- -DNOLOG vncpass.cpp vncauth.c d3des.c
  if exist vncpass.obj DEL vncpass.obj
  if exist vncauth.obj DEL vncauth.obj
  if exist d3des.obj DEL d3des.obj

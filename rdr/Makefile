CC=gcc
AR=ar rc
RANLIB=ranlib

CFLAGS=-O3 -s -Zcrtdll -Zmtd -DTCPV40HDRS
#CFLAGS=-O3 -s -Zcrtdll -Zmtd

LIBS=librdr.a


SRCS = FdInStream.cxx FdOutStream.cxx InStream.cxx NullOutStream.cxx \
       ZlibInStream.cxx ZlibOutStream.cxx

OBJS = FdInStream.o FdOutStream.o InStream.o NullOutStream.o \
       ZlibInStream.o ZlibOutStream.o

DIR_CPPFLAGS = -I.. -I../zlib

$(LIBS): $(OBJS)
	rm -f $(LIBS)
	$(AR) $(LIBS) $(OBJS)
	$(RANLIB) $(LIBS)
	emxomf -t $(LIBS)


.cxx.o:
	$(CC) $(CFLAGS) $(DIR_CPPFLAGS) -c {$< }
	
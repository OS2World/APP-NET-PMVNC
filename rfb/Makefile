CC=gcc
AR=ar rc
RANLIB=ranlib

CFLAGS=-O3 -s -Zcrtdll -Zmtd

SRCS = d3des.c vncauth.c

OBJS = d3des.o vncauth.o

DIR_CPPFLAGS = -I..

library = librfb.a

$(library): $(OBJS)
	rm -f $(library)
	$(AR) $(library) $(OBJS)
	$(RANLIB) $(library)

.c.o:
	$(CC) $(CFLAGS) $(DIR_CPPFLAGS) -c {$< }

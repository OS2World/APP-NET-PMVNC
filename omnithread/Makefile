CC=gcc
AR=ar rc
RANLIB=ranlib

CFLAGS=-s -O3 -Zcrtdll -Zmtd

LIBS=omnithread.a

SRCS = omnithread/os2.cpp

OBJS = os2.o 

DIR_CPPFLAGS = -I. -I./omnithread

all: $(LIBS) test.exe

$(LIBS): $(OBJS)
	rm -f $(LIBS)
	$(AR) $(LIBS) $(OBJS)
	emxomf -t $(LIBS)

test.exe: test.cpp
	$(CC) $(CFLAGS) $(DIR_CPPFLAGS) test.cpp -lomnithread -lstdc++

$(OBJS): $(SRCS)
	$(CC) $(CFLAGS) $(DIR_CPPFLAGS) -c $(SRCS)

	
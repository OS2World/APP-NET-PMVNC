CC=gcc
AR=ar rc
RANLIB=ranlib

CFLAGS=-O3 -s -Zcrtdll -Zmtd

LIBS=libz.a

OBJS = adler32.o compress.o crc32.o gzio.o uncompr.o deflate.o \
       trees.o zutil.o inflate.o infblock.o inftrees.o infcodes.o \
       infutil.o inffast.o

TEST_OBJS = example.o minigzip.o

all: $(LIBS) sub

sub: example.exe minigzip.exe

$(LIBS): $(OBJS)
	rm -f $(LIBS)
	$(AR) $@ $(OBJS)
	$(RANLIB) $(LIBS)
	emxomf -t $(LIBS)

example.exe: example.c $(LIBS)
    $(CC) $(CFLAGS) example.c $(LIBS)

minigzip.exe: minigzip.c $(LIBS)
    $(CC) $(CFLAGS) minigzip.c $(LIBS)

.C.o:
	$(CC) $(CFLAGS) -c {$< }


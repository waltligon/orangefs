
PVFSDIR=../../../
INCDIR=-I. -I$(PVFSDIR)/include
LIBDIR=

#CFLAGS=-g -O0 $(INCDIR) $(LIBDIR) -Wall -Wstrict-prototypes -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE
CFLAGS=-g -O0 $(INCDIR) $(LIBDIR) -Wall -Wstrict-prototypes -D_LARGEFILE64_SOURCE

OBJS=iocommon.o openfile-util.o posix-pvfs.o posix.o stdio.o request.o socket.o shim.o

INCS=usrint.h iocommon.h posix-pvfs.h openfile-util.h posix-ops.h stdio-ops.h

all: $(OBJS)

install::
	cp $(OBJS) /home/walt/Workspace/pvfs-test/test

clean::
	rm *.o

iocommon.o:$(INCS)
openfile-util.o:$(INCS)
posix-pvfs.o:$(INCS)
posix.o:$(INCS)
stdio.o:$(INCS)
request.o:$(INCS)
socket.o:$(INCS)

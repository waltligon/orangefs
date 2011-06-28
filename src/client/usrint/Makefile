
PVFSDIR=../../../
INCDIR=-I. -I$(PVFSDIR)/include
LIBDIR=

CFLAGS=-g -O0 $(INCDIR) $(LIBDIR)

OBJS=iocommon.o openfile-util.o posix-pvfs.o posix.o stdio.o request.o

INCS=usrint.h iocommon.h posix-pvfs.h openfile-util.h posix-ops.h

all: $(OBJS)

clean::
	rm *.o

iocommon.o:$(INCS)
openfile-util.o:$(INCS)
posix-pvfs.o:$(INCS)
posix.o:$(INCS)
stdio.o:$(INCS)
request.o:$(INCS)

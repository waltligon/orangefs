
PVFSDIR=../../../
INCDIR=-I. -I$(PVFSDIR)/include
LIBDIR=

CFLAGS=$(INCDIR) $(LIBDIR)

OBJS=iocommon.o openfile-util.o posix-pvfs.o posix.o stdio.o

INCS=usrint.h iocommon.h stdio-pvfs.h posix-pvfs.h openfile-util.h

all: $(OBJS)

clean: 
	rm *.o

iocommon.o:$(INCS)
openfile-util.o:$(INCS)
posix-pvfs.o:$(INCS)
posix.o:$(INCS)
stdio.o:$(INCS)

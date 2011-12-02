# Makefile for client tests
# Copyright (C) 2011 Omnibond, LLC
# Use with Linux kernel module to compare
# results to the Windows client test

CFLAGS=-g
LDFLAGS=-lpthread

objects=client-test.o create.o file-ops.o info.o open.o test-io.o test-support.o timer.o

client-test : $(objects)
	cc -o client-test $(objects) $(CFLAGS) $(LDFLAGS)

client-test.o: client-test.c test-support.h test-list.h
	cc -o $@ -c $< $(CFLAGS)
create.o: create.c create.h
	cc -o $@ -c $< $(CFLAGS)
file-ops.o: file-ops.c file-ops.h test-support.h
	cc -o $@ -c $< $(CFLAGS)
info.o: info.c info.h test-support.h
	cc -o $@ -c $< $(CFLAGS)
open.o: open.c open.h test-support.h
	cc -o $@ -c $< $(CFLAGS)
test-io.o: test-io.c test-io.h test-support.h
	cc -o $@ -c $< $(CFLAGS)
test-support.o: test-support.c test-support.h
	cc -o $@ -c $< $(CFLAGS)
timer.o: timer.c timer.h
	cc -o $@ -c $< $(CFLAGS)

# override implicit rule
#%.o: %.c

#%.o: %.c %.h test-support.h test-list.h
#	cc -o $@ -c $< $(CFLAGS)

clean:
	rm -f client-test *.o


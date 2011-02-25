# Makefile for client tests
# Copyright (C) 2011 Omnibond, LLC
# Use with Linux kernel module to compare
# results to the Windows client test

CFLAGS=-g
LDFLAGS=-lpthread

objects:=$(patsubst %.c,%.o,$(wildcard *.c))

client-test : $(objects)
	cc -o client-test $(objects) $(CFLAGS) $(LDFLAGS)

# override implicit rule
%.o: %.c

%.o: %.c %.h test-support.h test-list.h
	cc -o $@ -c $< $(CFLAGS)

clean:
	rm -f client-test *.o


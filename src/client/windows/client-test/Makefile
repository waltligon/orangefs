# Makefile for client tests
# Copyright (C) 2011 Omnibond, LLC
# Use with Linux kernel module to compare
# results to the Windows client test

LDFLAGS=-lpthread

objects=$(patsubst %.c,%.o,$(wildcard *.c))

client-test : $(objects)
	cc -o client-test $(objects) $(CFLAGS) $(LDFLAGS)

%.o : %.h test-support.h

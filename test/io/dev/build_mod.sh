#!/bin/tcsh

set KERN_VER = `uname -r`

gcc -I/lib/modules/${KERN_VER}/build/include -O2 -Wall \
-I../../../src/io/dev/ \
-Wstrict-prototypes -DMODULE -D__KERNEL__ -DLINUX  \
-pipe -fomit-frame-pointer -fno-strict-aliasing \
-mpreferred-stack-boundary=2 -c dev-mod.c

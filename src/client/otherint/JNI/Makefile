#------------------------------------------------------------------------------
# (C) 2001 Clemson University and The University of Chicago.
# See COPYING in top-level directory.

# Purpose: create a shared library that can be loaded by a Java class exposing 
# PVFS2 Library Calls to Java Developers.
#------------------------------------------------------------------------------

#    Setup - adjust these values accordingly...
#------------------------------------------------------------------------------
# Path to our PVFS prefix
PVFS2_TOP_PREFIX=$(HOME)/trunk
# Path to usrint folder in src tree
USRINT_DIR=$(PVFS2_TOP_PREFIX)/ofs/src/client/usrint
# JAVA: Found by running: 
#    readlink -f /usr/bin/javac | sed "s:bin/javac::"
JAVA_HOME=/usr/lib/jvm/java-7-openjdk-amd64/
#------------------------------------------------------------------------------

#Link
libPVFS2STDIOJNI.so : PVFS2STDIOJNI.class PVFS2STDIOJNI.h libPVFS2STDIOJNI.o
	gcc -shared -g \
	  -o $@ \
	  libPVFS2STDIOJNI.o \
	  -L$(PVFS2_TOP_PREFIX)/lib \
	  -lofs \
	  -lpvfs2 \
	  -lpthread \
	  -ldl \
	  -lcrypto

#Generate Java classes
PVFS2STDIOJNI.class : PVFS2STDIOJNI.java libPVFS2STDIOJNI.c
	javac $<

#Generate JNI Compatible C Header for JNI Shim
PVFS2STDIOJNI.h : PVFS2STDIOJNI.java libPVFS2STDIOJNI.c
	javah PVFS2STDIOJNI

#Preprocess, Compile, and Assemble in one step
libPVFS2STDIOJNI.o : libPVFS2STDIOJNI.c PVFS2STDIOJNI.java PVFS2STDIOJNI.h \
                     PVFS2STDIOJNI.class 
	cc -fPIC -c \
	  -I$(PVFS2_TOP_PREFIX)/include \
	  -I$(USRINT_DIR) \
	  -I$(JAVA_HOME)include \
	  -I$(JAVA_HOME)include/linux \
	  $<

clean :
	rm -f *.o *.so *.class *.h

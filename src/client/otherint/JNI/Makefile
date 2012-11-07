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
JAVA_HOME=/usr/lib/jvm/java-6-openjdk-amd64/

#For use with package
ORGDIR = org/orangefs/usrint
ORGDOT = org.orangefs.usrint
ORGUNDER = org_orangefs_usrint
#JNI Directory
JNIDIR=/home/denton/trunk/ofs/src/client/otherint/JNI
#------------------------------------------------------------------------------
ofs_jni.jar : libPVFS2POSIXJNI.so libPVFS2STDIOJNI.so \
		$(ORGDIR)/OrangeFileSystemInputStream.class
	jar cf ofs_jni.jar \
	  $(ORGDIR)/*.class

install : ofs_jni.jar
	cp *.so $(PVFS2_TOP_PREFIX)/lib
	cp ofs_jni.jar $(PVFS2_TOP_PREFIX)/lib

#Other Java Classes
IOSTREAMS = $(ORGDIR)/OrangeFileSystemInputStream.java \
	$(ORGDIR)/OrangeFileSystemOutputStream.java
$(ORGDIR)/OrangeFileSystemInputStream.class : $(IOSTREAMS)
	javac $(IOSTREAMS)

#PVFS2POSIXJNI
#==============================================================================
#link
libPVFS2POSIXJNI.so : PVFS2POSIXJNI.class $(ORGUNDER)_PVFS2POSIXJNI.h libPVFS2POSIXJNI.o
	gcc -shared -g \
      -o $@ \
      libPVFS2POSIXJNI.o \
      -L$(PVFS2_TOP_PREFIX)/lib \
      -lofs \
      -lpvfs2 \
      -lpthread \
      -ldl \
      -lcrypto

#Generate Java classes
POSIX_JAVA= $(ORGDIR)/PVFS2POSIXJNI.java \
	$(ORGDIR)/PVFS2POSIXJNIFlags.java $(ORGDIR)/Orange.java \
	$(ORGDIR)/Fsid.java $(ORGDIR)/Stat.java $(ORGDIR)/Stat64.java \
	$(ORGDIR)/Statfs.java $(ORGDIR)/Statfs64.java $(ORGDIR)/Statvfs.java \
	$(ORGDIR)/Timeval.java $(ORGDIR)/Utimbuf.java

PVFS2POSIXJNI.class : $(POSIX_JAVA) libPVFS2POSIXJNI.c
	javac $<

#Generate JNI Compatible C Header for JNI Shim
$(ORGUNDER)_PVFS2POSIXJNI.h : $(ORGDIR)/PVFS2POSIXJNI.java libPVFS2POSIXJNI.c
	javah $(ORGDOT).PVFS2POSIXJNI

#Preprocess, Compile, and Assemble in one step
libPVFS2POSIXJNI.o : libPVFS2POSIXJNI.c $(ORGUNDER)_PVFS2POSIXJNI.h
	cc -fPIC -c \
      -I$(PVFS2_TOP_PREFIX)/include \
      -I$(USRINT_DIR) \
      -I$(JAVA_HOME)include \
      -I$(JAVA_HOME)include/linux \
	  -I$(JNIDIR) \
      libPVFS2POSIXJNI.c

#PVFS2STDIOJNI
#==============================================================================

#Link PVFS2STDIOJNI
libPVFS2STDIOJNI.so : PVFS2STDIOJNI.class $(ORGUNDER)_PVFS2STDIOJNI.h libPVFS2STDIOJNI.o
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
STDIO_JAVA=$(ORGDIR)/PVFS2STDIOJNI.java $(ORGDIR)/PVFS2STDIOJNIFlags.java \
	$(ORGDIR)/Dirent.java $(ORGDIR)/Dirent64.java
PVFS2STDIOJNI.class : $(STDIO_JAVA) libPVFS2STDIOJNI.c
	javac $<


#Generate JNI Compatible C Header for JNI Shim
$(ORGUNDER)_PVFS2STDIOJNI.h : $(ORGDIR)/PVFS2STDIOJNI.java libPVFS2STDIOJNI.c
	javah $(ORGDOT).PVFS2STDIOJNI


#Preprocess, Compile, and Assemble in one step
libPVFS2STDIOJNI.o : libPVFS2STDIOJNI.c $(ORGUNDER)_PVFS2STDIOJNI.h
	cc -fPIC -c \
	  -I$(PVFS2_TOP_PREFIX)/include \
	  -I$(USRINT_DIR) \
	  -I$(JAVA_HOME)include \
	  -I$(JAVA_HOME)include/linux \
	  libPVFS2STDIOJNI.c
#==============================================================================
clean :
	rm -f *.o org_*.h *.so *.jar org/orangefs/usrint/*.class \
		$(PVFS2_TOP_PREFIX)/lib/libPVFS2POSIXJNI.so \
		$(PVFS2_TOP_PREFIX)/lib/libPVFS2STDIOJNI.so 


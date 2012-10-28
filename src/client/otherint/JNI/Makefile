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

#For use with package
ORGDIR = org/orangefs/usrint
ORGDOT = org.orangefs.usrint
ORGUNDER = org_orangefs_usrint
#------------------------------------------------------------------------------

ofs_jni.jar : libPVFS2POSIXJNI.so libPVFS2STDIOJNI.so 
	jar cf ofs_jni.jar \
	  $(ORGDIR)/*.class

install : ofs_jni.jar
	cp *.so $(PVFS2_TOP_PREFIX)/lib
	cp ofs_jni.jar $(PVFS2_TOP_PREFIX)/lib

#PVFS2POSIXJNI
#==============================================================================
#link
libPVFS2POSIXJNI.so : PVFS2POSIXJNI.class $(ORGUNDER)_PVFS2POSIXJNI.h libPVFS2POSIXJNI.o
	gcc -shared -g \
      -o $@ \
      libPVFS2POSIXJNI.o \
      flag_conv_common.o \
      posix_flag_conv.o \
      -L$(PVFS2_TOP_PREFIX)/lib \
      -lofs \
      -lpvfs2 \
      -lpthread \
      -ldl \
      -lcrypto

#Generate Java classes
PVFS2POSIXJNI.class : $(ORGDIR)/PVFS2POSIXJNI.java libPVFS2POSIXJNI.c
	javac $<

#Generate JNI Compatible C Header for JNI Shim
$(ORGUNDER)_PVFS2POSIXJNI.h : $(ORGDIR)/PVFS2POSIXJNI.java libPVFS2POSIXJNI.c
	javah $(ORGDOT).PVFS2POSIXJNI

#Preprocess, Compile, and Assemble in one step
libPVFS2POSIXJNI.o : libPVFS2POSIXJNI.c $(ORGDIR)/PVFS2POSIXJNI.java $(ORGUNDER)_PVFS2POSIXJNI.h \
                     PVFS2POSIXJNI.class flags/flag_conv_common.c flags/posix_flag_conv.c
	cc -fPIC -c \
      -I$(PVFS2_TOP_PREFIX)/include \
      -I$(USRINT_DIR) \
      -I$(JAVA_HOME)include \
      -I$(JAVA_HOME)include/linux \
      libPVFS2POSIXJNI.c \
      flags/flag_conv_common.c \
      flags/posix_flag_conv.c 

#PVFS2STDIOJNI
#==============================================================================

#Link PVFS2STDIOJNI
libPVFS2STDIOJNI.so : PVFS2STDIOJNI.class $(ORGUNDER)_PVFS2STDIOJNI.h libPVFS2STDIOJNI.o
	gcc -shared -g \
	  -o $@ \
	  libPVFS2STDIOJNI.o \
	  flag_conv_common.o \
	  stdio_flag_conv.o \
	  -L$(PVFS2_TOP_PREFIX)/lib \
	  -lofs \
	  -lpvfs2 \
	  -lpthread \
	  -ldl \
	  -lcrypto

#Generate Java classes
PVFS2STDIOJNI.class : $(ORGDIR)/PVFS2STDIOJNI.java libPVFS2STDIOJNI.c
	javac $<

#Generate JNI Compatible C Header for JNI Shim
$(ORGUNDER)_PVFS2STDIOJNI.h : $(ORGDIR)/PVFS2STDIOJNI.java libPVFS2STDIOJNI.c
	javah $(ORGDOT).PVFS2STDIOJNI

#Preprocess, Compile, and Assemble in one step
libPVFS2STDIOJNI.o : libPVFS2STDIOJNI.c $(ORGDIR)/PVFS2STDIOJNI.java $(ORGUNDER)_PVFS2STDIOJNI.h \
                     PVFS2STDIOJNI.class flags/flag_conv_common.c flags/stdio_flag_conv.c 
	cc -fPIC -c \
	  -I$(PVFS2_TOP_PREFIX)/include \
	  -I$(USRINT_DIR) \
	  -I$(JAVA_HOME)include \
	  -I$(JAVA_HOME)include/linux \
	  libPVFS2STDIOJNI.c \
	  flags/flag_conv_common.c \
	  flags/stdio_flag_conv.c 
#==============================================================================
clean :
	rm -f *.o org_*.h *.so *.jar org/orangefs/usrint/*.class \
		$(PVFS2_TOP_PREFIX)/lib/libPVFS2POSIXJNI.so \
		$(PVFS2_TOP_PREFIX)/lib/libPVFS2STDIOJNI.so 


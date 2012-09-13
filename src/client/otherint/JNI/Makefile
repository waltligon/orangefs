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

#Build both libraries and move to OrangeFS lib directory
libs : libPVFS2STDIOJNI.so libPVFS2JNI.so
	touch libs

ofs_jni.jar : libs
	jar cf ofs_jni.jar \
	  $(ORGDIR)/*.class

install : ofs_jni.jar
	cp *.so $(PVFS2_TOP_PREFIX)/lib
	cp ofs_jni.jar $(PVFS2_TOP_PREFIX)/lib

#Link
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
PVFS2STDIOJNI.class : $(ORGDIR)/PVFS2STDIOJNI.java libPVFS2STDIOJNI.c
	javac $<

#Generate JNI Compatible C Header for JNI Shim
$(ORGUNDER)_PVFS2STDIOJNI.h : $(ORGDIR)/PVFS2STDIOJNI.java libPVFS2STDIOJNI.c
	javah $(ORGDOT).PVFS2STDIOJNI

#Preprocess, Compile, and Assemble in one step
libPVFS2STDIOJNI.o : libPVFS2STDIOJNI.c $(ORGDIR)/PVFS2STDIOJNI.java $(ORGUNDER)_PVFS2STDIOJNI.h \
                     PVFS2STDIOJNI.class 
	cc -fPIC -c \
	  -I$(PVFS2_TOP_PREFIX)/include \
	  -I$(USRINT_DIR) \
	  -I$(JAVA_HOME)include \
	  -I$(JAVA_HOME)include/linux \
	  $<

#Other JNI Lib
#==============================================================================
#Link
libPVFS2JNI.so : PVFS2JNI.class $(ORGUNDER)_PVFS2JNI.h libPVFS2JNI.o
	gcc -shared -g \
      -o $@ \
      libPVFS2JNI.o \
      -L$(PVFS2_TOP_PREFIX)/lib \
      -lofs \
      -lpvfs2 \
      -lpthread \
      -ldl \
      -lcrypto

#Generate Java classes
PVFS2JNI.class : $(ORGDIR)/PVFS2JNI.java libPVFS2JNI.c
	javac $<

#Generate JNI Compatible C Header for JNI Shim
$(ORGUNDER)_PVFS2JNI.h : $(ORGDIR)/PVFS2JNI.java libPVFS2JNI.c
	javah $(ORGDOT).PVFS2JNI

#Preprocess, Compile, and Assemble in one step
libPVFS2JNI.o : libPVFS2JNI.c $(ORGDIR)/PVFS2JNI.java $(ORGUNDER)_PVFS2JNI.h \
                     PVFS2JNI.class
	cc -fPIC -c \
      -I$(PVFS2_TOP_PREFIX)/include \
      -I$(USRINT_DIR) \
      -I$(JAVA_HOME)include \
      -I$(JAVA_HOME)include/linux \
      $<

clean :
	rm -f *.o *.so *.h *.jar org/orangefs/usrint/*.class \
		$(PVFS2_TOP_PREFIX)/lib/libPVFS2STDIOJNI.so \
		$(PVFS2_TOP_PREFIX)/lib/libPVFS2JNI.so \
	    libs


#------------------------------------------------------------------------------
# (C) 2012 Clemson University
# See COPYING in top-level directory.

# Purpose: create a shared library that can be loaded by a Java class exposing 
# PVFS2 Library Calls to Java Developers.
#------------------------------------------------------------------------------

#    Setup - adjust these values accordingly...
#------------------------------------------------------------------------------
#Path to OrangeFS libs, binaries, etc.
INSTALL=$(HOME)/trunk
#Folder containing OrangeFS src, etc.
TOP=$(HOME)/trunk/ofs
# JAVA: Found by running: 
#    readlink -f /usr/bin/javac | sed "s:bin/javac::"
JAVA_HOME=/usr/lib/jvm/java-7-openjdk-amd64/

#Hadoop Only Related Configuration Values
#------------------------------------------------------------------------------
#Location of OFS JNI Jar
OFS_JNI =$(INSTALL)/lib/ofs_jni.jar
#Path to OFS-Hadoop Code
OFS_HADOOP=org/apache/hadoop/fs/ofs
#HADOOP VERSION
HADOOP_VERSION="1.1.1"
#Dir containing Hadoop files
HADOOP_DIR=$(HOME)/hadoop-$(HADOOP_VERSION)
#Name of Hadoop Core Jar File
CORE_NAME=hadoop-core-$(HADOOP_VERSION).jar
#Path to Hadoop's core Jar
HADOOP_CORE=$(HADOOP_DIR)/$(CORE_NAME)

#Don't Modify the following:
#------------------------------------------------------------------------------
ORGDIR=org/orangefs/usrint
ORGDOT=org.orangefs.usrint
ORGUNDER=org_orangefs_usrint

#------------------------------------------------------------------------------
#OFS Input and Output Stream Classes
IOSTREAM_JAVA=$(ORGDIR)/OrangeFileSystemInputStream.java \
	$(ORGDIR)/OrangeFileSystemOutputStream.java

IOSTREAM_CLASS=$(ORGDIR)/OrangeFileSystemInputStream.class \
	$(ORGDIR)/OrangeFileSystemOutputStream.class

#Package our OFS only Java .class files into a jar
ofs_jni.jar : libPVFS2POSIXJNI.so libPVFS2STDIOJNI.so \
        $(IOSTREAM_CLASS)
	jar cf ofs_jni.jar \
      $(ORGDIR)/*.class

#Install all Jars and Generated Shared Libs to OFS lib dir
install : ofs_jni.jar
	cp *.so $(INSTALL)/lib
	cp *.jar $(INSTALL)/lib

#Generate class files for I/O Streams
$(IOSTREAM_CLASS) : $(IOSTREAM_JAVA)
	javac $(IOSTREAM_JAVA)

#Hadoop Only Related Classes
#==============================================================================
OFS_JAVA=$(OFS_HADOOP)/OrangeFileSystem.java \
    $(OFS_HADOOP)/OrangeFileSystemFSInputStream.java

OFS_CLASS=$(OFS_HADOOP)/OrangeFileSystem.class \
    $(OFS_HADOOP)/OrangeFileSystemFSInputStream.class

$(OFS_CLASS) : $(OFS_JAVA)
	javac -cp "$(HADOOP_CORE):$(OFS_JNI):." $(OFS_JAVA)

ofs.jar : ofs_jni.jar $(OFS_CLASS)
	jar cf ofs.jar $(OFS_CLASS)

#PVFS2POSIXJNI
#==============================================================================
#Generate Java classes
POSIX_JAVA= $(ORGDIR)/PVFS2POSIXJNI.java \
	$(ORGDIR)/PVFS2POSIXJNIFlags.java \
	$(ORGDIR)/Orange.java \
	$(ORGDIR)/Fsid.java \
	$(ORGDIR)/Stat.java \
	$(ORGDIR)/Stat64.java \
	$(ORGDIR)/Statfs.java \
	$(ORGDIR)/Statfs64.java \
	$(ORGDIR)/Statvfs.java \
	$(ORGDIR)/Timeval.java \
	$(ORGDIR)/Utimbuf.java

POSIX_CLASS= $(ORGDIR)/PVFS2POSIXJNI.class \
    $(ORGDIR)/PVFS2POSIXJNIFlags.class \
	$(ORGDIR)/Orange.class \
    $(ORGDIR)/Fsid.class \
	$(ORGDIR)/Stat.class \
	$(ORGDIR)/Stat64.class \
    $(ORGDIR)/Statfs.class \
	$(ORGDIR)/Statfs64.class \
	$(ORGDIR)/Statvfs.class \
    $(ORGDIR)/Timeval.class \
	$(ORGDIR)/Utimbuf.class

#link
libPVFS2POSIXJNI.so : $(POSIX_CLASS) $(ORGUNDER)_PVFS2POSIXJNI.h libPVFS2POSIXJNI.o
	gcc -shared -g \
	  -o $@ \
	  libPVFS2POSIXJNI.o \
	  -L$(INSTALL)/lib \
	  -lofs \
	  -lpvfs2 \
	  -lpthread \
	  -ldl \
	  -lcrypto

$(POSIX_CLASS) : $(POSIX_JAVA) libPVFS2POSIXJNI.c
	javac $(POSIX_JAVA)

#Generate JNI Compatible C Header for JNI Shim
$(ORGUNDER)_PVFS2POSIXJNI.h : $(ORGDIR)/PVFS2POSIXJNI.java libPVFS2POSIXJNI.c
	javah $(ORGDOT).PVFS2POSIXJNI

#Preprocess, Compile, and Assemble in one step
libPVFS2POSIXJNI.o : libPVFS2POSIXJNI.c $(ORGUNDER)_PVFS2POSIXJNI.h
	cc -fPIC -c \
      -I$(INSTALL)/include \
      -I$(TOP)/src/client/usrint \
      -I$(JAVA_HOME)include \
      -I$(JAVA_HOME)include/linux \
      libPVFS2POSIXJNI.c

#PVFS2STDIOJNI
#===============================================================================
STDIO_JAVA=$(ORGDIR)/PVFS2STDIOJNI.java \
	$(ORGDIR)/PVFS2STDIOJNIFlags.java \
	$(ORGDIR)/Dirent.java \
	$(ORGDIR)/Dirent64.java

STDIO_CLASS=$(ORGDIR)/PVFS2STDIOJNI.class \
	$(ORGDIR)/PVFS2STDIOJNIFlags.class \
	$(ORGDIR)/Dirent.class \
	$(ORGDIR)/Dirent64.class

#Link PVFS2STDIOJNI
libPVFS2STDIOJNI.so : $(STDIO_CLASS) $(ORGUNDER)_PVFS2STDIOJNI.h libPVFS2STDIOJNI.o
	gcc -shared -g \
	  -o $@ \
	  libPVFS2STDIOJNI.o \
	  -L$(INSTALL)/lib \
	  -lofs \
	  -lpvfs2 \
	  -lpthread \
	  -ldl \
	  -lcrypto

$(STDIO_CLASS) : $(STDIO_JAVA) libPVFS2STDIOJNI.c
	javac $(STDIO_JAVA)

#Generate JNI Compatible C Header for JNI Shim
$(ORGUNDER)_PVFS2STDIOJNI.h : $(ORGDIR)/PVFS2STDIOJNI.java libPVFS2STDIOJNI.c
	javah $(ORGDOT).PVFS2STDIOJNI


#Preprocess, Compile, and Assemble in one step
libPVFS2STDIOJNI.o : libPVFS2STDIOJNI.c $(ORGUNDER)_PVFS2STDIOJNI.h
	cc -fPIC -c \
	  -I$(INSTALL)/include \
	  -I$(TOP)/src/client/usrint \
	  -I$(JAVA_HOME)include \
	  -I$(JAVA_HOME)include/linux \
	  libPVFS2STDIOJNI.c

#==============================================================================
clean :
	rm -f *.o org_*.h *.so *.jar org/orangefs/usrint/*.class \
		org/apache/hadoop/fs/ofs/*.class \
		$(INSTALL)/lib/libPVFS2POSIXJNI.so \
		$(INSTALL)/lib/libPVFS2STDIOJNI.so 

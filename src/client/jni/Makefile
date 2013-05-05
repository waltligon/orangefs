#------------------------------------------------------------------------------
# (C) 2012 Clemson University
# See COPYING in top-level directory.

# Purpose: create shared libraries and jars that can be loaded by Java classes;
# thereby exposing OrangeFS client library Calls to Java Developers.
#------------------------------------------------------------------------------

# Set your prefix path. Same as PREFIX during OrangeFS installation.
#------------------------------------------------------------------------------
# Path to OrangeFS libs, binaries, etc.
prefix=/opt/orangefs

# Hadoop Only Related Configuration Values
#------------------------------------------------------------------------------
#Dir containing Hadoop files
HADOOP_DIR=$(HOME)/hadoop-1.1.2

# Shouldn't Need to Modify the Following:
#------------------------------------------------------------------------------
#Folder containing OrangeFS src, etc.
OFS_TOP=../../..
#Location of JDK
JAVA_HOME=`readlink -f /usr/bin/javac | sed "s:bin/javac::"`
COMMONS_LOGGING=lib/commons-logging-1.1.1.jar
#Path to OFS-Hadoop Code
OFS_HADOOP=org/apache/hadoop/fs/ofs
#Location of OFS JNI Jar
OFS_JNI =$(prefix)/lib/ofs_jni.jar
#Path to Hadoop's core Jar
HADOOP_CORE=`ls $(HADOOP_DIR)/hadoop-core-*.jar`
ORGDIR=org/orangefs/usrint
ORGDOT=org.orangefs.usrint
ORGUNDER=org_orangefs_usrint

#------------------------------------------------------------------------------
#OFS I/O Stream and I/O Channel Classes
IOSTREAM_JAVA=$(ORGDIR)/OrangeFileSystemInputStream.java \
	$(ORGDIR)/OrangeFileSystemOutputStream.java

IOSTREAM_CLASS=$(ORGDIR)/OrangeFileSystemInputStream.class \
	$(ORGDIR)/OrangeFileSystemOutputStream.class

IOCHANNEL_JAVA=$(ORGDIR)/OrangeFileSystemInputChannel.java \
	$(ORGDIR)/OrangeFileSystemOutputChannel.java

IOCHANNEL_CLASS=$(ORGDIR)/OrangeFileSystemInputChannel.class \
	$(ORGDIR)/OrangeFileSystemOutputChannel.class

#Package our OFS only Java .class files into a jar
lib/ofs_jni.jar : lib/libPVFS2POSIXJNI.so lib/libPVFS2STDIOJNI.so \
		$(IOSTREAM_CLASS) \
		$(IOCHANNEL_CLASS)
	$(JAVA_HOME)/bin/jar cf $@ \
      $(ORGDIR)/*.class

#Install all Jars and Generated Shared Libs to OFS lib dir
install : lib/ofs_jni.jar
	cp lib/*.so $(prefix)/lib
	cp lib/*.jar $(prefix)/lib

#Generate class files for I/O Streams and I/O Channels
$(IOSTREAM_CLASS): $(IOSTREAM_JAVA)
	$(JAVA_HOME)/bin/javac -cp "$(COMMONS_LOGGING):." \
	  $(IOSTREAM_JAVA)

$(IOCHANNEL_CLASS): $(IOCHANNEL_JAVA)
	$(JAVA_HOME)/bin/javac -cp "$(COMMONS_LOGGING):." \
      $(IOCHANNEL_JAVA)

#Hadoop Only Related Classes
#==============================================================================
OFS_JAVA=$(OFS_HADOOP)/OrangeFileSystem.java \
    $(OFS_HADOOP)/OrangeFileSystemFSInputStream.java

OFS_CLASS=$(OFS_HADOOP)/OrangeFileSystem.class \
    $(OFS_HADOOP)/OrangeFileSystemFSInputStream.class

$(OFS_CLASS) : $(OFS_JAVA)
	$(JAVA_HOME)/bin/javac -cp "$(HADOOP_CORE):$(OFS_JNI):$(COMMONS_LOGGING):." $(OFS_JAVA)

lib/ofs.jar : lib/ofs_jni.jar $(OFS_CLASS)
	$(JAVA_HOME)/bin/jar cf $@ $(OFS_CLASS)

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
lib/libPVFS2POSIXJNI.so : $(POSIX_CLASS) $(ORGUNDER)_PVFS2POSIXJNI.h libPVFS2POSIXJNI.o
	gcc -shared -g \
	  -o $@ \
	  libPVFS2POSIXJNI.o \
	  -L$(prefix)/lib \
	  -lofs \
	  -lpvfs2 \
	  -lpthread \
	  -ldl \
	  -lcrypto

$(POSIX_CLASS) : $(POSIX_JAVA) libPVFS2POSIXJNI.c
	$(JAVA_HOME)/bin/javac $(POSIX_JAVA)

#Generate JNI Compatible C Header for JNI Shim
$(ORGUNDER)_PVFS2POSIXJNI.h : $(ORGDIR)/PVFS2POSIXJNI.java libPVFS2POSIXJNI.c
	$(JAVA_HOME)/bin/javah $(ORGDOT).PVFS2POSIXJNI

#Preprocess, Compile, and Assemble in one step
libPVFS2POSIXJNI.o : libPVFS2POSIXJNI.c $(ORGUNDER)_PVFS2POSIXJNI.h
	cc -fPIC -c \
      -I$(prefix)/include \
      -I$(OFS_TOP)/src/client/usrint \
      -I$(JAVA_HOME)/include \
      -I$(JAVA_HOME)/include/linux \
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
lib/libPVFS2STDIOJNI.so : $(STDIO_CLASS) $(ORGUNDER)_PVFS2STDIOJNI.h libPVFS2STDIOJNI.o
	gcc -shared -g \
	  -o $@ \
	  libPVFS2STDIOJNI.o \
	  -L$(prefix)/lib \
	  -lofs \
	  -lpvfs2 \
	  -lpthread \
	  -ldl \
	  -lcrypto

$(STDIO_CLASS) : $(STDIO_JAVA) libPVFS2STDIOJNI.c
	$(JAVA_HOME)/bin/javac $(STDIO_JAVA)

#Generate JNI Compatible C Header for JNI Shim
$(ORGUNDER)_PVFS2STDIOJNI.h : $(ORGDIR)/PVFS2STDIOJNI.java libPVFS2STDIOJNI.c
	$(JAVA_HOME)/bin/javah $(ORGDOT).PVFS2STDIOJNI


#Preprocess, Compile, and Assemble in one step
libPVFS2STDIOJNI.o : libPVFS2STDIOJNI.c $(ORGUNDER)_PVFS2STDIOJNI.h
	cc -fPIC -c \
	  -I$(prefix)/include \
	  -I$(OFS_TOP)/src/client/usrint \
	  -I$(JAVA_HOME)/include \
	  -I$(JAVA_HOME)/include/linux \
	  libPVFS2STDIOJNI.c

#==============================================================================
# Simplify make command to build jni and hadoop jars
jni : lib/ofs_jni.jar
hadoop : lib/ofs.jar
all : jni hadoop

clean :
	rm -f *.o org_*.h lib/*.so lib/ofs_jni.jar lib/ofs.jar org/orangefs/usrint/*.class \
		org/apache/hadoop/fs/ofs/*.class

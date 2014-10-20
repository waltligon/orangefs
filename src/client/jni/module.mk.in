ifdef BUILD_JNI

JNI_DIR := src/client/jni
JNI_JAVA_DIR := src/client/jni/src/main/java
ORGDIR := $(JNI_JAVA_DIR)/org/orangefs/usrint

USRC := \
	$(JNI_DIR)/libPVFS2POSIXJNI.c \
	$(JNI_DIR)/libPVFS2STDIOJNI.c

JNIJAVA := \
	$(ORGDIR)/PVFS2POSIXJNI.java \
	$(ORGDIR)/PVFS2POSIXJNIFlags.java \
	$(ORGDIR)/Stat.java \
	$(ORGDIR)/Statfs.java \
	$(ORGDIR)/PVFS2STDIOJNI.java \
	$(ORGDIR)/PVFS2STDIOJNIFlags.java \
	$(ORGDIR)/Orange.java \
	$(ORGDIR)/OrangeFileSystemInputStream.java \
	$(ORGDIR)/OrangeFileSystemOutputStream.java \
	$(ORGDIR)/OrangeFileSystemInputChannel.java \
	$(ORGDIR)/OrangeFileSystemOutputChannel.java \
	$(ORGDIR)/OrangeFileSystemLayout.java

#	$(ORGDIR)/Statvfs.java

# TODO add test classes to JNIJAVA

# list of all .c files (generated or otherwise) that belong in library
ULIBSRC += $(USRC)

endif # BUILD_JNI

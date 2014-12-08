DIR := src/client/usrint

ifeq ($(build_usrint),yes)

OSRC := \
	$(DIR)/pvfs-path.c \
	$(DIR)/mmap.c \
	$(DIR)/openfile-util.c \
	$(DIR)/iocommon.c \
	$(DIR)/request.c \
	$(DIR)/ucache.c \
	$(DIR)/posix-pvfs.c  \
	$(DIR)/env-vars.c

#   these routines don't need pvfs implementation and will
#   probably be removed
#	$(DIR)/acl.c \

USRC := \
	$(DIR)/posix.c \
	$(DIR)/stdio.c \
	$(DIR)/selinux.c \
	$(DIR)/overunder.c \
	$(DIR)/fts.c \
	$(DIR)/glob.c \
	$(DIR)/error.c \
	$(DIR)/recursive-remove.c

#   these routines do no need to use the pvfs descriptor
#   and will probably be removed
#	$(DIR)/socket.c \

# list of all .c files (generated or otherwise) that belong in library
OLIBSRC += $(OSRC)
ULIBSRC += $(USRC)

endif # build_usrint

SRC := \
	$(DIR)/pvfs-qualify-path.c

LIBSRC += $(SRC)


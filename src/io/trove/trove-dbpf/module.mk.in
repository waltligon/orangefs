DIR := src/io/trove/trove-dbpf
SERVERSRC += \
	$(DIR)/dbpf-bstream.c \
	$(DIR)/dbpf-collection.c \
	$(DIR)/dbpf-bstream-aio.c \
	$(DIR)/dbpf-keyval.c \
	$(DIR)/dbpf-attr-cache.c \
	$(DIR)/dbpf-open-cache.c \
	$(DIR)/dbpf-dspace.c \
        $(DIR)/dbpf-context.c \
	$(DIR)/dbpf-op.c \
	$(DIR)/dbpf-op-queue.c \
	$(DIR)/dbpf-thread.c \
	$(DIR)/dbpf-error.c \
	$(DIR)/dbpf-mgmt.c \
	$(DIR)/dbpf-keyval-pcache.c \
	$(DIR)/dbpf-sync.c \
	$(DIR)/dbpf-alt-aio.c \
	$(DIR)/dbpf-null-aio.c \
	$(DIR)/dbpf-bstream-direct.c

# Grab trove-ledger.h from handle-mgmt.  Also make _GNU_SOURCE definition 
# required for access to pread/pwrite on Linux.  _XOPEN_SOURCE seems to be
# incompatible with Berkeley DB.
MODCFLAGS_$(DIR) = -I$(srcdir)/src/io/trove/trove-handle-mgmt -D_GNU_SOURCE

DIR := src/client/sysint

CSRC := \
	$(DIR)/finalize.c \
	$(DIR)/initialize.c \
	$(DIR)/acache.c \
	$(DIR)/ncache.c \
	$(DIR)/pint-sysint-utils.c \
	$(DIR)/getparent.c \
	$(DIR)/client-state-machine.c \
	$(DIR)/mgmt-get-config.c \
	$(DIR)/mgmt-misc.c \
	$(DIR)/sys-dist.c \
	$(DIR)/error-details.c

CLIENT_SMCGEN := \
	$(DIR)/remove.c \
	$(DIR)/sys-getattr.c \
	$(DIR)/sys-setattr.c \
	$(DIR)/sys-get-eattr.c \
	$(DIR)/sys-set-eattr.c \
	$(DIR)/sys-del-eattr.c \
	$(DIR)/sys-list-eattr.c \
	$(DIR)/sys-lookup.c \
	$(DIR)/sys-truncate.c \
	$(DIR)/sys-io.c \
	$(DIR)/sys-small-io.c \
	$(DIR)/sys-create.c \
	$(DIR)/sys-mkdir.c \
	$(DIR)/sys-remove.c \
	$(DIR)/sys-flush.c \
	$(DIR)/sys-symlink.c \
	$(DIR)/sys-readdir.c \
	$(DIR)/sys-readdirplus.c \
	$(DIR)/sys-rename.c \
	$(DIR)/sys-statfs.c \
	$(DIR)/client-job-timer.c \
	$(DIR)/perf-count-timer.c \
	$(DIR)/pint-sysdev-unexp.c \
	$(DIR)/server-get-config.c \
	$(DIR)/fs-add.c \
	$(DIR)/mgmt-noop.c \
	$(DIR)/mgmt-setparam-list.c \
	$(DIR)/mgmt-statfs-list.c \
	$(DIR)/mgmt-perf-mon-list.c \
	$(DIR)/mgmt-event-mon-list.c \
	$(DIR)/mgmt-iterate-handles-list.c \
	$(DIR)/mgmt-get-dfile-array.c \
	$(DIR)/mgmt-remove-object.c \
	$(DIR)/mgmt-remove-dirent.c \
	$(DIR)/mgmt-create-dirent.c \
	$(DIR)/mgmt-get-dirdata-handle.c

# track generated .c files that need to be removed during dist clean, etc.
SMCGEN += $(CLIENT_SMCGEN)

# list of all .c files (generated or otherwise) that belong in library
LIBSRC += $(CSRC) $(CLIENT_SMCGEN)

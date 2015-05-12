DIR := src/apps/admin

ADMINSRC := \
	$(DIR)/pvfs2-check-config.c \
	$(DIR)/pvfs2-set-debugmask.c \
	$(DIR)/pvfs2-set-mode.c \
	$(DIR)/pvfs2-set-eventmask.c \
	$(DIR)/pvfs2-set-sync.c \
	$(DIR)/pvfs2-ls.c \
	$(DIR)/pvfs2-ping.c \
	$(DIR)/pvfs2-stat.c \
	$(DIR)/pvfs2-statfs.c \
	$(DIR)/pvfs2-perf-mon-example.c \
	$(DIR)/pvfs2-perf-mon-snmp.c \
	$(DIR)/pvfs2-mkdir.c \
	$(DIR)/pvfs2-chmod.c \
	$(DIR)/pvfs2-chown.c \
	$(DIR)/pvfs2-fs-dump.c\
	$(DIR)/pvfs2-fsck.c\
	$(DIR)/pvfs2-validate.c\
	$(DIR)/pvfs2-cp.c \
	$(DIR)/pvfs2-write.c \
	$(DIR)/pvfs2-viewdist.c \
	$(DIR)/pvfs2-xattr.c \
	$(DIR)/pvfs2-touch.c \
	$(DIR)/pvfs2-remove-object.c \
	$(DIR)/pvfs2-ln.c \
	$(DIR)/pvfs2-perror.c \
	$(DIR)/pvfs2-check-server.c \
	$(DIR)/pvfs2-drop-caches.c \
	$(DIR)/pvfs2-get-uid.c

ifdef ENABLE_SECURITY_KEY
	ADMINSRC += $(DIR)/pvfs2-gencred.c
else ifdef ENABLE_SECURITY_CERT
	ADMINSRC += $(DIR)/pvfs2-gencred.c \
	            $(DIR)/pvfs2-get-user-cert.c
endif

ADMINSRC_SERVER := \
	$(DIR)/pvfs2-mkspace.c \
	$(DIR)/pvfs2-migrate-collection.c \
	$(DIR)/pvfs2-change-fsid.c \
	$(DIR)/pvfs2-showcoll.c

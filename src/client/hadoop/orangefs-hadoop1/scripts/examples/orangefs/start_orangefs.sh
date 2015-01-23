cd $(dirname $0)
. setenv
# Copy the tab file to the storage directory
cp $PVFS2TAB_FILE /tmp/orangefs_hadoop_storage/pvfs2tab

# START
${ORANGEFS_PREFIX}/sbin/pvfs2-server -a localhost ${ORANGEFS_CONF_FILE}
sleep 3
# PING
${ORANGEFS_PREFIX}/bin/pvfs2-ping -m /mnt/orangefs

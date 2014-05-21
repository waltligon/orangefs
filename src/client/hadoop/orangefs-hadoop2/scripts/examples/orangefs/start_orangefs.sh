cd $(dirname $0)
. setenv
# START
${ORANGEFS_PREFIX}/sbin/pvfs2-server "${ORANGEFS_CONF_FILE}"
# PING
${ORANGEFS_PREFIX}/bin/pvfs2-ping -m /mnt/orangefs

cd $(dirname $0)
. setenv
${ORANGEFS_PREFIX}/sbin/pvfs2-server "${ORANGEFS_CONF_FILE}" -f

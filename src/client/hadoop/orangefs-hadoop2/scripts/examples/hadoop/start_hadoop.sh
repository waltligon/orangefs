#!/usr/bin/env bash
set -x
cd $(dirname $0)

# This script requires that two environment variables be defined:
# - HADOOP_PREFIX
# - HADOOP_CONF_DIR

# START
$HADOOP_PREFIX/sbin/yarn-daemon.sh --config ${HADOOP_CONF_DIR} start resourcemanager
for slave in $(cat $HADOOP_CONF_DIR/slaves); do
    ssh $slave "$HADOOP_PREFIX/sbin/yarn-daemon.sh --config ${HADOOP_CONF_DIR} start nodemanager"
    ssh $slave "$HADOOP_PREFIX/sbin/yarn-daemon.sh --config ${HADOOP_CONF_DIR} start proxyserver"
    ssh $slave "$HADOOP_PREFIX/sbin/mr-jobhistory-daemon.sh --config ${HADOOP_CONF_DIR} start historyserver"
done
echo "Visit http://localhost:8088"

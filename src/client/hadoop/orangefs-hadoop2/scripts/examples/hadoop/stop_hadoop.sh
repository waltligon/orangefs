#!/usr/bin/env bash
set -x
cd $(dirname $0)

# This script requires that two environment variables be defined:
# - HADOOP_PREFIX
# - HADOOP_CONF_DIR

# STOP
$HADOOP_PREFIX/sbin/yarn-daemon.sh --config ${HADOOP_CONF_DIR} stop resourcemanager
for slave in $(cat $HADOOP_CONF_DIR/slaves); do
  ssh $slave "$HADOOP_PREFIX/sbin/yarn-daemon.sh --config ${HADOOP_CONF_DIR} stop nodemanager"
  ssh $slave "$HADOOP_PREFIX/sbin/yarn-daemon.sh --config ${HADOOP_CONF_DIR} stop proxyserver"
  ssh $slave "$HADOOP_PREFIX/sbin/mr-jobhistory-daemon.sh --config ${HADOOP_CONF_DIR} stop historyserver"
done

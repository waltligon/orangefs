#!/usr/bin/env bash
set -x
cd $(dirname $0)

# This script requires two variables to be defined in ./setenv
# - HADOOP_PREFIX
# - HADOOP_CONF_DIR
. setenv

# STOP
$HADOOP_PREFIX/sbin/yarn-daemon.sh --config ${HADOOP_CONF_DIR} stop resourcemanager
$HADOOP_PREFIX/sbin/yarn-daemon.sh --config ${HADOOP_CONF_DIR} stop nodemanager
$HADOOP_PREFIX/sbin/yarn-daemon.sh --config ${HADOOP_CONF_DIR} stop proxyserver
$HADOOP_PREFIX/sbin/mr-jobhistory-daemon.sh --config ${HADOOP_CONF_DIR} stop historyserver

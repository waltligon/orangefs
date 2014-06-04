#!/usr/bin/env bash
set -x
cd $(dirname $0)

# This script requires two variables to be defined in ./setenv
# - HADOOP_PREFIX
# - HADOOP_CONFIG_DIR
. setenv

# STOP
$HADOOP_PREFIX/sbin/yarn-daemon.sh --config ${HADOOP_CONFIG_DIR} stop resourcemanager
$HADOOP_PREFIX/sbin/yarn-daemon.sh --config ${HADOOP_CONFIG_DIR} stop nodemanager
$HADOOP_PREFIX/sbin/yarn-daemon.sh --config ${HADOOP_CONFIG_DIR} stop proxyserver
$HADOOP_PREFIX/sbin/mr-jobhistory-daemon.sh --config ${HADOOP_CONFIG_DIR} stop historyserver

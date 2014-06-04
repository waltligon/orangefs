#!/usr/bin/env bash
set -x
cd $(dirname $0)

# This script requires two variables to be defined in ./setenv
# - HADOOP_PREFIX
# - HADOOP_CONFIG_DIR
. setenv

# START
$HADOOP_PREFIX/sbin/yarn-daemon.sh --config ${HADOOP_CONFIG_DIR} start resourcemanager
$HADOOP_PREFIX/sbin/yarn-daemon.sh --config ${HADOOP_CONFIG_DIR} start nodemanager
$HADOOP_PREFIX/sbin/yarn-daemon.sh --config ${HADOOP_CONFIG_DIR} start proxyserver
$HADOOP_PREFIX/sbin/mr-jobhistory-daemon.sh --config ${HADOOP_CONFIG_DIR} start historyserver
echo "Visit http://localhost:8088"

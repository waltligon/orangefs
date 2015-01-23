#!/usr/bin/env bash
set -x
cd $(dirname $0)

# This script requires that two variables be defined:
# - HADOOP_CONF_DIR
# - HADOOP_LOCAL_DIR
. setenv

# CLEANUP
for slave in $(cat $HADOOP_CONF_DIR/slaves); do
  ssh $slave "rm -rf /tmp/hadoop-$USER $HADOOP_LOCAL_DIR"
done

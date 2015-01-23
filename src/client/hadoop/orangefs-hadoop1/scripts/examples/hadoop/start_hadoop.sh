#!/usr/bin/env bash
set -x
cd $(dirname $0)

# This script requires that two environment variables be defined:
# - HADOOP_PREFIX
# - HADOOP_CONF_DIR

# START
$HADOOP_PREFIX/bin/start-mapred.sh
echo "Visit http://localhost:50030"

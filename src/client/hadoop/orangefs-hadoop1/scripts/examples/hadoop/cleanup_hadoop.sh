#!/usr/bin/env bash
set -x
cd $(dirname $0)

# This script requires two variables to be defined in ./setenv
# - HADOOP_LOG_DIR
# - HADOOP_LOCAL_DIR
. setenv

# CLEANUP
rm -rf /tmp/hadoop-$USER

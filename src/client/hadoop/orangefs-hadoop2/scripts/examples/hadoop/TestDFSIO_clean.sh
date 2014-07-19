#!/usr/bin/env bash
set -x
cd $(dirname $0)

# This script requires two variables to be defined in ./setenv
# - HADOOP_PREFIX
# - HADOOP_CONFIG_DIR
. setenv

# -fileSize represents file size in MB
${HADOOP_PREFIX}/bin/hadoop \
    --config ${HADOOP_CONFIG_DIR} \
    org.apache.hadoop.fs.TestDFSIO \
        -clean


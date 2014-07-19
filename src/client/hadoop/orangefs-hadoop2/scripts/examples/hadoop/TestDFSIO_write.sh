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
        -write \
        -nrFiles 1 \
        -fileSize 64 \
        -bufferSize 67108864 # 64MB <--- buffer size in bytes. default is 1000000 bytes

#       -bufferSize 4194304 # 4MB has shown good performance in the past <--- buffer size in bytes. default is 1000000 bytes


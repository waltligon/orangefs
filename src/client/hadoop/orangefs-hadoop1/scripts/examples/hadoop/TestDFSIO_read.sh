#!/usr/bin/env bash
set -x
cd $(dirname $0)

# This script requires two variables to be defined in ./setenv
# - HADOOP_PREFIX
# - HADOOP_CONF_DIR
. setenv

# -fileSize represents file size in MB
${HADOOP_PREFIX}/bin/hadoop \
  --config ${HADOOP_CONF_DIR} \
  jar ${HADOOP_PREFIX}/hadoop-test-1.?.?.jar TestDFSIO \
  -read \
  -nrFiles 1 \
  -fileSize 8 \
  -bufferSize 67108864 # 64MB <--- buffer size in bytes. default is 1000000 bytes

#       -bufferSize 4194304 # 4MB has shown good performance in the past <--- buffer size in bytes. default is 1000000 bytes

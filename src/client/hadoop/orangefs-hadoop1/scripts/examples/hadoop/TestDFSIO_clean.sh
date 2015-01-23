#!/usr/bin/env bash
set -x
cd $(dirname $0)

# This script requires that two variables be defined:
# - HADOOP_PREFIX
# - HADOOP_CONF_DIR

# -fileSize represents file size in MB
${HADOOP_PREFIX}/bin/hadoop \
  --config ${HADOOP_CONF_DIR} \
  jar ${HADOOP_PREFIX}/hadoop-test-1.?.?.jar TestDFSIO \
  -clean

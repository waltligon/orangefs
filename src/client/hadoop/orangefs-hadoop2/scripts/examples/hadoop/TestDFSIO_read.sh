#!/usr/bin/env bash
set -x
cd $(dirname $0)

# This script requires that two variables be defined:
# - HADOOP_PREFIX
# - HADOOP_CONF_DIR

# -fileSize represents file size in MB
${HADOOP_PREFIX}/bin/hadoop \
  --config ${HADOOP_CONF_DIR} \
  org.apache.hadoop.fs.TestDFSIO \
  -read \
  -nrFiles 2 \
  -fileSize 64

# To specify a custom buffer size for TestDFSIO -read, add the following to the command above:
#
#  -Dfs.ofs.file.buffer.size=67108864
#
# This instructs the OrangeFS / Hadoop plugin to use the specified buffer size
# rather than the parameter passed via the API.


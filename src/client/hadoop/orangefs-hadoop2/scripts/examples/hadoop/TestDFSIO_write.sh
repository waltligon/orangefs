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
  -write \
  -nrFiles 2 \
  -fileSize 64 \
  -bufferSize 67108864 # 64MB <--- write buffer size in bytes. default is 1000000 bytes

# To use specific layout for OrangeFS files, add to the above command:
#
#  -Dfs.ofs.file.layout=PVFS_SYS_LAYOUT_RANDOM \
#
# PVFS_SYS_LAYOUT_RANDOM is the default for files created via the
# OrangeFS / Hadoop plugin.

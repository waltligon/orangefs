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
  -write \
  -nrFiles 2 \
  -fileSize 64

# To use specific layout for OrangeFS files, add to the above command:
#
#  -Dfs.ofs.file.layout=PVFS_SYS_LAYOUT_RANDOM \
#
# PVFS_SYS_LAYOUT_RANDOM is the default for files created via the
# OrangeFS / Hadoop plugin.


# To specify a custom buffer size for TestDFSIO -write, add the following to the command above:
#
#  -Dfs.ofs.file.buffer.size=67108864
#
# This instructs the OrangeFS / Hadoop plugin to use the specified buffer size
# rather than the parameter passed via the API.


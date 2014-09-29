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
  -nrFiles 2 \
  -fileSize 64

# To specify a custom buffer size for TestDFSIO -read, add the following to the command above:
#
#  -Dfs.ofs.file.buffer.size=67108864
#
# This instructs the OrangeFS / Hadoop plugin to use the specified buffer.size
# rather than the plugin default. This is different than TestDFSIO -write in
# that the bufferSize flag isn't used in TestDFSIO -read, so in order to
# customize buffer size the above flag must be used.

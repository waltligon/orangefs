#!/usr/bin/env bash
set -x
cd $(dirname $0)

# This script requires that two variables be defined:
# - HADOOP_PREFIX
# - HADOOP_CONF_DIR

${HADOOP_PREFIX}/bin/hadoop \
    --config ${HADOOP_CONF_DIR} \
    jar ${HADOOP_PREFIX}/hadoop-examples-1.?.?.jar \
    teragen \
    10000000 teragen_data

# You may want to increase the number of reduce tasks by specifying for instance:
#
# -D mapred.map.tasks=10 \
#
# The default is 1.


#!/usr/bin/env bash
set -x
cd $(dirname $0)

# This script requires that two variables be defined:
# - HADOOP_PREFIX
# - HADOOP_CONF_DIR

${HADOOP_PREFIX}/bin/hadoop \
    --config ${HADOOP_CONF_DIR} \
    jar ${HADOOP_PREFIX}/hadoop-examples-1.?.?.jar \
    terasort \
    teragen_data terasort_data
    
# You may want to increase the number of reduce tasks by specifying for instance:
#
# -D mapred.reduce.tasks=30 \
#
# The default is 1.

# TODO How is the number of map tasks influenced?


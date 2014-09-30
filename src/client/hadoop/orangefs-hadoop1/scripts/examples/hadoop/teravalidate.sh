#!/usr/bin/env bash
set -x
cd $(dirname $0)

# This script requires that two variables be defined:
# - HADOOP_PREFIX
# - HADOOP_CONF_DIR

${HADOOP_PREFIX}/bin/hadoop \
    --config ${HADOOP_CONF_DIR} \
    jar ${HADOOP_PREFIX}/hadoop-examples-1.?.?.jar \
    teravalidate \
    terasort_data teravalidate_data

# Notes:
# One map task is created per reduce task run by terasort. (One per file)


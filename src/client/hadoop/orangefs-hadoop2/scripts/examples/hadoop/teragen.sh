#!/usr/bin/env bash
set -x
cd $(dirname $0)

# This script requires two variables to be defined in ./setenv
# - HADOOP_PREFIX
# - HADOOP_CONFIG_DIR
. setenv

${HADOOP_PREFIX}/bin/hadoop \
    --config ${HADOOP_CONFIG_DIR} \
    jar ${HADOOP_PREFIX}/share/hadoop/mapreduce/hadoop-mapreduce-examples-?.?.?.jar \
    teragen 5242880  teragen_data # 500 MB = (5242880 * 100 Bytes)
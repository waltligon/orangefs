#!/usr/bin/env bash
set -x
cd $(dirname $0)

# This script requires two variables to be defined in ./setenv
# - HADOOP_PREFIX
# - HADOOP_CONF_DIR
. setenv

${HADOOP_PREFIX}/bin/hadoop \
    --config ${HADOOP_CONF_DIR} \
    jar ${HADOOP_PREFIX}/share/hadoop/mapreduce/hadoop-mapreduce-examples-?.?.?.jar \
    wordcount $@

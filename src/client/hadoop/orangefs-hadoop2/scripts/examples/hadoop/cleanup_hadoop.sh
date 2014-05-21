#!/bin/bash
set -x
cd $(dirname $0)

HADOOP_LOG_DIR="/home/denton/hadoop2logs"
HADOOP_LOCAL_DIR="/home/denton/hadoop2local"

# CLEANUP
rm -rf "${HADOOP_LOG_DIR}" "${HADOOP_LOCAL_DIR}"

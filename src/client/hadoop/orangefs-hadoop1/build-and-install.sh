#!/usr/bin/env bash
set -x
cd $(dirname $0)

mvn -DskipTests clean package && \
  sudo cp target/orangefs-hadoop1-?.?.?.jar "${ORANGEFS_PREFIX}/lib/"


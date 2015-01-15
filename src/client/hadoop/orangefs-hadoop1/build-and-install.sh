#!/usr/bin/env bash
set -x
cd $(dirname $0)

mvn -Dmaven.compiler.target=1.6 -Dmaven.compiler.source=1.6 -DskipTests clean package && \
  sudo cp target/orangefs-hadoop1-?.?.?.jar "${ORANGEFS_PREFIX}/lib/"


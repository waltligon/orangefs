#!/usr/bin/env bash
set -x
cd $(dirname $0)

orangefs_install_prefix=/opt/orangefs-denton.hadoop2.trunk

mvn -DskipTests clean package && \
  sudo cp target/orangefs-hadoop2-?.?.?.jar ${orangefs_install_prefix}/lib/


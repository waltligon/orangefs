#!/usr/bin/env bash
cd $(dirname $0)
ORANGEFS_INSTALL_PREFIX=/opt/orangefs-denton.hadoop2.trunk

mvn -Djni.library.path=${ORANGEFS_INSTALL_PREFIX}/lib -DskipTests clean
mvn -Djni.library.path=${ORANGEFS_INSTALL_PREFIX}/lib -DskipTests package
sudo cp target/orangefs-hadoop2-?.?.?.jar ${ORANGEFS_INSTALL_PREFIX}/lib/
./scripts/examples/orangefs/stop_orangefs.sh
./scripts/examples/hadoop/stop_hadoop.sh
./scripts/examples/hadoop/cleanup_hadoop.sh
./scripts/examples/orangefs/reset_orangefs.sh
./scripts/examples/hadoop/start_hadoop.sh

#!/usr/bin/env bash
cd $(dirname $0)

mvn -Djni.library.path=/opt/orangefs-denton.hadoop2.trunk/lib -DskipTests clean
mvn -Djni.library.path=/opt/orangefs-denton.hadoop2.trunk/lib -DskipTests package
sudo cp target/orangefs-hadoop2-2.9.jar /opt/orangefs-denton.hadoop2.trunk/lib/
./scripts/examples/orangefs/stop_orangefs.sh
./scripts/examples/hadoop/stop_hadoop.sh
./scripts/examples/hadoop/cleanup_hadoop.sh
./scripts/examples/orangefs/reset_orangefs.sh
./scripts/examples/hadoop/start_hadoop.sh

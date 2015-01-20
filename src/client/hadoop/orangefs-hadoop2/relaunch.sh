#!/usr/bin/env bash
set -x
cd $(dirname $0)

# TODO fail if any of these scripts fail
./scripts/examples/orangefs/stop_orangefs.sh
./scripts/examples/hadoop/stop_hadoop.sh
./scripts/examples/hadoop/cleanup_hadoop.sh
./scripts/examples/orangefs/reset_orangefs.sh
./scripts/examples/hadoop/start_hadoop.sh


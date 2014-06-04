#!/usr/bin/env bash
set -x
cd $(dirname $0)

# RESET
./stop_hadoop.sh
sleep 3
./cleanup_hadoop.sh
./start_hadoop.sh

#!/usr/bin/env bash
cd $(dirname $0)

SHOW_LOGS=${1:-false}

# This script requires one variable to be defined in ./setenv
# - HADOOP_LOG_DIR
. setenv

ALL_LOGS=$(find ${HADOOP_LOG_DIR} -iname "*.log")
WIDTH=-20

echo
printf "Found the following log files in HADOOP_LOG_DIR=%s\n" "${HADOOP_LOG_DIR}"
printf "================================================================================\n"
printf "%s\n" "${ALL_LOGS}"
echo

printf "Some log statistics:\n"
printf "================================================================================\n"
printf "%${WIDTH}s %${WIDTH}s %${WIDTH}s %s\n" "ERROR" "WARN" "LINES" "LOG_PATH"
for logfile in ${ALL_LOGS}; do
  printf "%${WIDTH}s %${WIDTH}s %${WIDTH}s %${WIDTH}s\n" \
      "$(cat ${logfile} | grep ERROR | wc -l)" \
      "$(cat ${logfile} | grep WARN | wc -l)" \
      "$(cat ${logfile} | wc -l)" \
      "${logfile}"
done
echo

# Show only if SHOW_LOGS=true
if [ "${SHOW_LOGS}" = true ]; then
  for logfile in ${ALL_LOGS}; do
    printf "logfile=%s\n\n" "${logfile}"
    cat "${logfile}"
    echo
  done
fi

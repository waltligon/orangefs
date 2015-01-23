#!/usr/bin/env bash
set -x

# The lines that should be added when enabling ("on") and removed when
# disabling ("off") logging for the classes mentioned below.
#
# log4j.logger.org.apache.hadoop.fs.ofs.OrangeFileSystem=DEBUG
# log4j.logger.org.apache.hadoop.fs.ofs.OrangeFS=DEBUG
#

WHICH=${1:-on} # on or off
UPDATE_USING_SUDO=true # set to false to update jar under your user account (not root)

# Lines to be added or removed from target file
LINE1="log4j.logger.org.apache.hadoop.fs.ofs.OrangeFileSystem=DEBUG"
LINE2="log4j.logger.org.apache.hadoop.fs.ofs.OrangeFS=DEBUG"

# Variables
#HADOOP_PREFIX=
TMP_DIR="/tmp"
TARGET_FILE="container-log4j.properties"
TARGET_JAR_PATH="$(find "${HADOOP_PREFIX}" -iname "hadoop-yarn-server-nodemanager-?.?.?.jar")"

trap 'rm "${TMP_DIR}/${TARGET_FILE}"' EXIT

cd "${TMP_DIR}"
# Extract the target file to the TMP_DIR
jar xf "${TARGET_JAR_PATH}" ${TARGET_FILE}

if [ "$WHICH" = "off" ]; then
  printf "off\n"
  # Remove matching lines from target file
  sed -i "/\b\(${LINE1}\|${LINE2}\)\b/d" "${TARGET_FILE}"
fi

if [ "$WHICH" = "on" ]; then
  printf "on\n"
  # Remove matching lines from target file so the lines aren't added redundantly
  sed -i "/\b\(${LINE1}\|${LINE2}\)\b/d" "${TARGET_FILE}"
  # Add the lines
  printf "${LINE1}\n" >> "${TARGET_FILE}"
  printf "${LINE2}\n" >> "${TARGET_FILE}"
fi

# Update the jar as root or yourself
if [ "${UPDATE_USING_SUDO}" = true ]; then
  sudo jar uf "${TARGET_JAR_PATH}" "${TARGET_FILE}"
  sudo chmod a+r "${TARGET_JAR_PATH}"
else
  jar uf "${TARGET_JAR_PATH}" "${TARGET_FILE}"
  chmod a+r "${TARGET_JAR_PATH}"
fi

# Show the contents of the updated file (may have changes or not)...
printf "File: ${TARGET_FILE} has been updated.\nFILE CONTENTS BELOW...\n"
cat "${TARGET_FILE}"

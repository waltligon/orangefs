#!/usr/bin/env bash

VERSION=2.5.0

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root" 1>&2
   exit 1
fi

TMP_DIR=/tmp/install_protoc

set -x

trap 'rm -rf ${TMP_DIR}' EXIT && \
mkdir -p ${TMP_DIR} && \
cd ${TMP_DIR} && \
wget http://protobuf.googlecode.com/files/protobuf-${VERSION}.tar.gz && \
tar xzf protobuf-${VERSION}.tar.gz && \
cd protobuf-${VERSION} && \
./configure && \
make && \
sudo make install && \
sudo ldconfig

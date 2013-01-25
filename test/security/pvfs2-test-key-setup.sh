#!/bin/sh
# setup-security-key-server.sh
# Copyright (C) Clemson University and Omnibond Systems LLC
#
# USAGE: setup-security-key.sh [-s [servers...]] [-c [clients...]]
#    Setup environment for key-based security testing
#    Will generate keypairs for hosts specified on command-line, 
#    as well as keystore file. With no arguments, generate server and 
#    client keys for current host only.
#    Will use scp to copy files to specified hosts.
#    Requires OpenSSL.
#    Run as root.
#    Environment variables:
#        PVFS2_SSK_KEY_PATH: path to store keys and keystore on each host
#           Default: /usr/local/orangefs/etc
#        PVFS2_SSK_NO_SCP: define to not scp (copy) files to other hosts
#        PVFS2_SSK_KEY_SIZE: server key size in bits, client size is half this
#           Default: 2048
#        PVFS2_SSK_REMOVE: define to remove files not needed by the local host

# build host lists
thishost=`hostname`

listmode="none"

if [ "x$*" != "x" ]; then
  while [ $# -gt 0 ]; do
    if [ "$1" = "-c" ]; then
      listmode="client"
    elif [ "$1" = "-s" ]; then
      listmode="server"
    else
      test $listmode = "none" && echo "USAGE: ./setup-security-key.sh [-s [servers...]] [-c [clients...]]" && exit
      test $listmode = "server" && servers="$servers $1"
      test $listmode = "client" && clients="$clients $1"
    fi
    shift
  done
else
  clients=$thishost
  servers=$thishost
fi

# set key path
if [ "x$PVFS2_SSK_KEY_PATH" != "x" ]; then
  keypath=$PVFS2_SSK_KEY_PATH
else
  keypath=/usr/local/orangefs/etc
fi

# set key size
if [ "x$PVFS2_SSK_KEY_SIZE" != "x" ]; then
  skeysize=$PVFS2_SSK_KEY_SIZE
  ckeysize=$(($PVFS2_SSK_KEY_SIZE / 2))
else
  skeysize=2048
  ckeysize=1024
fi

# keystore file
ksfile="${keypath}/keystore"

# generate server keys
for server in $servers
do
  fname="${keypath}/$server-serverkey.pem"
  echo "Generating server key for $server..."

# generate private key
  openssl genrsa -out $fname $skeysize
  chmod 600 $fname

# output public key to keystore
  echo "S:${server}" >> $ksfile
  openssl rsa -in $fname -pubout >> $ksfile
done

# generate client keys
for client in $clients
do
  fname="${keypath}/$client-clientkey.pem"
  echo "Generating client key for $client..."

# generate private key
  openssl genrsa -out $fname $ckeysize
  chmod 600 $fname

# output public key to keystore
  echo "C:${client}" >> $ksfile
  openssl rsa -in $fname -pubout >> $ksfile
done

# rename current host's files
if [ -f "${keypath}/${thishost}-serverkey.pem" ]; then
  mv "${keypath}/${thishost}-serverkey.pem" "${keypath}/pvfs2-serverkey.pem"
fi
if [ -f "${keypath}/${thishost}-clientkey.pem" ]; then
  mv "${keypath}/${thishost}-clientkey.pem" "${keypath}/pvfs2-clientkey.pem"
fi

# scp files to other hosts
if [ "x$PVFS2_SSK_NO_SCP" != "x" ]; then
  exit 0
fi

for server in $servers
do
  if [ $server = $thishost ]; then
    continue
  fi
  fname="${keypath}/$server-serverkey.pem"
  target="${keypath}/pvfs2-serverkey.pem"

# scp private key and keystore
  scp -B -p $fname "${server}:$target"
  scp -B -p $ksfile "${server}:$ksfile"
done

for client in $clients
do
  if [ $client = $thishost ]; then
    continue
  fi
  fname="${keypath}/$client-clientkey.pem"
  target="${keypath}/pvfs2-clientkey.pem"

# scp client key
  scp -B -p $fname "${client}:$target"
done

if [ "x$PVFS2_SSK_REMOVE" != "x" ]; then
  for server in $servers
  do
    rm "${keypath}/${server}-serverkey.pem"
  done
  for client in $clients
  do
    rm "${keypath}/${client}-clientkey.pem"
  done
fi


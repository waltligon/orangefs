#!/bin/sh
# Usage: pvfs2-gen-keys.sh [-a] [-s <servers...>] [-c <clients...>]
# Generates private keys and keystore for specified servers and clients
# -a: append to existing keystore

declare -a servers
declare -a clients

append=0

if [ "$1x" = "-ax" ]; then
    append=1
    shift
fi

# read servers
if [ "$1x" = "-sx" ]; then 
    i=0
    while shift && test "$1x" != "-cx"
    do
        servers[$((i++))]=$1	
    done
fi

# read clients
if [ "$1x" = "-cx" ]; then
    i=0
    while shift
    do
        clients[$((i++))]=$1
    done
fi

if [ ${#servers[*]} -eq 0 -a ${#clients[*]} -eq 0 ]; then
    echo "USAGE: $0 [-a] [-s <servers...>] [-c <clients...>]"
    exit 1
fi

# backup keystore
if [ $append -ne 1 -a -f orangefs-keystore ]; then
    mv orangefs-keystore orangefs-keystore.bak
fi

for server in ${servers[*]}
do
    # generate private key
    openssl genrsa -out orangefs-serverkey-${server}.pem 2048
    chmod 600 orangefs-serverkey-${server}.pem
    # append public key to keystore
    echo "S:${server}" >> orangefs-keystore
    openssl rsa -in orangefs-serverkey-${server}.pem -pubout >> orangefs-keystore
    echo "Created orangefs-serverkey-${server}.pem"
done

for client in ${clients[*]}
do
    # generate private key
    openssl genrsa -out pvfs2-clientkey-${client}.pem 1024
    chmod 600 pvfs2-clientkey-${client}.pem
    # append public key to keystore
    echo "C:${client}" >> orangefs-keystore
    openssl rsa -in pvfs2-clientkey-${client}.pem -pubout >> orangefs-keystore
    echo "Created pvfs2-clientkey-${client}.pem"
done

chmod 600 orangefs-keystore

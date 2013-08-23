#!/bin/sh
# Usage: ./pvfs2-dist-keys.sh <OrangeFS dir>
# After using pvfs2-gen-keys.sh, use this script to copy files to 
# remote systems.

if [ $# -ne 1 ]; then
    echo "Usage: $0 <OrangeFS dir>"
    exit 1
fi

install_dir=$1

# copy server keys and keystore
for file in orangefs-serverkey*.pem
do
    server=`echo $file | sed 's/orangefs-serverkey-//' | sed 's/\.pem//'`
    scp $file ${server}:${install_dir}/etc/orangefs-serverkey.pem
    scp orangefs-keystore ${server}:${install_dir}/etc/orangefs-keystore
done

# copy client keys
for file in pvfs2-clientkey*.pem
do
    client=`echo $file | sed 's/pvfs2-clientkey-//' | sed 's/\.pem//'`
    scp $file ${client}:${install_dir}/etc/pvfs2-clientkey.pem
done


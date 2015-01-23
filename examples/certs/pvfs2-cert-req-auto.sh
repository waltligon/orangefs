#!/bin/bash
# Generate a certificate request.
name=${1:-pvfs2}
# if user not specified, current user will be used
user=$2

if [ "x$user" != "x" ]
then
  # create temporary config file with user name
  sed "s/^CN.*$/CN = $user/" ${name}-user-auto.cnf > ${name}-user-auto-tmp.cnf
else
  cp ${name}-user-auto.cnf ${name}-user-auto-tmp.cnf
fi

openssl req -newkey rsa:1024 -keyout ${name}-cert-key.pem \
    -config ${name}-user-auto-tmp.cnf -out ${name}-cert-req.pem -nodes \
    && echo "${name}-cert-key.pem and ${name}-cert-req.pem created"

rm ${name}-user-auto-tmp.cnf

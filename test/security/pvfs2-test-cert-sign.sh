#!/bin/bash
# Usage: sign-cert.sh <cert name> <CA name>
# Input: <cert name>-cert-req.pem  (certificate request)
#        <CA name>-ca.pem   (CA (signing) certificate)
#        <CA name>-ca-key.pem (CA private key)
# Output: <cert name>-cert.pem (output certificate)
# Default cert name and CA name: pvfs2
# Generates signed certificate using cert. request and CA cert. 
# Requires serial number in <name>-ca.srl (even number of hex digits)
name=${1:-pvfs2-test}
caname=${2:-pvfs2-test}
if [ ! -f ${caname}-ca.srl ]; then
    echo 03E8 > ${caname}-ca.srl  # start at 1000  
fi
openssl x509 -req -in ${name}-cert-req.pem -out ${name}-cert.pem \
    -CA ${caname}-ca.pem -CAkey ${caname}-ca-key.pem -days 365 \
    && echo "${name}-cert.pem created"


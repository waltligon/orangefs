#!/bin/bash
# Usage: pvfs2-cert-sign.sh <cert name> <CA name>
# Input: <cert name>-cert-req.pem  (certificate request)
#        <CA name>-ca.pem   (CA (signing) certificate)
#        <CA name>-ca-key.pem (CA private key)
# Output: <cert name>-cert.pem (output certificate)
# Default cert name and CA name: pvfs2
# Generates signed certificate using cert. request and CA cert. 
# Requires serial number in <name>-ca.srl (even number of hex digits)
name=${1:-pvfs2}
caname=${2:-orangefs}
# create serial number file if necessary
if [ ! -f ${caname}-ca-cert.srl ]; then
    echo 03E8 > ${caname}-ca-cert.srl   # start at 1000
fi
openssl x509 -req -in ${name}-cert-req.pem -out ${name}-cert.pem \
    -CA ${caname}-ca-cert.pem -CAkey ${caname}-ca-cert-key.pem -days 365 \
    && echo "${name}-cert.pem created"

#!/bin/bash
# command used to generate self-signed root CA certificate based on settings in $1.cnf
# normally openssl req generates a request, using the -x509 option outputs a root certificate
name=${1:-pvfs2-test}
openssl req -outform PEM -out ${name}-ca.pem -new -x509 -config ${name}.cnf \
    -keyout ${name}-ca-key-pass.pem -days 1825
if [ $? -eq 0 ]; then
    echo "Created ${name}-ca.pem and ${name}-ca-key-pass.pem"
fi

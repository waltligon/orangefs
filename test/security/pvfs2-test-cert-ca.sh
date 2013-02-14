#!/bin/bash
# command used to generate self-signed root CA certificate based on settings in $1.cnf
# normally openssl req generates a request, using the -x509 option outputs a root certificate
name=${1:-pvfs2-test}
openssl req -outform PEM -out ${name}-ca.pem -new -x509 -config ${name}.cnf \
    -keyout ${name}-ca-key.pem -nodes -days 1825 \
    && echo "Created ${name}-ca.pem and ${name}-ca-key.pem"


#!/bin/bash
# command used to generate self-signed root CA certificate based on settings 
# in $1.cnf. Normally openssl req generates a request, using the -x509 option 
# outputs a root certificate

name=${1:-orangefs}
# Create CA certificate
openssl req -outform PEM -out ${name}-ca-cert.pem -new -x509 -config ${name}.cnf \
    -keyout ${name}-ca-cert-key.pem -nodes -days 1825 \
    && echo "Created ${name}-ca-cert.pem and ${name}-ca-cert-key.pem"


#!/bin/bash
# Generate a certificate request. OpenSSL prompts for inputs.
name=${1:-pvfs2}

openssl req -newkey rsa:1024 -keyout ${name}-cert-key.pem \
    -config ${name}-user.cnf -out ${name}-cert-req.pem -nodes \
    && echo "${name}-cert-key.pem and ${name}-cert-req.pem created"

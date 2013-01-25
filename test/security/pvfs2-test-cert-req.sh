#!/bin/bash
# Generate a certificate request. OpenSSL prompts for inputs.
name=${1:-pvfs2-test}
openssl req -newkey rsa:1024 -nodes \
    -subj "/DC=org/DC=orangefs/O=Security/OU=Testing/CN=${USER}/" \
    -keyout ${name}-cert-key.pem -out ${name}-cert-req.pem && \
    echo "${name}-cert-key.pem and ${name}-cert-req.pem created"

#!/bin/bash
# Generate a certificate request. OpenSSL prompts for inputs.
name=${1:-pvfs2-test}
openssl req -newkey rsa:1024 -passout pass:pvfs2pass \
    -subj "/DC=org/DC=orangefs/O=Security/OU=Testing/CN=${USER}/" \
    -keyout ${name}-cert-key-pass.pem -out ${name}-cert-req.pem && \
    echo "${name}-cert-key-pass.pem and ${name}-cert-req.pem created"

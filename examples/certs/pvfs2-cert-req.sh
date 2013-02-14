#!/bin/bash
# Generate a certificate request. OpenSSL prompts for inputs.
name=${1:-pvfs2}
# use user config file if available
if [ -f ${name}-user.cnf ]; then
    confopt="-config ${name}-user.cnf"
fi

openssl req -newkey rsa:1024 -keyout ${name}-cert-key.pem \
    -out ${name}-cert-req.pem -nodes $confopt \
    && echo "${name}-cert-key.pem and ${name}-cert-req.pem created"

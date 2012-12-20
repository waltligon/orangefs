#!/bin/bash

check ()
{
    ret=$?
    if [ $ret -ne 0 ]; then
        echo "$1"
        echo "Exiting..."
        exit $ret
    fi
}

# Create CA certificate
./pvfs2-test-cert-ca.sh
check "Error: could not create CA certificate"

# Generate user certificate request
./pvfs2-test-cert-req.sh
check "Error: could not generate certificate request"

# Generate and sign user certificate with CA
./pvfs2-test-cert-sign.sh
check "Error: could not sign user certificate"

# Verify user certificate
openssl verify -CAfile pvfs2-test-ca.pem pvfs2-test-cert.pem
check "Error: could not verify user certificate"

# Copy certificates to user home directory
if [ "x$PVFS2_TEST_CERT_NO_COPY" = "x" -a -d ~ ]; then
    cp pvfs2-test-cert.pem ~/.pvfs2-cert.pem 
    cp pvfs2-test-cert-key.pem ~/.pvfs2-cert-key.pem
    chmod 600 ~/.pvfs2-cert.pem ~/.pvfs2-cert-key.pem
fi



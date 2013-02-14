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

# Setup LDAP directory
./pvfs2-test-ldap-create-dir.sh
check "Error: could not create LDAP directory"

# Create non-root user
if [ "x$EUID" != "x0" ]; then
    # Compute LDAP container and admin dn
    hn=`hostname -f`
    base="dc=${hn/./,dc=}"
    admindn="cn=admin,${base}"
    container="ou=Users,${base}"

    ./pvfs2-test-ldap-add-user.sh -a "$admindn" -w "ldappwd" "$USER" "$container"
fi


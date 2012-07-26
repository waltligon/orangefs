#!/bin/sh
# This script generates a proxy certificate with a policy in the format
# of {UID}/{GID}, e.g. 1000/100. The certificate is stored in /tmp/x509up_u{UID},
# e.g. /tmp/x509up_u1000. This certificate is for use with the OrangeFS 
# Windows Client.
# 
# $GLOBUS_LOCATION must be set, or grid-proxy-init must be on the path.
# 
# Arguments to this script will be passed to grid-proxy-init.

echo `id -u`/`id -g` > cert-policy
if [ $? -ne 0 ]; then
    echo Could not create cert-policy, exiting
    exit 1
fi

if [ "$GLOBUS_LOCATION" != "" ]; then
    $GLOBUS_LOCATION/bin/grid-proxy-init -policy cert-policy -pl id-ppl-anyLanguage $@
else
    grid-proxy-init -policy cert-policy -pl id-ppl-anyLanguage $@
fi


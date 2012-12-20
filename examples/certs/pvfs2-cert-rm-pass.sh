#!/bin/bash
# removes passphrase from private key PEM file
if [ "x$1" = "x" -o "x$2" = "x" ]; then
    echo "USAGE: $0 <input key file> <output key file>"
	exit 1
fi
openssl rsa -in $1 -out $2 && echo "$2 created"

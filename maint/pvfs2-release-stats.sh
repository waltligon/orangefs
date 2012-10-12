#!/bin/sh

# a simple script to collect a few stats about a release:
# usage: $0 <tag1> <tag2>

if [ $# -ne 2 ] ; then
	echo "usage: $0 <tag1> <tag2>"
	exit -1
fi

patchsets=$(cvsps -q -r $1 -r $2 | grep PatchSet | wc -l)

echo "This release represents $patchsets changesets over ___"

diffstat=$(cvs diff -r$1 -r$2 | diffstat | tail -1)
echo "Diffstat reports: $diffstat"

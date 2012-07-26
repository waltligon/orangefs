#!/bin/tcsh

#########################################################
# Phil Carns, 4-2-03
#
# WARNING: THIS SCRIPT IS DANGEROUS!!!!
#
# Note: this script must be run from within the maint directory
#
# This script takes a target file and runs it through
# 3 scripts; one to fix quotation marks on includes, one to fix the
# indentation style, and one to tack on copyright headers and vim/emacs
# format footers.  It overwrites the input file with the output file.
#

if ( $# != 1 ) then
	echo "Usage: pvfs2-mangle-in-place.sh <filename>."
	exit 1
endif

if !(-f ${1} ) then
	echo "Error: could not access ${1}."
	exit 1
endif

if !(-f TEMPLATE_COPYRIGHT_HEADER.txt) then
	echo "Error: please run this script from within the maint directory of the pvfs2 source tree."
	exit 1
endif

if !(-f ../pvfs2-config.h.in) then
	echo "Error: please run this script from within the maint directory of the pvfs2 source tree."
	exit 1
endif

echo "PVFS2 mangling ${1}."

./pvfs2indent-80col.sh ${1} > /tmp/mangle1
./pvfs2-quote-includes.sh ../ /tmp/mangle1 > /tmp/mangle2
./pvfs2codecheck.pl --fix /tmp/mangle2 > /tmp/mangle3
cp /tmp/mangle3 ${1}

echo "...done."

exit 0

#!/bin/tcsh

###############################################
# Phil Carns, 4-2-03
# quick and dirty script to change 
#    #include <foo.h> 
# to
#    #include "foo.h"
# in the specified target file, where foo.h is a header that resides
# in the pvfs2 source tree.  It automatically generates the list of
# headers files from the source tree at run time.

# look for two arguments
if ( $# != 2 ) then
	echo "Usage: pvfs2-quote-includes.sh <pvfs2_source_tree> <target_file>."
	exit 1
endif

# make sure the first argument is a valid pvfs2 source tree
if !( -f ${1}/pvfs2-config.h.in ) then
	echo "Error: ${1} doesn't look like a pvfs2 source tree."
	exit 1
endif

# make sure the second argument exists
if !( -f ${2} ) then
	echo "Error: could not access ${2}."
	exit 1
endif

# grab list of headers (without path) from source tree
set HEADERS = `find ${1} -name "*.h" | awk 'BEGIN { FS = "/" } ; {print $NF}'`

# delete any old scratch file from previous runs
rm /tmp/quoterules.sed >& /dev/null

# dump a bunch of sed commands into a file
foreach header ( $HEADERS)
	echo "s/#include[[:space:]]*<[[:space:]]*${header}[[:space:]]*>/#include "'"'"${header}"'"'"/g" >> /tmp/quoterules.sed
end

# now nail the target file with those sed commands
sed -f /tmp/quoterules.sed ${2}

exit 0

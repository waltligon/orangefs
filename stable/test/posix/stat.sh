#!/bin/bash
E_OPTERR=65

usage()
{
	echo "Usage: `basename $0` -p <PVFS2 posix extensions path> -d <PVFS2 mount point>"
 	exit $E_OPTERR;
}

while getopts "p:d:" Option
do
	case $Option in
		p) PVFS2_TOPDIR=$OPTARG;;
		d) PVFS2_MNT=$OPTARG;;
		*) usage;;	
	esac
done

if [ -z "$PVFS2_TOPDIR" -o -z "$PVFS2_MNT" ]
then
	echo "Please specify PVFS2 top dir and mount point";
	usage;
fi

COUNT="10 100 250 500 1000 5000";

for c in ${COUNT}
do
	echo "Uncached: stat $c objects "
	$PVFS2_TOPDIR/test/posix/stat -f $PVFS2_MNT/$c

	echo "Cached: stat listing $c objects "
	$PVFS2_TOPDIR/test/posix/stat -f $PVFS2_MNT/$c

	echo "Uncached: stat-lite $c objects "
	$PVFS2_TOPDIR/test/posix/stat -l -f $PVFS2_MNT/$c

	echo "Cached: stat-lite $c objects "
	$PVFS2_TOPDIR/test/posix/stat -l -f $PVFS2_MNT/$c

done


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
	echo "Uncached: directory listing $c objects (getdents + stat) "
	$PVFS2_TOPDIR/test/posix/getdents -f $PVFS2_MNT/$c

	echo "Cached: directory listing $c objects (getdents + stat) "
	$PVFS2_TOPDIR/test/posix/getdents -f $PVFS2_MNT/$c

	echo "Uncached: (lite) directory listing $c objects (getdents + stat_lite) "
	$PVFS2_TOPDIR/test/posix/getdents -f $PVFS2_MNT/$c -l

	echo "Cached: (lite) directory listing $c objects (getdents + stat_lite) "
	$PVFS2_TOPDIR/test/posix/getdents -f $PVFS2_MNT/$c -l

	echo "Uncached: directory listing $c objects (getdents_plus) "
	$PVFS2_TOPDIR/test/posix/getdents -p -f $PVFS2_MNT/$c

	echo "Cached: directory listing $c objects (getdents_plus) "
	$PVFS2_TOPDIR/test/posix/getdents -p -f $PVFS2_MNT/$c

	echo "Uncached: (lite) directory listing $c objects (getdents_plus_lite) "
	$PVFS2_TOPDIR/test/posix/getdents -p -f $PVFS2_MNT/$c -l

	echo "Cached: (lite) directory listing $c objects (getdents_plus_lite) "
	$PVFS2_TOPDIR/test/posix/getdents -p -f $PVFS2_MNT/$c -l
done


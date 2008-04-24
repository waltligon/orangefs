#!/bin/sh 
#
# requires: 
#  expect
#  cvs (if pulling from CVS

if [ $DEBUG ] ; then
	set -xv
fi

rootdir=/tmp/pvfs2-build-test
srcdir=$rootdir/pvfs2
builddir=$rootdir/BUILD-pvfs2
installdir=$rootdir/INSTALL-pvfs2

# run the script that downloads, builds, and installs pvfs2
./pvfs2-build.sh -t
if [ $? -ne 0 ] ; then
	echo "Failed to build PVFS2."
	exit 1
fi

cd $builddir

# after installing, create a pvfs volume (PAV)
#  . start the volume
#  . load up some environment variables, exporting the important ones 
#          like PVFS2TAB_FILE

PAV_DIR=/test/common/pav
$srcdir/$PAV_DIR/pav_start -c $builddir/${PAV_DIR}/configfile.sample > $rootdir/pav-setup.log 2>&1

if [ $? -ne 0 ] ; then
	echo "Failed to start PAV. see $rootdir/pav-setup.log for details"
	exit 1
fi

eval $($srcdir/${PAV_DIR}/pav_info -c $builddir/${PAV_DIR}/configfile.sample)
export PVFS2TAB_FILE

# then run a test or set of tests (most likely PTS)
$builddir/src/apps/admin/pvfs2-ping -m $MOUNTPOINT >/dev/null 
if [ $? -eq 0 ] ; then 
	echo "Servers started successfully"
else 
	echo "Servers failed to start"
	exit 1
fi

# now do some testing. something trivial for now
$builddir/src/apps/admin/pvfs2-cp -t $builddir/src/apps/admin/pvfs2-cp $MOUNTPOINT/pvfs2-cp


# and clean up
$srcdir/$PAV_DIR/pav_stop -c $builddir/${PAV_DIR}/configfile.sample > $rootdir/pav-shutdown.log 2>&1 &&  echo "Script completed successfully." 

exit 0

#!/bin/sh 

rootdir=/tmp/pvfs2-build-test
kerneldir=fake

oldwd=`pwd`

usage()
{
    echo "USAGE: single-node-kernel-test.sh <-k kernel source> <-r dir>"
    echo "  -k: path to kernel source (enables module build)"
    echo "  -r: path to directory to build and install in"
    return
}

# get command line arguments
while getopts k:r: opt
do  
    case "$opt" in
        k) build_kernel="true"; kerneldir="$OPTARG";;
        r) rootdir="$OPTARG";;
        \?) usage; exit 1;;
    esac
done

if [ $kerneldir = "fake" ] ; then
	echo "No kernel path specified with -k; aborting."
	exit 1
fi

srcdir=$rootdir/pvfs2
builddir=$rootdir/BUILD-pvfs2

# run the script that downloads, builds, and installs pvfs2
cd ../../maint/build/
./pvfs2-build.sh -k $kerneldir -r $rootdir -t
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
export PVFSPORT

cd $oldwd
./kmod_ctrl.sh $rootdir start
if [ $? -ne 0 ] ; then
	echo "Failed to start PVFS2 kernel services."
	exit 1
fi

###################################################
# begin tests

echo "\nPVFS2-SHELL-TEST RESULTS ======================================"
bash $srcdir/test/kernel/linux-2.6/pvfs2-shell-test.sh $rootdir/INSTALL-pvfs2/mnt/pvfs2 > /tmp/pvfs2-shell-test.${USER} 2>&1
if [ $? -ne 0 ] ; then
	cat /tmp/pvfs2-shell-test.${USER}
	echo "*** pvfs2-shell-test.sh failed."
	exit 1
else
	# only show summary of output if it succeeds
	cat /tmp/pvfs2-shell-test.${USER} | grep PASS
fi

echo "[END] PVFS2-SHELL-TEST RESULTS ================================\n"


#echo "\nBONNIE++ RESULTS ======================================"
#./bonnie++.sh -d $rootdir/INSTALL-pvfs2/mnt/pvfs2/bonnie++ 
#if [ $? -ne 0 ] ; then
#	echo "*** bonnie++.sh failed."
#	exit 1
#fi
#
#echo "[END] BONNIE++ RESULTS ================================\n"


# end tests
###################################################

./kmod_ctrl.sh $rootdir stop
if [ $? -ne 0 ] ; then
	echo "Failed to stop PVFS2 kernel services."
	exit 1
fi
cd $builddir

# and clean up
$srcdir/$PAV_DIR/pav_stop -c $builddir/${PAV_DIR}/configfile.sample > $rootdir/pav-shutdown.log 2>&1 &&  echo "Script completed successfully." 

exit 0

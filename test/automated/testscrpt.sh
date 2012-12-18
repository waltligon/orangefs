#/bin/bash 

# i'm not married to bash. just wanted to get things prototyped

# prereqs: 
#   - $user needs to be in the sudoers file
#   - $user needs to be able to sudo w/o prompting
#   - please don't cheat and run this as root: will not catch permissions bugs

# set ENABLE_SECURITY to build with security
# set SECURITY_FAIL to test that security will fail - SSS

# you can override these settings in nightly-tests.cfg 
export PVFS2_DEST=/tmp/pvfs2-nightly
export PVFS2_MOUNTPOINT=/pvfs2-nightly
export EXTRA_TESTS=/tmp/${USER}/src/benchmarks
#export EXTRA_TESTS=/tmp/src/benchmarks
export URL=http://devorange.clemson.edu/pvfs
export BENCHMARKS=benchmarks-20121017.tar.gz

# look for a 'nightly-test.cfg' in the same directory as this script
if [ -f /tmp/$USER/nightly-tests.cfg ] ; then 
	. /tmp/$USER/nightly-tests.cfg
fi


# need to make this a command line arugment:
export CVS_TAG="${CVS_TAG:-HEAD}"

if [ $ENABLE_SECURITY ] ; then
	sec_dir=${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/etc
fi

export CVS_TAG_FULL="${CVS_TAG:-HEAD}"
export CVS_TAG=`echo $CVS_TAG_FULL | awk -F"/" '{print $NF}'`
# no need to modify these. they make their own gravy
STARTTIME=`date +%s`
TINDERSCRIPT=$(cd `dirname $0`; pwd)/tinder-pvfs2-status
#SYSINT_SCRIPTS=~+/sysint-tests.d
SYSINT_SCRIPTS=`pwd`/sysint-tests.d
VFS_SCRIPTS=`pwd`/vfs-tests.d
#VFS_SCRIPTS=~+/vfs-tests.d
VFS_SCRIPT="dbench"
MPIIO_DRIVER=${PVFS2_DEST}/pvfs2-${CVS_TAG}/test/automated/testscrpt-mpi.sh
REPORT_LOG=${PVFS2_DEST}/alltests-${CVS_TAG}.log
REPORT_ERR=${PVFS2_DEST}/alltests-${CVS_TAG}.err
BENCHMARKS=benchmarks-20121017.tar.gz

# for debugging and testing, you might need to set the above to your working
# direcory.. .unless you like checking in broken scripts
#SYSINT_SCRIPTS=$(cd `dirname $0`; pwd)/sysint-tests.d
#VFS_SCRIPTS=$(cd `dirname $0`; pwd)/vfs-tests.d
MPIIO_DRIVER=$(cd `dirname $0`; pwd)/testscrpt-mpi.sh

HOSTNAME=`hostname -s 2> /dev/null || hostname`
TESTNAME="${HOSTNAME}-nightly"

# before starting any client apps, we need to deal with the possiblity that we
# might have built with shared libraries
export LD_LIBRARY_PATH=${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib:${LD_LIBRARY_PATH}

# we only have a few hosts that meet all the earlier stated prereqs
VFS_HOSTS="`hostname`"
#VFS_HOSTS="badname"
#
# Detect basic heap corruption
#
export MALLOC_CHECK_=2

. ${PVFS2_DEST}/pvfs2-${CVS_TAG}/test/automated/testfunctions.sh
###
### entry point for script
###

# show that we're doing something
${TINDERSCRIPT} ${TESTNAME}-${CVS_TAG} building $STARTTIME </dev/null

# will we be able to do VFS-related tests?
do_vfs=0
for s in $(echo $VFS_HOSTS); do
	if [ ${HOSTNAME} = $s ] ; then
		do_vfs=1
		break
	fi
done


if [ ! $RUN_VFS_TEST ] 
then
	do_vfs=0;
fi

# "install" benchmark software, if EXTRA_TESTS is not null
if [ $EXTRA_TESTS ] 
then
   echo "Installing benchmark software...."
   my_cwd=`pwd`

   #create directory, if not already there
   mkdir -p $EXTRA_TESTS
   if [ $? != 0 ]
   then
      echo "benchmarks: mkdir failed"
      setupfail
   fi

   #remove existing tar file and/or subdirectories
   cd $EXTRA_TESTS/..
   sudo /bin/rm -rf *

   #get new tar file
   wget ${URL}/${BENCHMARKS}
   if [ $? != 0 ]
   then
      echo "benchmarks: wget failed"
      setupfail
   fi

   #untar the file
   tar -xzf ${BENCHMARKS}
   if [ $? != 0 ]
   then
      echo "benchmarks: tar failed"
      setupfail
   fi

   #go back to original working directory
   cd $my_cwd
fi

echo "pull_and_build_pvfs2"
pull_and_build_pvfs2  $CVS_TAG_FULL || buildfail

echo "setup_pvfs2"
teardown_pvfs2 && setup_pvfs2 

if [ $? != 0 ] ; then
	echo "setup failed"
	setupfail
fi

echo "Run MPI test is $RUN_MPI_TEST"
echo "Run VFS test is $RUN_VFS_TEST"
#echo "do_vfs is $do_vfs"

if [ $do_vfs -eq 1 ] ; then 
	echo "setup_vfs"
	teardown_vfs && setup_vfs

	if [ $? != 0 ] ; then
		echo "setup failed"
		setupfail
	fi
fi

# at this point we've got 
# - pvfs2 servers running
# on hosts in our whitelist
# - an entry in /etc/fstab (that was a prerequisite for this script after all)
# - the VFS mounted at $PVFS2_MOUNTPOINT

echo "Checking if pvfs is running"

ps aux | grep pvfs

echo "Checking mount"
mount 

nr_passed=0
nr_failed=0

# save file descriptors for later
exec 6<&1
exec 7<&2

exec 1> ${REPORT_LOG}
exec 2> ${REPORT_ERR}

# print current environment to env.log
env > env.log

echo "running sysint scripts"
run_parts ${SYSINT_SCRIPTS}

if [ $do_vfs -eq 1 ] ; then
	echo ""
	echo "running vfs scripts"
	export VFS_SCRIPTS
	run_parts ${VFS_SCRIPTS}
#        run_one ${VFS_SCRIPTS} ${VFS_SCRIPT}
fi

# down the road (as we get our hands on more clusters) we'll need a more
# generic way of submitting jobs. for now assume all the world has pbs

if [ $RUN_MPI_TEST ]
then
	which qsub >/dev/null 2>&1
	if [ $? -eq 0 ] ; then
		echo ""
		#echo "Found qsub at `which qsub`"
		echo "running mpi scripts"
		# go through the hassle of downloading/building mpich2 only if we are
		# actually going to use it
		pull_and_build_mpich2 || buildfail
		source $MPIIO_DRIVER
		. $MPIIO_DRIVER
	fi
fi

# restore file descriptors and close temporary fds
exec 1<&6 6<&-
exec 2<&7 7<&-

if [ -f $PVFS2_DEST/pvfs2-built-with-warnings -o \
	-f ${PVFS2_DEST}/pvfs2-test-built-with-warnings ] ; then
	tinder_report successwarn
	rm -f $PVFS2_DEST/pvfs2-built-with-warnings
	rm -f ${PVFS2_DEST}/pvfs2-test-built-with-warnings

elif [ $nr_failed -gt 0 ]; then
	tinder_report test_failed
else
	tinder_report success
fi

[ $do_vfs -eq 1 ] && teardown_vfs
teardown_pvfs2

#/bin/bash 

. `pwd`/testfunctions.sh
###
### entry point for script
###

# show that we're doing something
${TINDERSCRIPT} ${TESTNAME}-${CVS_TAG} building $STARTTIME </dev/null

# will we be able to do VFS-related tests?
do_vfs=0
echo "Looking for host ${HOSTNAME}, in VFS_HOSTS list ${VFS_HOSTS}"

for s in ${VFS_HOSTS}; do
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
teardown_pvfs2 && configure_pvfs2 

for my_host in $VFS_HOSTS
do
	copy_pvfs2 $my_host &
done
wait

for my_host in $VFS_HOSTS
do
	start_all_pvfs2 $my_host &
done
wait

if [ $? != 0 ] ; then
	echo "setup failed"
	setupfail
fi

export RUN_USERLIB_TEST=1

echo "Run Userlib test is $RUN_USERLIB_TEST"
echo "Run VFS test is $RUN_VFS_TEST"
echo "Run MPI test is $RUN_MPI_TEST"


# at this point we've got 
# - pvfs2 servers running
# on hosts in our whitelist
# - an entry in /etc/fstab (that was a prerequisite for this script after all)
# - the VFS mounted at $PVFS2_MOUNTPOINT

echo "Checking if pvfs2 is running. Client should NOT be running at this point."

ps aux | grep pvfs



nr_passed=0
nr_failed=0

# save file descriptors for later
exec 6<&1
exec 7<&2

exec 1> ${REPORT_LOG}
exec 2> ${REPORT_ERR}

# print current environment to env.log
env > env.log

if [ $RUN_USERLIB_TEST ]
then
	OLD_LD_PRELOAD=$LD_PRELOAD
	if [ $LD_PRELOAD ]
	then
		export LD_PRELOAD=${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib/libofs.so:${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib/libpvfs2.so:$LD_PRELOAD
	else
		export LD_PRELOAD=${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib/libofs.so:${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib/libpvfs2.so
	fi
	echo ""
	echo "running userlib scripts"
	run_parts ${USERLIB_SCRIPTS}
	LD_PRELOAD=$OLD_LD_PRELOAD
fi




if [ $do_vfs -eq 1 ] ; then
	# restore file descriptors and close temporary fds
	exec 1<&6 6<&-
	exec 2<&7 7<&-

	echo "setup_vfs"
	teardown_vfs && setup_vfs

	if [ $? != 0 ] ; then
		echo "setup failed"
		setupfail
	fi
	echo "Checking if pvfs2 client is running"
	ps aux | grep pvfs
	echo "Checking mount"
	mount 
	# save file descriptors for later
	exec 6<&1
	exec 7<&2
	
	# Add output to the end of the log.
	exec 1>> ${REPORT_LOG}
	exec 2>> ${REPORT_ERR}

	echo "running sysint scripts"
	run_parts ${SYSINT_SCRIPTS}

	echo ""
	echo "running vfs scripts"
	export VFS_SCRIPTS
	run_parts ${VFS_SCRIPTS}
#        run_one ${VFS_SCRIPTS} ${VFS_SCRIPT}
else	
	echo "Unable to do VFS test for ${VFS_HOSTS}"
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

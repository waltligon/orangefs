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

TESTNAME="`hostname -s`-nightly"

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

# takes one argument: a tag or branch in CVS
pull_and_build_pvfs2 () {
	# debugging aide... when we run this script repeatedly, we don't 
	# really need to build everything again 
	[ -n "$SKIP_BUILDING_PVFS2" ] && return 0

	mkdir -p $PVFS2_DEST
	with_kernel=""
	if  [ $do_vfs -eq 1 ] ; then
		with_kernel="-k /lib/modules/`uname -r`/build"
	fi
   with_security=""
   if [ $ENABLE_SECURITY ] ; then
      with_security="-s"
   fi
	# a bit of gross shell hackery, but cuts down on the number of
	# variables we have to set.  Assumes we ran this script out of a
	# checked out pvfs2 tree
	$(cd `dirname $0`;pwd)/../../maint/build/pvfs2-build.sh -t -v $1 \
		$with_kernel $with_security -r $PVFS2_DEST
	
}

pull_and_build_mpich2 () {
	# just to make debugging less painful
	[ -n "${SKIP_BUILDING_MPICH2}" ] && return 0
	[ -d ${PVFS2_DEST} ] || mkdir ${PVFS2_DEST}
	cd ${PVFS2_DEST}
	rm -rf mpich2-*.tar.gz
	#wget http://www.mcs.anl.gov/research/projects/mpich2/downloads/tarballs/1.5/mpich2-1.5.tar.gz
	wget --quiet http://devorange.clemson.edu/pvfs/mpich2-1.5.tar.gz
	#wget --passive-ftp --quiet 'ftp://ftp.mcs.anl.gov/pub/mpi/misc/mpich2snap/mpich2-snap-*' -O mpich2-latest.tar.gz
	rm -rf mpich2-snap-*
	#tar xzf mpich2-latest.tar.gz
	tar xzf mpich2-1.5.tar.gz
	mv mpich2-1.5 mpich2-snapshot
	cd mpich2-snapshot
	mkdir build
	cd build
	../configure -q --prefix=${PVFS2_DEST}/soft/mpich2 \
		--enable-romio --with-file-system=ufs+nfs+testfs+pvfs2 \
		--with-pvfs2=${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG} \
		--enable-g=dbg --without-mpe \
		--disable-f77 --disable-fc >mpich2config-${CVS_TAG}.log &&\
	make > mpich2make-$CVSTAG.log 2> /dev/null && make install > mpich2install-${CVSTAG} 2> /dev/null
}


teardown_vfs() {
	mount | grep -q $PVFS2_MOUNTPOINT && sudo umount $PVFS2_MOUNTPOINT
	ps -e | grep -q pvfs2-client && sudo killall pvfs2-client
	sleep 1
	/sbin/lsmod | grep -q pvfs2 && sudo /sbin/rmmod pvfs2
	# let teardown always succeed.  pvfs2-client might already be killed
	# and pvfs2 kernel module might not be loaded yet 
	return 0
}

setup_vfs() {
	sudo dmesg -c >/dev/null
	sudo /sbin/insmod ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib/modules/`uname -r`/kernel/fs/pvfs2/pvfs2.ko
#	sudo LD_LIBRARY_PATH=${LD_LIBRARY_PATH} ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client \
#		-p ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client-core \
#		-L ${PVFS2_DEST}/pvfs2-client-${CVS_TAG}.log
	# sudo screen -d -m cgdb -x ${PVFS2_DEST}/.gdbinit --args ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client-core -L ${PVFS2_DEST}/pvfs2-client-${CVS_TAG}.log
	#sudo valgrind --log-file=${PVFS2_DEST}/pvfs2-client.vg ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client-core -L ${PVFS2_DEST}/pvfs2-client-${CVS_TAG}.log &
	keypath=""
	if [ $ENABLE_SECURITY ] ; then
		keypath="--keypath ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/etc/clientkey.pem"
	fi
	sudo ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client \
		-p ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client-core \
		-L ${PVFS2_DEST}/pvfs2-client-${CVS_TAG}.log \
		$keypath
	sudo chmod 644 ${PVFS2_DEST}/pvfs2-client-${CVS_TAG}.log
	echo "Mounting pvfs2 service at tcp://`hostname -s`:3399/pvfs2-fs at mountpoint $PVFS2_MOUNTPOINT"
	sudo mount -v -t pvfs2 tcp://`hostname -s`:3399/pvfs2-fs ${PVFS2_MOUNTPOINT}
	if [ $? -ne 0 ]
	then
		echo "Something has gone wrong. Mount failed."
	fi
}

check_openssl() {
	if [ $? -ne 0 ] ; then
		echo "OpenSSL error:" 1>&2
		cat ${sec_dir}/error.tmp 1>&2
	fi
}

setup_security() {
	echo "....initializing security"
	which openssl > /dev/null 2>&1
	if [ $? -ne 0 ] ; then
		echo "openssl must be installed for security mode"
		return 1
	fi
	mkdir -p $sec_dir
	# remove existing files
	rm -f ${sec_dir}/*
	# generate client private key
	openssl genrsa -out ${sec_dir}/clientkey.pem 1024 > /dev/null \
		2> ${sec_dir}/error.tmp
	check_openssl
	for alias in `grep 'Alias ' fs.conf | cut -d ' ' -f 2`; do
		# output client public key to keystore, unless security 
		# is intended to fail
		if [ ! $SECURITY_FAIL ] ; then
			echo "C:`hostname -s`" >> ${sec_dir}/keystore-${alias}
			openssl rsa -in ${sec_dir}/clientkey.pem -pubout >> \
				${sec_dir}/keystore-${alias} 2> ${sec_dir}/error.tmp
			check_openssl
		fi
		# generate server private key
		openssl genrsa -out ${sec_dir}/serverkey-${alias}.pem 2048 \
			> /dev/null 2> ${sec_dir}/error.tmp
		check_openssl
		# output server public key to keystore files
		for keystore_alias in `grep 'Alias ' fs.conf | cut -d ' ' -f 2`; do
			echo "S:${alias}" >> ${sec_dir}/keystore-${keystore_alias}
			openssl rsa -in ${sec_dir}/serverkey-${alias}.pem \
				-pubout >> ${sec_dir}/keystore-${keystore_alias} \
				2> ${sec_dir}/error.tmp
			check_openssl
		done
	done
	# set client key location
	export PVFS2KEY_FILE=${sec_dir}/clientkey.pem
	rm -f ${sec_dir}/error.tmp
}

setup_pvfs2() {
	cd $PVFS2_DEST
	rm -f fs.conf 
	sec_args=""
	if [ $ENABLE_SECURITY ] ; then
		sec_args="--keystore=${sec_dir}/keystore-_ALIAS_ "
		sec_args+="--serverkey=${sec_dir}/serverkey-_ALIAS_.pem"
	fi
	INSTALL-pvfs2-${CVS_TAG}/bin/pvfs2-genconfig fs.conf \
		--protocol tcp \
		--iospec="`hostname -s`:{3396-3399}" \
		--metaspec="`hostname -s`:{3396-3399}"  \
		--storage ${PVFS2_DEST}/STORAGE-pvfs2-${CVS_TAG} \
		$sec_args \
		--logfile=${PVFS2_DEST}/pvfs2-server-${CVS_TAG}.log --quiet
	# generate security keys
	if [ $ENABLE_SECURITY ] ; then
		setup_security
		if [ $? -ne 0 ] ; then
			return 1
		fi
	fi
	# clean up any artifacts from earlier runs
	rm -rf ${PVFS2_DEST}/STORAGE-pvfs2-${CVS_TAG}*
	rm -f ${PVFS2_DEST}/pvfs2-server-${CVS_TAG}.log* 
	failure_logs="${PVFS2_DEST}/pvfs2-server-${CVS_TAG}.log* $failure_logs"
	for alias in `grep 'Alias ' fs.conf | cut -d ' ' -f 2`; do
		INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-server \
			-p `pwd`/pvfs2-server-${alias}.pid \
			-f fs.conf -a $alias
		INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-server \
			-p `pwd`/pvfs2-server-${alias}.pid  \
			fs.conf $server_conf -a $alias
	done

	echo "tcp://`hostname -s`:3399/pvfs2-fs ${PVFS2_MOUNTPOINT} pvfs2 defaults 0 0" > ${PVFS2_DEST}/pvfs2tab
	# do we need to use our own pvfs2tab file?  If we will mount pvfs2, we
	# can fall back to /etc/fstab
	grep -q 'pvfs2-nightly' /etc/fstab
	if [ $? -ne 0 -a $do_vfs -eq 0 ] ; then
		export PVFS2TAB_FILE=${PVFS2_DEST}/pvfs2tab
	fi
	#turn on debugging on each server
	export PVFS2TAB_FILE=${PVFS2_DEST}/pvfs2tab
	echo "....setting server-side debug mask"
	INSTALL-pvfs2-${CVS_TAG}/bin/pvfs2-set-debugmask -m ${PVFS2_MOUNTPOINT} "all"	
}

teardown_pvfs2() {
	for pidfile in ${PVFS2_DEST}/pvfs2-server*.pid ; do
		[ ! -f $pidfile ] && continue

		kill `cat $pidfile`
		# occasionally the server ends up in a hard-to-kill state.
		# server has atexit(3) remove .pid file
		sleep 3
		if [ -f $pidfile ] ; then 
			kill -9 `cat $pidfile`
		fi
	done

	# let teardown always succeed.  pvfs2-server.pid could be stale
	return 0
}

buildfail() {
	echo "Failure in build process"
	cat ${PVFS2_DEST}/configure-${CVS_TAG}.log \
		${PVFS2_DEST}/make-extracted-${CVS_TAG}.log \
		${PVFS2_DEST}/make-install-${CVS_TAG}.log \
		${PVFS2_DEST}/make-${CVS_TAG}.log \
		${PVFS2_DEST}/make-test-${CVS_TAG}.log | \
		${TINDERSCRIPT} ${TESTNAME}-${CVS_TAG} build_failed $STARTTIME 
	exit 1
}

setupfail() {
	echo "Failure in setup"
	dmesg > ${PVFS2_DEST}/dmesg
	cat ${PVFS2_DEST}/dmesg ${PVFS2_DEST}/pvfs2-client-${CVS_TAG}.log \
		${PVFS2_DEST}/pvfs2-server-${CVS_TAG}.log* | \
		${TINDERSCRIPT}  ${TESTNAME}-${CVS_TAG} test_failed $STARTTIME 
	exit 1
}

tinder_report() {
	eval cat $REPORT_LOG $failure_logs |\
		${TINDERSCRIPT} ${TESTNAME}-${CVS_TAG} $1 $STARTTIME \
		"$nr_failed of $(( $nr_failed + $nr_passed)) failed"
}

testfail() {
	echo "Failure in testing"
	tinder_report test_failed
	exit 1
}

# idea stolen from debian: for a given directory, run every executable file
run_parts() {
	cd $1
	for f in *; do
		# skip CVS
		[ -d $f ] && continue
		if [ -x $f ] ; then 
			echo -n "====== `date` == running $f ..."
			./$f > ${PVFS2_DEST}/${f}-${CVS_TAG}.log
			if [ $? -eq 0 ] ; then 
				nr_passed=$((nr_passed + 1))
				echo "OK"
			else
				nr_failed=$((nr_failed + 1))
				failure_logs="$failure_logs ${PVFS2_DEST}/${f}-${CVS_TAG}.log"
				echo "FAILED"
			fi
		fi
	done
}

#run only one script
run_one() {
   cd $1
   echo -n "===== `date` == running ${1}/${2} ..."
   ${1}/${2} > ${PVFS2_DEST}/${2}-${CVS_TAG}.log
   if [ $? -eq 0 ] ; then 
      nr_passed=$((nr_passed + 1))
      echo "OK"
   else
      nr_failed=$((nr_failed + 1))
      failure_logs="$failure_logs ${PVFS2_DEST}/${2}-${CVS_TAG}.log"
      echo "FAILED"
   fi
}
###
### entry point for script
###

# show that we're doing something
${TINDERSCRIPT} ${TESTNAME}-${CVS_TAG} building $STARTTIME </dev/null

# will we be able to do VFS-related tests?
do_vfs=0
for s in $(echo $VFS_HOSTS); do
	if [ `hostname -s` = $s ] ; then
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
echo "do_vfs is $do_vfs"

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

nr_passed=0
nr_failed=0

# save file descriptors for later
exec 6<&1
exec 7<&2

exec 1> ${REPORT_LOG}
exec 2> ${REPORT_ERR}
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

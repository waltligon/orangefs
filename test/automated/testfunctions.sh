# #!/bin/bash
# testfunction - list of common functions for test programs
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
USERLIB_SCRIPTS=`pwd`/userint-tests.d
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
#export LD_LIBRARY_PATH=${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib:${LD_LIBRARY_PATH}

# we only have a few hosts that meet all the earlier stated prereqs
if [ ! "$VFS_HOSTS" ]
then
VFS_HOSTS=$HOSTNAME
fi
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
	# v-ariables we have to set.  Assumes we ran this script out of a
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
	#sudo dmesg -c >/dev/null
	sudo /sbin/insmod ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib/modules/`uname -r`/kernel/fs/pvfs2/pvfs2.ko &> pvfs2-kernel-module.log
	sudo /sbin/lsmod >> pvfs2-kernel-module.log
#	sudo LD_LIBRARY_PATH=${LD_LIBRARY_PATH} ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client \
#		-p ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client-core \
#		-L ${PVFS2_DEST}/pvfs2-client-${CVS_TAG}.log
	# sudo screen -d -m cgdb -x ${PVFS2_DEST}/.gdbinit --args ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client-core -L ${PVFS2_DEST}/pvfs2-client-${CVS_TAG}.log
	#sudo valgrind --log-file=${PVFS2_DEST}/pvfs2-client.vg ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client-core -L ${PVFS2_DEST}/pvfs2-client-${CVS_TAG}.log &
	keypath=""
	if [ $ENABLE_SECURITY ] ; then
		keypath="--keypath ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/etc/clientkey.pem"
	fi
	sudo LD_LIBRARY_PATH=${LD_LIBRARY_PATH} ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client \
		-p ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client-core \
		-L ${PVFS2_DEST}/pvfs2-client-${CVS_TAG}.log \
		$keypath
	sudo chmod 644 ${PVFS2_DEST}/pvfs2-client-${CVS_TAG}.log
	echo "Mounting pvfs2 service at tcp://${HOSTNAME}:3396/orangefs at mountpoint $PVFS2_MOUNTPOINT"
	sudo mount -t pvfs2 tcp://${HOSTNAME}:3396/orangefs ${PVFS2_MOUNTPOINT}
	
		if [ $? -ne 0 ]
	then
		echo "Something has gone wrong. Mount failed."
	fi
	mount > allmount.log
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
			echo "C:${HOSTNAME}" >> ${sec_dir}/keystore-${alias}
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


configure_pvfs2() {

	echo "PVFS2_DEST is $PVFS2_DEST"
	cd $PVFS2_DEST
	mkdir mount
	rm -f fs.conf 
	sec_args=""
	if [ $ENABLE_SECURITY ] ; then
		sec_args="--keystore=${sec_dir}/keystore-_ALIAS_ "
		sec_args+="--serverkey=${sec_dir}/serverkey-_ALIAS_.pem"
	fi
	MY_VFS_HOSTS=`echo $VFS_HOSTS | sed s/' '/':3396,'/g`
	INSTALL-pvfs2-${CVS_TAG}/bin/pvfs2-genconfig fs.conf \
		--protocol tcp \
		--iospec="${MY_VFS_HOSTS}:3396" \
		--metaspec="${MY_VFS_HOSTS}:3396"  \
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

}	

start_pvfs2() {

	# clean up any artifacts from earlier runs
	cd ${PVFS2_DEST}
	rm -rf ${PVFS2_DEST}/STORAGE-pvfs2-${CVS_TAG}*
	rm -f ${PVFS2_DEST}/pvfs2-server-${CVS_TAG}.log* 
	failure_logs="${PVFS2_DEST}/pvfs2-server-${CVS_TAG}.log* $failure_logs"
	for alias in `grep 'Alias ' fs.conf | grep ${HOSTNAME} | cut -d ' ' -f 2`; do
		${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-server \
			-p `pwd`/pvfs2-server-${alias}.pid \
			-f fs.conf -a $alias
		${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-server \
			-p `pwd`/pvfs2-server-${alias}.pid  \
			fs.conf $server_conf -a $alias
	done

        # give the servers time to finish all their initialization tasks
        sleep 10

		echo "tcp://${HOSTNAME}:3396/orangefs ${PVFS2_MOUNTPOINT} pvfs2 defaults 0 0" > ${PVFS2_DEST}/pvfs2tab
	# do we need to use our own pvfs2tab file?  If we will mount pvfs2, we
	# can fall back to /etc/fstab
	grep -q 'pvfs2-nightly' /etc/fstab
	#if [ $? -ne 0 -a $do_vfs -eq 0 ] ; then
	#	export PVFS2TAB_FILE=${PVFS2_DEST}/pvfs2tab
	#fi
	export PVFS2TAB_FILE=${PVFS2_DEST}/pvfs2tab
	#turn on debugging on each server
	if [ $SERVER_DEBUG_PARAMS ] ; then
		echo "....setting server-side debug mask to $SERVER_DEBUG_PARAMS"
		INSTALL-pvfs2-${CVS_TAG}/bin/pvfs2-set-debugmask -m ${PVFS2_MOUNTPOINT} $SERVER_DEBUG_PARAMS
	fi
}

setup_pvfs2() {
	configure_pvfs2 && start_pvfs2

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
	echo "Running $1 Starting from `pwd`"
	cd $1
	echo "Currently at `pwd`"
	TESTS=$(basename `pwd`)
	
	for f in *; do
		# skip CVS
		[ -d $f ] && continue
		if [ -x $f ] ; then 
			echo -n "====== `date` == running $f ..."
			./$f > ${PVFS2_DEST}/${TESTS}-${f}-${CVS_TAG}.log
			if [ $? -eq 0 ] ; then 
				nr_passed=$((nr_passed + 1))
				echo "OK"
			else
				nr_failed=$((nr_failed + 1))
				failure_logs="$failure_logs ${PVFS2_DEST}/${TESTS}-${f}-${CVS_TAG}.log"
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

copy_pvfs2() {
#$1 is the vfs server
my_host=$1

if [ ! $KEYFILE ]
then
	#A bit naive, but there should only be one keyfile in the home directory.
	KEYFILE=`ls ~/*.pem`
fi
VMUSER=`basename ~`


	# verify /home/${VMUSER}/${KEYFILESHORT} exists

	if [ $my_host != ${HOSTNAME} ]
	then
		echo  "Copying PVFS2... to $my_host"
		echo "ssh -i ${KEYFILE} ${VMUSER}@${my_host} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no \"mkdir -p ${PVFS2_DEST}\""
		ssh -i ${KEYFILE} ${VMUSER}@${my_host} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no "mkdir -p ${PVFS2_DEST}"	
		
		echo "rsync -a -e \"ssh -i ${KEYFILE} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no\" ${PVFS2_DEST}/ ${VMUSER}@${my_host}:${PVFS2_DEST}"
		rsync -a -e "ssh -i ${KEYFILE} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no" ${PVFS2_DEST}/ ${VMUSER}@${my_host}:${PVFS2_DEST} 
		
	fi


}

start_all_pvfs2() {

	#$1 is the vfs server

	my_host=$1

	if [ ! $KEYFILE ]
	then
		#A bit naive, but there should only be one keyfile in the home directory.
		KEYFILE=`ls ~/*.pem`
	fi
	VMUSER=`basename ~`


	if [ $my_host != ${HOSTNAME} ]
	then

	echo  "Starting PVFS2 on $my_host"
	echo "ssh -i ${KEYFILE} ${VMUSER}@${my_host} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no \"cd ${PVFS2_DEST}/pvfs2-${CVS_TAG}/test/automated/ && ./start_pvfs2.sh\""
	ssh -i ${KEYFILE} ${VMUSER}@${my_host} -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no "cd ${PVFS2_DEST}/pvfs2-${CVS_TAG}/test/automated/ && ./start_pvfs2.sh $PVFS2_DEST $CVS_TAG "	
		
	else
		echo  "Starting PVFS2 on $my_host"
		start_pvfs2
	fi

}

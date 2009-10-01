#!/bin/sh

# i'm not married to bash. just wanted to get things prototyped

# prereqs: 
#   - $user needs to be in the sudoers file
#   - $user needs to be able to sudo w/o prompting
#   - please don't cheat and run this as root: will not catch permissions bugs

# you can override these settings in nightly-tests.cfg 
export PVFS2_DEST=/tmp/pvfs2-nightly
export PVFS2_MOUNTPOINT=/pvfs2-nightly
export EXTRA_TESTS=${HOME}/src/benchmarks

# look for a 'nightly-test.cfg' in the same directory as this script
if [ -f $(cd `dirname $0`; pwd)/nightly-tests.cfg ] ; then 
	. $(cd `dirname $0`; pwd)/nightly-tests.cfg
fi


# need to make this a command line arugment:
export CVS_TAG="${CVS_TAG:-HEAD}"

# no need to modify these. they make their own gravy
STARTTIME=`date +%s`
TINDERSCRIPT=$(cd `dirname $0`; pwd)/tinder-pvfs2-status
SYSINT_SCRIPTS=${PVFS2_DEST}/pvfs2-${CVS_TAG}/test/automated/sysint-tests.d
VFS_SCRIPTS=${PVFS2_DEST}/pvfs2-${CVS_TAG}/test/automated/vfs-tests.d
MPIIO_DRIVER=${PVFS2_DEST}/pvfs2-${CVS_TAG}/test/automated/testscrpt-mpi.sh
REPORT_LOG=${PVFS2_DEST}/alltests-${CVS_TAG}.log

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
VFS_HOSTS="gil lain stan"

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
	# a bit of gross shell hackery, but cuts down on the number of
	# variables we have to set.  Assumes we ran this script out of a
	# checked out pvfs2 tree
	$(cd `dirname $0`;pwd)/../../maint/build/pvfs2-build.sh -t -v $1 \
		$with_kernel -r $PVFS2_DEST
	
}

pull_and_build_mpich2 () {
	# just to make debugging less painful
	[ -n "${SKIP_BUILDING_MPICH2}" ] && return 0
	[ -d ${PVFS2_DEST} ] || mkdir ${PVFS2_DEST}
	cd ${PVFS2_DEST}
	rm -rf mpich2-latest.tar.gz
	wget --passive-ftp --quiet 'ftp://ftp.mcs.anl.gov/pub/mpi/misc/mpich2snap/mpich2-snap-*' -O mpich2-latest.tar.gz
	rm -rf mpich2-snap-*
	tar xzf mpich2-latest.tar.gz
	cd mpich2-snap-*
	mkdir build
	cd build
	../configure -q --prefix=${PVFS2_DEST}/soft/mpich2 \
		--enable-romio --with-file-system=ufs+nfs+testfs+pvfs2 \
		--with-pvfs2=${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG} \
		--enable-g=dbg --without-mpe \
		--disable-f77 >mpich2config-${CVS_TAG}.log &&\
	make >/dev/null && make install >/dev/null 
}


teardown_vfs() {
	sudo umount $PVFS2_MOUNTPOINT
	sudo killall pvfs2-client
	sleep 1
	sudo /sbin/rmmod  pvfs2
	# let teardown alway ssucceed.  pvfs2-client might already be killed
	# and pvfs2 kernel module might not be loaded yet 
	return 0
}

setup_vfs() {
	sudo dmesg -c >/dev/null
	sudo /sbin/insmod ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/lib/modules/`uname -r`/kernel/fs/pvfs2/pvfs2.ko
	sudo LD_LIBRARY_PATH=${LD_LIBRARY_PATH} ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client \
		-p ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client-core \
		-L ${PVFS2_DEST}/pvfs2-client-${CVS_TAG}.log
	# sudo screen -d -m cgdb -x ${PVFS2_DEST}/.gdbinit --args ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client-core -L ${PVFS2_DEST}/pvfs2-client-${CVS_TAG}.log
	#sudo valgrind --log-file=${PVFS2_DEST}/pvfs2-client.vg ${PVFS2_DEST}/INSTALL-pvfs2-${CVS_TAG}/sbin/pvfs2-client-core -L ${PVFS2_DEST}/pvfs2-client-${CVS_TAG}.log &
	sudo chmod 644 ${PVFS2_DEST}/pvfs2-client-${CVS_TAG}.log
	sudo mount -t pvfs2 tcp://`hostname -s`:3399/pvfs2-fs ${PVFS2_MOUNTPOINT}
}

setup_pvfs2() {
	cd $PVFS2_DEST
	rm -f fs.conf 
	INSTALL-pvfs2-${CVS_TAG}/bin/pvfs2-genconfig fs.conf \
		--protocol tcp \
		--iospec="`hostname -s`:{3396-3399}" \
		--metaspec="`hostname -s`:{3396-3399}"  \
		--storage ${PVFS2_DEST}/STORAGE-pvfs2-${CVS_TAG} \
		--logfile=${PVFS2_DEST}/pvfs2-server-${CVS_TAG}.log --quiet
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
			echo -n "====== running $f ..."
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

failure_logs=""   # a space-delimited list of logs that failed
# compile and install
pull_and_build_pvfs2  $CVS_TAG || buildfail

teardown_pvfs2 && setup_pvfs2 

if [ $? != 0 ] ; then
	echo "setup failed"
	setupfail
fi

if [ $do_vfs -eq 1 ] ; then 
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
exec 2>&1
run_parts ${SYSINT_SCRIPTS}

if [ $do_vfs -eq 1 ] ; then
	export VFS_SCRIPTS
	run_parts ${VFS_SCRIPTS}
fi

# down the road (as we get our hands on more clusters) we'll need a more
# generic way of submitting jobs. for now assume all the world has pbs
which qsub >/dev/null 2>&1
if [ $? -eq 0 ] ; then 
	# go through the hassle of downloading/building mpich2 only if we are
	# actually going to use it
	pull_and_build_mpich2 || buildfail
	. $MPIIO_DRIVER
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

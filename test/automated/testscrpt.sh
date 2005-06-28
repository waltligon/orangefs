#!/bin/sh

# i'm not married to bash. just wanted to get things prototyped

# prereqs: 
#   - $user needs to be in the sudoers file
#   - $user needs to be able to sudo w/o prompting
#   - please don't cheat and run this as root: will not catch permissions bugs
#   - an entry in /etc/fstab for pvfs2.  a bit of a stretch for clusters but
#     not for red machines like gil and lain.

# modify these variables
PVFS2_DEST=/tmp/pvfs2-nightly
PVFS2_MOUNTPOINT=/pvfs2-nightly
EXTRA_TESTS=${HOME}/src/benchmarks

# no need to modify these. they make their own gravy
STARTTIME=`date +%s`
TINDERSCRIPT=$(cd `dirname $0`; pwd)/tinder-pvfs2-status
SYSINT_SCRIPTS=${PVFS2_DEST}/pvfs2/test/automated/sysint-tests.d
VFS_SCRIPTS=${PVFS2_DEST}/pvfs2/test/automated/vfs-tests.d
MPIIO_SCRIPTS=${PVFS2_DEST}/pvfs2/test/automated/mpiio-tests.d

# for debugging and testing, you might need to set the above to your working
# direcory.. .unless you like checking in broken scripts
#SYSINT_SCRIPTS=$(cd `dirname $0`; pwd)/sysint-tests.d
#VFS_SCRIPTS=$(cd `dirname $0`; pwd)/vfs-tests.d
#MPIIO_SCRIPTS=$(cd `dirname $0`; pwd)/mpiio-tests.d

TESTNAME="`hostname -s`-nightly"


# we only have a few hosts that meet all the earlier stated prereqs
VFS_HOSTS="gil lain"


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
	$(cd `dirname $0`;pwd)/../../maint/build/pvfs2-build.sh \
		$with_kernel -r $PVFS2_DEST
	
}

pull_and_build_mpich2 () {
	# just to make debugging less painful
	[ -n "${SKIP_BUILDING_MPICH2}" ] && return 0
	[ -d ${PVFS2_DEST} ] || mkdir ${PVFS2_DEST}
	cd ${PVFS2_DEST}
	rm -rf mpich2-latest.tar.gz
	wget --quiet 'http://www.mcs.anl.gov/~robl/mpich2/mpich2-latest.tar.gz' -O mpich2-latest.tar.gz
	rm -rf mpich2-snap-*
	tar xzf mpich2-latest.tar.gz
	cd mpich2-snap-*
	mkdir build
	cd build
	CFLAGS="-I${PVFS2_DEST}/INSTALL-pvfs2/include"
	LDFLAGS="-L${PVFS2_DEST}/INSTALL-pvfs2/lib"
	LIBS="-lpvfs2 -lpthread"
	export CFLAGS LDFLAGS LIBS

	../configure -q --prefix=${PVFS2_DEST}/soft/mpich2 \
		--enable-romio --with-file-system=ufs+nfs+testfs+pvfs2 \
		--without-mpe --disable-cxx --disable-f77 >mpich2config.log &&\
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
	sudo /sbin/insmod ${PVFS2_DEST}/INSTALL-pvfs2/lib/modules/`uname -r`/kernel/fs/pvfs2.ko
	sudo ${PVFS2_DEST}/INSTALL-pvfs2/sbin/pvfs2-client -p ${PVFS2_DEST}/INSTALL-pvfs2/sbin/pvfs2-client-core
	sudo mount ${PVFS2_MOUNTPOINT}
}

setup_pvfs2() {
	pushd .
	cd $PVFS2_DEST
	INSTALL-pvfs2/bin/pvfs2-genconfig fs.conf server.conf \
		--protocol tcp --port 3399 \
		--ioservers `hostname -s` --metaservers `hostname -s` \
		--storage ${PVFS2_DEST}/STORAGE-pvfs2 \
		--logfile=${PVFS2_DEST}/pvfs2-server.log --quiet
	rm -rf ${PVFS2_DEST}/STORAGE-pvfs2
	INSTALL-pvfs2/sbin/pvfs2-server -p pvfs2-server.pid -f fs.conf server.conf-`hostname -s`
	INSTALL-pvfs2/sbin/pvfs2-server -p pvfs2-server.pid  fs.conf server.conf-`hostname -s`
	chmod 644 ${PVFS2_DEST}/pvfs2-server.log
	popd
}

teardown_pvfs2() {
	if [ -f ${PVFS2_DEST}/pvfs2-server.pid ] ; then
		kill `cat ${PVFS2_DEST}/pvfs2-server.pid`
		sleep 3
		kill -9 `cat ${PVFS2_DEST}/pvfs2-server.pid`
	fi
	# let teardown always succeed.  pvfs2-server.pid could be stale
	return 0
}

buildfail() {
	echo "Failure in build process"
	cat ${PVFS2_DEST}/configure.log ${PVFS2_DEST}/make-extracted.log ${PVFS2_DEST}/make-install.log | \
		${TINDERSCRIPT} ${TESTNAME} build_failed $STARTTIME 
	exit 1
}

setupfail() {
	echo "Failure in setup"
	dmesg | tail -20 > ${PVFS2_DEST}/dmesg
	cat ${PVFS2_DEST}/dmesg ${PVFS2_DEST}/pvfs2-server.log | \
		${TINDERSCRIPT}  ${TESTNAME} test_failed $STARTTIME 
	exit 1
}

tinder_report() {
	if [ "x$failure_logs" == "x" ] ; then
		failure_logs=/dev/null
	fi

	eval cat $failure_logs  |\
		${TINDERSCRIPT} ${TESTNAME} $1 $STARTTIME \
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
		if [ -f $f -a -x $f ] ; then 
			echo "====== running $f ====== "
			. ./$f > ${PVFS2_DEST}/${f}.log 2>&1
			if [ $? == 0 ] ; then 
				nr_passed=$((nr_passed + 1))
			else
				nr_failed=$((nr_failed + 1))
				failure_logs="$failure_logs ${PVFS2_DEST}/${f}.log"
			fi
		fi
	done
}

###
### entry point for script
###

# show that we're doing something
${TINDERSCRIPT} ${TESTNAME} building $STARTTIME </dev/null

# will we be able to do VFS-related tests?
do_vfs=0
for s in $(echo $VFS_HOSTS); do
	if [ `hostname -s` = $s ] ; then
		do_vfs=1
		break
	fi
done

# compile and install
pull_and_build_pvfs2  || buildfail

teardown_pvfs2 && setup_pvfs2 

pull_and_build_mpich2 || buildfail

if [ $? != 0 ] ; then
	echo "setup failed"
	setupfail
fi

if [ $do_vfs -eq 0 ] ; then 
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
failure_logs=""   # a space-delimited list of logs that failed

run_parts ${SYSINT_SCRIPTS}

if [ $do_vfs ] ; then
	run_parts ${VFS_SCRIPTS}
fi

run_parts ${MPIIO_SCRIPTS}
	

if [ $nr_failed -gt 0 ]; then
	tinder_report test_failed
else
	tinder_report success
fi

teardown_pvfs2
[ $do_vfs ] && teardown_vfs

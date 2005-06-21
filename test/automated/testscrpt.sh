#!/bin/sh

# i'm not married to bash. just wanted to get things prototyped

# prereqs: 
#   - $user needs to be in the sudoers file
#   - $user needs to be able to sudo w/o prompting
#   - please don't cheat and run this as root: will not catch permissions bugs
#   - an entry in /etc/fstab for pvfs2.  a bit of a stretch for clusters but
#     not for red machines like gil and lain.


PVFS2_SRC=${HOME}/src/pvfs2
PVFS2_DEST=/tmp/pvfs2-nightly
PVFS2_MOUNTPOINT=/pvfs2-nightly
TESTS=${HOME}/src/benchmarks
STARTTIME=`date +%s`
TINDERSCRIPT=$(cd `dirname $0`; pwd)/tinder-pvfs2-status
TESTNAME="`hostname -s`-nightly"


pull_and_build_pvfs2 () {
	mkdir -p $PVFS2_DEST
	echo "Start time: $STARTTIME"
	${PVFS2_SRC}/maint/build/pvfs2-build.sh  -k /lib/modules/`uname -r`/build -r $PVFS2_DEST
	
}

teardown_vfs() {
	sudo umount $PVFS2_MOUNTPOINT
	sudo killall pvfs2-client
	sleep 1
	sudo /sbin/modprobe -r pvfs2
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
	sudo rm -rf ${PVFS2_DEST}/STORAGE-pvfs2
	sudo INSTALL-pvfs2/sbin/pvfs2-server -p pvfs2-server.pid -f fs.conf server.conf-`hostname -s`
	sudo INSTALL-pvfs2/sbin/pvfs2-server -p pvfs2-server.pid  fs.conf server.conf-`hostname -s`
}

teardown_pvfs2() {
	sudo killall pvfs2-server
	sleep 3
	sudo killall -9 pvfs2-server
}

buidfail() {
	echo "Failure in build process"
	cat ${PVFS2_DEST}/configure.log ${PVFS2_DEST}/make-extracted.log ${PVFS2_DEST}/make-install.log | \
		${TINDERSCRIPT} ${TESTNAME} build_failed $STARTTIME 
	exit 1
}

setupfail() {
	echo "Failure in setup"
	dmesg > ${PVFS2_DEST}/dmesg
	cat ${PVFS2_DEST}/dmesg ${PVFS2_DEST}/pvfs2-server.log | \
		${TINDERSCRIPT}  ${TESTNAME} test_fail $STARTTIME 
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

#
# shell functions for testing pvfs2, one per test
# 

test_ping() {
	${PVFS2_DEST}/INSTALL-pvfs2/bin/pvfs2-ping -m $PVFS2_MOUNTPOINT
}

test_cp() {
	${PVFS2_DEST}/INSTALL-pvfs2/bin/pvfs2-cp \
		${PVFS2_DEST}/pvfs2/configure ${PVFS2_MOUNTPOINT}/configure
}

test_shelltest() {
	sh ${PVFS2_DEST}/pvfs2/test/kernel/linux-2.6/pvfs2-shell-test.sh ${PVFS2_MOUNTPOINT} 
}

test_failure() {
	echo "this test is gauranteed to fail"
	false
}

test_bonnie() {
	pushd .
	cd ${TESTS}/bonnie++-1.03a
	./configure -q && make && cd ${PVFS2_MOUNTPOINT} || return 1
	cd ${PVFS2_MOUNTPOINT} && \
		${TESTS}/bonnie++-1.03a/bonnie++ -n 1:0:0 -s :$((1024*128))
	popd
}

test_iozone() {
	pushd .
	cd ${TESTS}/iozone3_239/src/current  || return 1
	make linux  || return 1
	# -y min record size
	# -q max record size
	# -n min file size
	# -g max file size
	./iozone -a -y 4096 -q $((1024*16)) -n 4096 -g $((1024*16*2)) \
		-f ${PVFS2_MOUNTPOINT}/test_iozone_file
	popd
}

###
### entry point for script
###

# show that we're doing something
${TINDERSCRIPT} ${TESTNAME} building $STARTTIME </dev/null

# compile and install
pull_and_build_pvfs2  || buildfail

teardown_vfs && teardown_pvfs2

setup_pvfs2 && setup_vfs

if [ $? != 0 ] ; then
	echo "setup failed"
	setupfail
fi

# at this point we've got 
# - pvfs2 servers running
# - an entry in /etc/fstab (that was a prerequisite for this script after all)
# - the VFS mounted at $PVFS2_MOUNTPOINT

nr_passed=0
nr_failed=0
failure_logs=""   # a space-delimited list of logs that failed

for t in test_ping test_cp test_failure test_shelltest; do
#for t in test_ping test_cp test_shelltest test_bonnie test_iozone; do
	$t > $t.log 2>&1
	if [ $? == 0 ] ; then 
		nr_passed=$((nr_passed + 1))
	else
		nr_failed=$((nr_failed + 1))
		failure_logs="$failure_logs $t.log"
	fi
done

tinder_report success

#!/bin/bash

export TEST_NAME=${1}.${2}.${3}.${4}.${5}                 # unique name
export BUILD_TEST_NAME=${1}.${2}.${3}                 # unique name
export OS=$2
export VFS=$3
export IO=$4
export FS=$5

# change the tests portion of the job URL to the build portion so we can find
# the artifact with the binaries from the last successful build
export BIN_NAME="${BUILD_TEST_NAME}-bin.tar.bz2"
export BIN_URL="$(echo ${JOB_URL} | sed s/-tests/-build/ |  sed s/io=[^,]*,//)lastSuccessfulBuild/artifact/${BIN_NAME}"

export PVFS2_LOCATION=${WORKSPACE}/install      # install location
export PVFS2_SRC=${WORKSPACE}                   # source tree
export VERBOSE="yes"

## server/client related locations ##
export BASE="/tmp/jenkins/nightly/${TEST_NAME}"  # base dir. of all tests
export PVFS2_MOUNT="${BASE}/mount"              # VFS mount
export PVFS2_LOG="${BASE}/logs/"                # all logs go here
export PVFS2_STORAGE="${BASE}/storage/"         # server backing storage

## extra files copied in by jenkins
export PVFS2_TESTS_NAME="pvfs2-tests-nextgen.tar.bz2"
export PVFS2_EXTRA_TESTS_NAME="benchmarks-20110616.tar.bz2"
export PVFS2_TESTS="${WORKSPACE}/new_tests/"
export PVFS2_EXTRA_TESTS="${BASE}/extra/"       # work dir. of extra tests

## tests to run ##
export PVFS2_SYSINT_TESTS="${PVFS2_TESTS}/sysint-tests.d"
export PVFS2_VFS_TESTS="${PVFS2_TESTS}/vfs-tests.d/"

export LD_LIBRARY_PATH="${PVFS2_LOCATION}/lib:/opt/db4/lib:${LD_LIBRARY_PATH}"

log() {
    if [ -n "${VERBOSE}" ]
    then
        echo $1 $2 | tee -a ${PVFS2_LOG}/all_tests.log
    fi
}

echo_tee() {
        echo -e "$1" "$2" | tee -a ${PVFS2_LOG}/all_tests.log
}

check_return() {
    rc=$1
    msg=$2
    if [ ${rc} -ne 0 ]
    then
       echo_tee "aborting tests due to failed return code of ${msg}"
       exit 1
    fi
}

## create log location right off the bat
mkdir -p ${PVFS2_LOG}
echo_tee "Running tests on $(uname -n -r -m)"

rm -rf ${BIN_URL}*
echo_tee -n "Retrieving artifact [\"$BIN_URL\"]... "
wget -q ${BIN_URL} &>/dev/null
check_return $? "wget binary tarball artifact failed"
echo_tee "ok"

# remove previous binaries
rm -rf ${PVFS2_LOCATION}
echo_tee -n "Extracting binaries ... "
tar -xjf ${BIN_NAME} >/dev/null
check_return $? "extract binary tarball failed"
echo_tee "ok"
rm -f ${BIN_NAME} > /dev/null

if [ ! -d ${PVFS2_LOCATION} -o ! -f ${PVFS2_LOCATION}/sbin/pvfs2-server ]
then
    echo_tee "no install directory"
    exit 1
fi

if [ ! -d ${PVFS_SRC} ]
then
    echo_tee "no source directory"
    exit 1
fi

if [ ! -f ${PVFS2_TESTS_NAME} ]
then
    echo_tee "Test tarball ${PVFS2_TESTS_NAME} doesn't exist"
    exit 1
fi

# remove previous and extract current tests
rm -rf ${PVFS2_TESTS}
mkdir -p ${PVFS2_TESTS}
echo_tee -n "Extracting tests ... "
tar -xjvf ${PVFS2_TESTS_NAME} -C ${PVFS2_TESTS} >/dev/null
check_return $? "extract tests failed"
echo_tee "ok"

pvfs2_client_kernel_cleanup() {
    # unmount anything, kill client processes, wait, then rmmod 
    sudo /bin/umount $PVFS2_MOUNT &>/dev/null
    sleep 2
    sudo killall -9 pvfs2-client &>/dev/null
    sleep 2
    sudo killall -9 pvfs2-client-core &>/dev/null
    sleep 2
    sudo /sbin/rmmod pvfs2 &>/dev/null
    return 0
}

pvfs2_client_fuse_cleanup() {
    sudo /bin/umount $PVFS2_MOUNT &>/dev/null
    sleep 2
    sudo /sbin/rmmod fuse &>/dev/null
    return 0
}

pvfs2_server_cleanup() {
    sudo killall -9 pvfs2-server &>/dev/null
    return 0
}

pvfs2_client_kernel_start() {

    # add kernel module
    # 2.4 is .o, 2.6 is .ko
    mod_base=`find ${PVFS2_LOCATION}/lib -type d -name kernel`
    mod_loc=`find ${mod_base} -type f -name "pvfs2.*o"`
    sudo /sbin/insmod ${mod_loc}
    check_return $? "insmod failed"

    # start client-core, some older sudo version won't let LD_* pass through
    if [ -z "$(sudo sudo -V | grep "Environment variables to preserve")" ]
    then
        echo "export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}; ${PVFS2_LOCATION}/sbin/pvfs2-client -p ${PVFS2_LOCATION}/sbin/pvfs2-client-core -L ${PVFS2_LOG}/pvfs2-client-core.log" > client_run.sh
         chmod +x client_run.sh
         sudo ./client_run.sh
         rm client_run.sh
    else
         sudo LD_LIBRARY_PATH=${LD_LIBRARY_PATH} \
                ${PVFS2_LOCATION}/sbin/pvfs2-client \
                -p ${PVFS2_LOCATION}/sbin/pvfs2-client-core \
                -L ${PVFS2_LOG}/pvfs2-client-core.log
    fi
    check_return $? "client core start failed"

    # make sure we can read the log
    sudo chmod 777 ${PVFS2_LOG}/pvfs2-client-core.log
    sleep 3

    # mount it up
    if [ -f /etc/redhat-release ] &&
       [ "3.9" = "$(cat /etc/redhat-release | awk '{ print $3 }')" ]
    then
        echo "export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}; ${PVFS2_LOCATION}/sbin/mount.pvfs2 tcp://`hostname -s`:3396/pvfs2-fs ${PVFS2_MOUNT}" > client_mount.sh
         chmod +x client_mount.sh
         sudo ./client_mount.sh
         rm client_mount.sh
    else
        sudo /bin/mount -t pvfs2 tcp://`hostname -s`:3396/pvfs2-fs \
            ${PVFS2_MOUNT}
    fi
    check_return $? "mount failed"
    return 0
}

pvfs2_client_fuse_start() {

    mod=`/sbin/lsmod | grep fuse`
    if [ -z "${mod}" ]
    then
        sudo /sbin/modprobe fuse
        check_return $? "modprobe failed"
    fi

    # mount it up
    # start pvfs2fuse, some older sudo version won't let LD_* pass through
    if [ -z "$(sudo sudo -V | grep "Environment variables to preserve")" ]
    then
        echo "export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}; ${PVFS2_LOCATION}/bin/pvfs2fuse -o fs_spec=tcp://`hostname -s`:3396/pvfs2-fs,allow_other ${PVFS2_MOUNT}" > client_run.sh
         chmod +x client_run.sh
         sudo ./client_run.sh
    else
        sudo LD_LIBRARY_PATH=${LD_LIBRARY_PATH} \
	     ${PVFS2_LOCATION}/bin/pvfs2fuse \
                 -o fs_spec=tcp://`hostname -s`:3396/pvfs2-fs,allow_other \
                 ${PVFS2_MOUNT}
    fi
}

pvfs2_server_start() {
    cd $BASE
    ${PVFS2_LOCATION}/bin/pvfs2-genconfig ${BASE}/fs.conf \
        --protocol tcp \
        --iospec="`hostname -s`:{3396-3399}" \
        --metaspec="`hostname -s`:{3396-3399}"  \
        --storage ${PVFS2_STORAGE} \
        --trove-method=${IO:=alt-aio} \
        --logging "none" \
        --logfile=${PVFS2_LOG}/pvfs2-server-${TEST_NAME}.log --quiet
    check_return $? "pvfs2-genconfig failed"

    for alias in `grep 'Alias ' fs.conf | cut -d ' ' -f 2`
    do
        # create the space
        ${PVFS2_LOCATION}/sbin/pvfs2-server \
            -p ${BASE}/pvfs2-server-${alias}.pid \
            -f ${BASE}/fs.conf -a $alias \
            &>${PVFS2_LOG}/pvfs2-server-create-${alias}.log
        check_return $? "pvfs2-server -f failed for $alias"

        # start the server
        ${PVFS2_LOCATION}/sbin/pvfs2-server \
            -p ${BASE}/pvfs2-server-${alias}.pid  \
            ${BASE}/fs.conf $server_conf -a $alias \
            &>${PVFS2_LOG}/pvfs2-server-start-${alias}.log
        check_return $? "pvfs2-server failed for $alias"
    done

    # store out mount path
    echo "tcp://`hostname -s`:3396/pvfs2-fs ${PVFS2_MOUNT} pvfs2 defaults 0 0" \
         > ${BASE}/pvfs2tab
    export PVFS2TAB_FILE=${BASE}/pvfs2tab

    # up the logging chatter
    ${PVFS2_LOCATION}/bin/pvfs2-set-debugmask -m ${PVFS2_MOUNT} "all" \
        &>/dev/null
    check_return $? "pvfs2-set-debugmask failed"
    return 0
}

external_tests_setup()
{
    rm -rf ${PVFS2_EXTRA_TESTS}/*

    tar -xjf ${WORKSPACE}/${PVFS2_EXTRA_TESTS_NAME} -C ${PVFS2_EXTRA_TESTS} \
        >/dev/null
    check_return $? "extra tests untar extra failed"
 
    mv ${PVFS2_EXTRA_TESTS}/benchmarks/* ${PVFS2_EXTRA_TESTS}/
    check_return $? "extra tests mv failed"
}

run_parts() {
    cd $1
    echo_tee "* TEST GROUP START *"
    for f in *
    do
        [ -d $f ] && continue
        if [ -x $f ]
        then
            echo_tee -n "* TEST $f:  "
            ./$f > ${PVFS2_LOG}/${f}-${TEST_NAME}.log
            rc=$?
            if [ ${rc} -eq 0 ]
            then
                nr_passed=$((nr_passed + 1))
                echo_tee "OK"
            else
                nr_failed=$((nr_failed + 1))
                echo_tee "FAILED (${rc})" 
            fi
        fi
    done
    echo_tee "* TEST GROUP DONE *"
}

echo_tee "Running test ${TEST_NAME} in ${BASE}"
# clean up if a nasty failure from the last test
if [ "${VFS:0:6}" = "kernel" ]
then
    pvfs2_client_kernel_cleanup
elif [ "${VFS}" = "fuse" ]
then
    pvfs2_client_fuse_cleanup
fi

pvfs2_server_cleanup

sudo rm -rf $BASE
mkdir -p ${PVFS2_MOUNT}; check_return $? "mkdir on ${PVFS2_MOUNT}"
chmod 777 ${PVFS2_MOUNT}
mkdir -p ${PVFS2_LOG}; check_return $? "mkdir on ${PVFS2_LOG}"
chmod 777 ${PVFS2_MOUNT}
mkdir -p ${PVFS2_STORAGE}; check_return $? "mkdir on ${PVFS2_STORAGE}"
chmod 777 ${PVFS2_MOUNT}
mkdir -p ${PVFS2_EXTRA_TESTS}; check_return $? "mkdir on ${PVFS2_EXTRA_TESTS}"
chmod 777 ${PVFS2_MOUNT}

#exec 6<&1
#exec 7<&2
exec 2>&1

echo_tee -n "setup external tests ... "
external_tests_setup
if [ $? -eq 0 ]
then
    echo_tee "okay"
else
    echo_tee "failed"
    exit 1
fi

echo_tee -n "setup servers... " 
pvfs2_server_start
if [ $? -eq 0 ]
then
   echo_tee "okay"
else
   echo_tee "failed"
   exit 1
fi

echo_tee -n "starting client... "
if [ "${VFS:0:6}" = "kernel" ]
then
    pvfs2_client_kernel_start
elif [ "${VFS}" = "fuse" ]
then
    pvfs2_client_fuse_start
else
    echo_tee "Unknown VFS test type: ${VFS}, exiting"
    exit 1
fi
if [ $? -eq 0 ]
then
   echo_tee "okay"
else
   echo_tee "failed"
   exit 1
fi

nr_passed=0
nr_failed=0

echo_tee "running SYSINT tests"
run_parts ${PVFS2_SYSINT_TESTS}
sleep 3
echo_tee "running VFS tests" 
run_parts ${PVFS2_VFS_TESTS}

# cleanup client and then server

echo_tee -n "stopping client... "
if [ "${VFS:0:6}" = "kernel" ]
then
    pvfs2_client_kernel_cleanup 
elif [ "${VFS}" = "fuse" ]
then
    pvfs2_client_fuse_cleanup
else
    echo_tee "Unknown test, exiting"
    exit 1
fi

if [ $? -eq 0 ]
then
   echo_tee "okay"
fi

echo_tee -n "stopping server... "
pvfs2_server_cleanup
if [ $? -eq 0 ]
then
   echo_tee "okay" 
fi

# restore file descriptors and close temporary fds
#exec 1<&6 6<&-
#exec 2<&7 7<&-

echo_tee "Total Failed: ${nr_failed}"

# remove extracted binaries and artifact
rm -rf ${PVFS2_LOCATION}
rm -rf ${BIN_NAME}*

# remove extraced tests and tarball
rm -rf ${WORKSPACE}/${PVFS2_TESTS_NAME}*
rm -rf ${PVFS2_TESTS}

# remove benchmarks tar ball
rm -rf ${WORKSPACE}/${PVFS2_EXTRA_TESTS_NAME}

echo_tee -n "creating log tarball... "
cd ${WORKSPACE}
tar -cjvf ${WORKSPACE}/test-logs.tar.bz2 ${PVFS2_LOG} >/dev/null 2>&1
echo_tee "done"

if [ ${nr_failed} -gt 0 ]
then
    exit 1
else
    exit 0
fi

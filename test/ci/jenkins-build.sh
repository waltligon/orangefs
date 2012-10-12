#!/bin/bash

# build parameters coming in as arguments
# for jenkins should correspond to appropriate job and matrix parameters
if [ $# -ne 3 ]
then
    echo "usage: $0 <test-name> <os> <vfs>"
    exit
fi

# NAME is just a friendly, no-white space name that should match the jenkins
#     job so the nightly test run can get the right artifact
# OS is just the distro name
# VFS is what interface to use for VFS, valid values are:
#     kernel
#     kernel_helper
#     fuse
NAME=$1
OS=$2
VFS=$3
TEST_NAME="${1}.${2}.${3}"

JENKINS_BUILD=1
# use BUILD_NUMBER to guess if this is running under Jenkins or not. If not,
# just assume we should build what's in pwd
if [ -z "${BUILD_NUMBER}" ]
then
    WORKSPACE=`pwd`
    JENKINS_BUILD=0
fi

INSTALL_PATH="${WORKSPACE}/install"
BUILD_PATH="${WORKSPACE}/build"

## jenkins gives us the CVS branch, otherwise it default to main
CVS_BRANCH=${CVS_BRANCH:="main"}

## setup appropriate configure flags
# common flags
flags=" --prefix=${INSTALL_PATH} --enable-shared" 

# make sure we have our local db4 version available
if [ -d /opt/db4 ]
then
    flags="${flags} --with-db=/opt/db4"
else
    # see if standard location works (ubuntu with db4.8)
    maj=`cat /usr/include/db.h | grep DB_VERSION_MAJOR | awk '{ print $3 }'`
    min=`cat /usr/include/db.h | grep DB_VERSION_MINOR | awk '{ print $3 }'`
    if [ "${maj}" -eq 4 -a "${min}" -ge 8 ]
    then
        echo "Using default system db library"
    else
        echo "No /opt/db4 directory"
        exit 1
    fi
fi

# find kernel sources and set appropriate flags taking 2.4/2.6 into account
# centos 3.9 has a 2.4 kernel, and has the same changes as redhat24
if [ "${VFS:0:6}" = "kernel" ]
then 
    kern=""
    if [ -f /etc/redhat-release ] && 
       [ "3.9" = "$(cat /etc/redhat-release | awk '{ print $3 }')" ]
    then
        kern=`find /usr/src -maxdepth 1 -type d -name "*$(uname -r)*"`
        flags="${flags} --enable-redhat24 --with-kernel24=${kern}"
    elif [ -f /etc/SuSE-release ]
    then
        flavor="desktop"
        kern=`find /usr/src/ -maxdepth 5 -type d -name ${flavor} | head -n 1` 
        flags="${flags} --with-kernel=${kern}"
    elif [ -f /etc/debian_version ]
    then
        kconf=`find /usr/src -maxdepth 3 -type f -path "*linux-source*" -name ".config"`
        kern=`dirname "${kconf}"`
        flags="${flags} --with-kernel=${kern}"
    else 
        kern=`find /usr/src/kernels/ -maxdepth 1 -type d -name "*$(uname -r)*"`
        flags="${flags} --with-kernel=${kern}"
    fi

    if [ -z "${kern}" ]
    then
        echo "No kernel source found"
        exit 1
    else
        echo "Using kernel source at ${kern}"
    fi
elif [ "${VFS}" = "fuse" ]
then
    flags="${flags} --enable-fuse"
else
    echo "Unknown interface type!"
    exit 1
fi

## just set enable kmod_helper if that's what we want
if [ "${VFS}" = "kernel_helper" ]
then
    flags="${flags} --enable-threaded-kmod-helper"
fi

## build exceptions for distros
# centos3 apparently doesn't have epoll
if [ -f /etc/redhat-release ] && 
   [ "3.9" = "$(cat /etc/redhat-release | awk '{ print $3 }')" ]
then
    flags="${flags} --disable-epoll"
fi



## configure and build it!
# run configure, clean out previous builds, and build it
rm -rf ${BUILD_PATH}
rm -rf ${INSTALL_PATH}

mkdir ${BUILD_PATH}
cd ${BUILD_PATH}

echo "Configuring with flags \"$flags\""
${WORKSPACE}/./configure $flags
if [ $? -ne 0 ]
then
    echo "configure failed, failing"
    exit 1
fi

make all
if [ $? -ne 0 ]
then
    echo "make all failed, failing"
    exit 1
fi

if [ "${VFS:0:6}" = "kernel" ]
then
    # build kmod, handling 2.4/2.6 cases
    if [ -f /etc/redhat-release ] &&
       [  "3.9" = "$(cat /etc/redhat-release | awk '{ print $3 }')" ]
    then
        make kmod24
    else
        make kmod 
    fi
fi
if [ $? -ne 0 ]
then
    echo "make kmod failed, failing"
    exit 1
fi

make install
if [ $? -ne 0 ]
then
    echo "make install failed, failing"
    exit 1
fi

if [ "${VFS:0:6}" = "kernel" ]
then
    # install kmod, handling 2.4/2.6 cases
    if [ -f /etc/redhat-release ] && 
       [  "3.9" = "$(cat /etc/redhat-release | awk '{ print $3 }')" ]
    then
        make "DESTDIR=${INSTALL_PATH}" just_kmod24_install
        make just_kmod24_apps_install
    else
        make kmod_prefix=${INSTALL_PATH} kmod_install
    fi
fi

# now make the tests
cd test
../../test/./configure $flags
make all
if [ $? -ne 0 ]
then
    echo "configure tests failed, failing"
    exit 1
fi

make install
if [ $? -ne 0 ]
then
    echo "make install tests failed, failing"
    exit 1
fi
# back to build directory
cd ../

# back to original workspace
cd ../
tar -cjf ${TEST_NAME}-bin.tar.bz2 install
if [ $? -ne 0 ]
then
    echo "Failure creating tar of installed binaries"
    exit 1
fi

exit 0

#!/bin/sh

usage()
{
    echo "USAGE: pvfs2-build-tests.sh <-r dir>"
    echo "  -r: path to directory to build and install in"
    echo "  -v: name of tag or branch in CVS"
    echo "  -m: enable mpi tests and use mpi install path"
    echo ""
    echo "set PVFS2_TEST_CONFIGOPTS to add platform specific configure options"
    echo ""
    return
}

configureopts=$PVFS2_TEST_CONFIGOPTS

rootdir=/tmp/pvfs2-build-test
cvs_tag="HEAD"
with_mpi=false

# get command line arguments
while getopts r:v:m: opt
do
    case "$opt" in
	r) rootdir="$OPTARG";;
	v) cvs_tag="$OPTARG";;
	m) with_mpi="$OPTARG";;
	\?) usage; exit 1;; 
    esac
done   

if [ ! -d $rootdir ] ; then
	echo "pvfs2 path not found: $rootdir."
	exit 1
fi

srcdir=$rootdir/pvfs2-$cvs_tag
builddir=$rootdir/BUILD-pvfs2-$cvs_tag
installdir=$rootdir/INSTALL-pvfs2-$cvs_tag

testsrcdir=$srcdir/test
testbuilddir=$builddir/test

cd $testbuilddir

configureopts="$configureopts --with-pvfs2-src=$srcdir --with-pvfs2-build=$builddir"

if test "x$with_mpi" != "xfalse"; then
	configureopts="$configureopts --with-mpi=$with_mpi"
else
	configureopts="$configureopts --with-mpi=$rootdir/mpich2/"
fi

$testsrcdir/configure $configureopts --prefix=$installdir > /dev/null 2>&1
if test $? != 0; then
	echo "configure of pvfs2 tests failed.  See $testbuilddir/configure.log"
	exit 1
fi

make > make.log 2>&1
if test $? != 0; then
	echo "Build of pvfs2 tests failed.  See $testbuilddir/make.log"
	exit 1
fi

make install > make-install.log 2>&1
if test $? != 0; then
	echo "Install of pvfs2 tests failed.  See $testbuilddir/make-install.log"
	exit 1
fi




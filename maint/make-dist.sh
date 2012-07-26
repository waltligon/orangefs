#!/bin/bash
#
# custom script for building a pvfs2 distribution
#
# this should only be run from a freshly exported pvfs2
# tagged release version, or a cvs checkout; a working
# directory will leave local changes in the distribution
#

if test -z $1; then
    echo "No source directory specified"
    echo "Usage: make_dist.sh <src dir> <version string>"
    exit 1
fi

if test -z $2; then
    echo "No version string specified"
    echo "Usage: make_dist.sh <src dir> <version string>"
    exit 1
fi

BUILDDIR=$(pwd)
SRCDIR="$1"
PVFS2_VERSION="$2"

if test "x$SRCDIR" = "x."; then
    SRCIR=`pwd`
    echo "Assuming top-level source directory is $SRCDIR"
fi

STARTDIR=`pwd`
TARGETBASE="pvfs-$PVFS2_VERSION"
TARGETDIR="/tmp/$TARGETBASE"
TARFILE_NAME="$BUILDDIR/pvfs-$PVFS2_VERSION.tar"
TARBALL_NAME="$BUILDDIR/pvfs-$PVFS2_VERSION.tar.gz"
TAR=`which tar`
GZIP=`which gzip`

if test -z $TAR; then
    echo "The required tar program is not in your path; aborting"
    exit 1
fi

if test -z $GZIP; then
    echo "The required gzip program is not in your path; aborting"
    exit 1
fi

##################
# start processing
##################

# new directory to construct release in
if test -d $TARGETDIR; then
    rm -rf $TARGETDIR
fi
mkdir $TARGETDIR
if [ $? -ne 0 ]
then
    exit 1
fi

# copy source over
cp -f --no-dereference -R $SRCDIR/* $TARGETDIR

cd $TARGETDIR

# dump some special options into the top level module.mk.in
echo "DIST_RELEASE = 1" >> module.mk.in

# Run config.save to regenerate files.
./config.save
if [ $? -ne 0 ]
then
    exit 1
fi
make docs
if [ $? -ne 0 ]
then
    exit 1
fi
make statecompgen
if [ $? -ne 0 ]
then
    exit 1
fi

# clean out extra files (distclean will not remove .sm or doc files if
# DIST_RELEASE is set)
make distclean
if [ $? -ne 0 ]
then
    exit 1
fi

# clean out cvs directories and other cruft (if any)
for f in `find . | grep CVS`; do rm -rf $f; done
for f in `find . | grep \#`;  do rm -rf $f; done
for f in `find . | grep \~`;  do rm -rf $f; done
for f in `find . -name *.o`;  do rm -rf $f; done
for f in `find . -name core`; do rm -rf $f; done
for f in `find . -name core\.[1-9]*`; do rm -rf $f; done
for f in `find . -name module.mk`; do rm -rf $f; done
for f in `find . -name "*.log"`; do rm -rf $f; done
for f in `find . -name "*.toc"`; do rm -rf $f; done
for f in `find . -name "*.aux"`; do rm -rf $f; done
rm -f Makefile pvfs2-config.h
rm -f src/common/statecomp/statecomp

# make sure the cleaned up directory exists
cd /tmp
if ! test -d "$TARGETBASE"; then
    echo "Newly created target directory doesn't exist; aborting"
    exit 1
fi

# tar up the cleaned up directory
tar c "$TARGETBASE" > $TARFILE_NAME 2> /dev/null

if ! test -f $TARFILE_NAME; then
    echo "Newly created tarfile does not exist!"
    echo "Error creating tarfile $TARBALL_NAME; aborting"
    exit 1
fi

# if a tarball already exists, remove it before creating a new one
if test -f $TARBALL_NAME; then
    rm -f $TARBALL_NAME
fi

# gzip the newly created tarfile
gzip $TARFILE_NAME

if ! test -f "$TARBALL_NAME"; then
    echo "Newly created tarball does not exist!"
    echo "Error creating tarball $TARBALL_NAME; aborting"
    exit 1
fi

rm -rf $TARGETDIR

cd $STARTDIR

if test -f "$TARBALL_NAME"; then
    echo "Distribution file is ready at:"
    echo "$TARBALL_NAME"
fi

exit 0

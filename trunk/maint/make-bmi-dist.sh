#!/bin/bash
#
# custom script for building a BMI distribution
#
# this should only be run from a freshly exported pvfs2
# tagged release version, or a cvs checkout; a working
# directory will leave local changes in the distribution
#

echo "make-bmi-dist"
if test -z $1; then
    echo "No source directory specified"
    echo "Usage: make-bmi-dist.sh <src dir> <builddir> <version string>"
    exit 1
fi

if test -z $3; then
    echo "No version string specified"
    echo "Usage: make-bmi-dist.sh <src dir> <builddir> <version string>"
    exit 1
fi

BUILDDIR=$2
SRCDIR="$1"
PVFS2_VERSION="$3"

if test "x$SRCDIR" = "x."; then
    SRCIR=`pwd`
    echo "Assuming top-level source directory is $SRCDIR"
fi

STARTDIR=`pwd`
TARGETBASE="bmi-$PVFS2_VERSION"
TARGETDIR="/tmp/$TARGETBASE"
TARFILE_NAME="$BUILDDIR/bmi-$PVFS2_VERSION.tar"
TARBALL_NAME="$BUILDDIR/bmi-$PVFS2_VERSION.tar.gz"
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
mkdir -p $TARGETDIR/src/common/misc
if [ $? -ne 0 ]
then
    exit 1
fi

mkdir -p $TARGETDIR/src/io
if [ $? -ne 0 ]
then
    exit 1
fi

mkdir -p $TARGETDIR/include
if [ $? -ne 0 ]
then
    exit 1
fi

# copy source over
cp -f --no-dereference -R $SRCDIR/AUTHORS $TARGETDIR
cp -f --no-dereference -R $SRCDIR/COPYING $TARGETDIR
cp -f --no-dereference -R $SRCDIR/CREDITS $TARGETDIR
cp -f --no-dereference -R $SRCDIR/ChangeLog $TARGETDIR
cp -f --no-dereference -R $SRCDIR/INSTALL $TARGETDIR
cp -f --no-dereference -R $SRCDIR/Makefile.in $TARGETDIR
cp -f --no-dereference -R $SRCDIR/README $TARGETDIR
cp -f --no-dereference -R $SRCDIR/README.name_change $TARGETDIR
cp -f --no-dereference -R $BUILDDIR/config.save $TARGETDIR
cp -f --no-dereference -R $SRCDIR/configure $TARGETDIR
cp -f --no-dereference -R $SRCDIR/configure.in $TARGETDIR
cp -f --no-dereference -R $SRCDIR/module.mk.in $TARGETDIR
cp -f --no-dereference -R $SRCDIR/prepare $TARGETDIR
cp -f --no-dereference -R $SRCDIR/maint $TARGETDIR
cp -f --no-dereference -R $SRCDIR/pvfs2-config.h.in $TARGETDIR
cp -f --no-dereference -R $SRCDIR/src/io/bmi $TARGETDIR/src/io/
cp -f --no-dereference -R $SRCDIR/src/common/gen-locks $TARGETDIR/src/common/
cp -f --no-dereference -R $SRCDIR/src/common/gossip $TARGETDIR/src/common/
cp -f --no-dereference -R $SRCDIR/src/common/id-generator $TARGETDIR/src/common/
cp -f --no-dereference -R $SRCDIR/src/common/llist $TARGETDIR/src/common/
cp -f $SRCDIR/include/pvfs2.h.in $TARGETDIR/include/
cp -f $SRCDIR/include/pvfs2-debug.h $TARGETDIR/include/
cp -f $SRCDIR/include/pvfs2-encode-stubs.h $TARGETDIR/include/
cp -f $SRCDIR/include/pvfs2-event.h $TARGETDIR/include/
cp -f $SRCDIR/include/pvfs2-mgmt.h $TARGETDIR/include/
cp -f $SRCDIR/include/pvfs2-request.h $TARGETDIR/include/
cp -f $SRCDIR/include/pvfs2-sysint.h $TARGETDIR/include/
cp -f $SRCDIR/include/pvfs2-util.h $TARGETDIR/include/
cp -f $SRCDIR/include/pvfs2-types.h $TARGETDIR/include/
cp -f $SRCDIR/include/pvfs2-hint.h $TARGETDIR/include/
cp -f $SRCDIR/src/common/misc/str-utils.h $TARGETDIR/src/common/misc/
cp -f $SRCDIR/src/common/misc/str-utils.c $TARGETDIR/src/common/misc/
cp -f $SRCDIR/src/common/misc/pint-event.h $TARGETDIR/src/common/misc/
cp -f $SRCDIR/src/common/misc/pint-event.c $TARGETDIR/src/common/misc/
cp -f $SRCDIR/src/common/misc/errno-mapping.c $TARGETDIR/src/common/misc/
cp -f $SRCDIR/src/common/misc/pvfs2-internal.h $TARGETDIR/src/common/misc/
cp -f $SRCDIR/src/common/misc/module.mk.in $TARGETDIR/src/common/misc/
cp -f --no-dereference -R $SRCDIR/src/common/quickhash $TARGETDIR/src/common/
cp -f --no-dereference -R $SRCDIR/src/common/quicklist $TARGETDIR/src/common/
cp -f --no-dereference -R $SRCDIR/src/common/statecomp $TARGETDIR/src/common/
cp -f --no-dereference -R $SRCDIR/src/common/mgmt $TARGETDIR/src/common/
cp -f --no-dereference -R $SRCDIR/src/common/misc $TARGETDIR/src/common/
cp -f --no-dereference -R $SRCDIR/src/proto $TARGETDIR/src/

cd $TARGETDIR

# make configure.in BMI specific
sed -ie "s/AC_INIT(include\/pvfs2-types.h)/AC_INIT()/" configure.in
sed -ie "s/dnl BMI_ONLY_TAG/BUILD_BMI_ONLY=1/" configure.in
sed -ie "s/chmod/dnl chmod/" configure.in

# make AC_OUTPUT bmi specific
sed -i "/src\/apps\/admin/d" configure.in
sed -i "/src\/apps\/karma/d" configure.in
sed -i "/src\/apps\/vis/d" configure.in
sed -i "/src\/apps\/fuse/d" configure.in
sed -i "/src\/apps\/kernel\/linux\/module.mk/d" configure.in
sed -i "/src\/io\/trove/d" configure.in
sed -i "/src\/common\/dotconf/d" configure.in
sed -i "/src\/io\/description/d" configure.in
sed -i "/src\/io\/flow/d" configure.in
sed -i "/src\/io\/buffer/d" configure.in
sed -i "/src\/io\/job/d" configure.in
sed -i "/src\/io\/dev/d" configure.in
sed -i "/src\/server\/module.mk/d" configure.in
sed -i "/src\/server\/request-scheduler/d" configure.in
sed -i "/src\/client\/sysint/d" configure.in
sed -i "/src\/kernel\/linux-2.6/d" configure.in
sed -i "/src\/kernel\/linux-2.4/d" configure.in
sed -i "/doc\/module.mk/d" configure.in
sed -i "/doc\/coding\/module.mk/d" configure.in
sed -i "/doc\/design\/module.mk/d" configure.in
sed -i "/doc\/random\/module.mk/d" configure.in
sed -i "/examples\/pvfs2-server.rc/d" configure.in
sed -i "/doc\/doxygen\/pvfs2-doxygen.conf/d" configure.in
sed -i "/common\/events\/module.mk/d" configure.in

# dump some special options into the top level module.mk.in
echo "DIST_RELEASE = 1" >> module.mk.in

# Run ./prepare to regenerate the configure script
./prepare

# Run config.save to regenerate files.
#./config.save
#if [ $? -ne 0 ]
#then
#    exit 1
#fi
#make docs
#if [ $? -ne 0 ]
#then
#    exit 1
#fi
#make statecompgen
#if [ $? -ne 0 ]
#then
#    exit 1
#fi

# clean out extra files (distclean will not remove .sm or doc files if
# DIST_RELEASE is set)
#make distclean
#if [ $? -ne 0 ]
#then
#    exit 1
#fi

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
rm -f Makefile pvfs2-config.h PVFS2-GLOBAL-TODO.txt
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

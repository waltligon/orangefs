#!/bin/sh 
#
#

rootdir=/tmp/pvfs2-build-test

tarballurl=http://www.mcs.anl.gov/hpio/pvfs2-0.0.6.tar.gz

# end of user defines

tarball=`basename $tarballurl`
tarballdir=`echo $tarball | sed -e "s/.tar.gz//" | sed -e "s/.tgz//"`
old_wd=`pwd`

if [ ! -d $rootdir ] ; then
	echo "Specified directory $rootdir does not exist.  Aborting."
	exit 1
fi

date=`date "+%Y-%m-%d-%H-%M"`
host=`uname -n`

srcdir=$rootdir/pvfs2
builddir=$rootdir/BUILD-pvfs2
installdir=$rootdir/INSTALL-pvfs2

# clean up src, build, install directories
# clean up misc. files in the root directory too
rm -rf $srcdir $builddir $installdir
rm -rf $rootdir/\*.tgz
rm -rf $rootdir/pvfs2

# move to our root dir
cd $rootdir

# get the source (-nv keeps it kinda quiet)
wget -nv $tarballurl

if [ $? != 0 ] ; then
	echo "wget of $tarballurl failed.  Aborting."
	exit 1
fi

# untar the source
tar xzf $tarball

if [ -d $tarballdir ] ; then
	mv $tarballdir $srcdir
fi

if [ ! -d $srcdir ] ; then
	echo "Tarball $tarball did not create a $srcdir directory or a $tarballdir directory.  Aborting."
	exit 1
fi

# create build and install directories, configure
mkdir $builddir
mkdir $installdir
cd $builddir
$srcdir/configure --prefix=$installdir 2>&1 > $rootdir/configure.log

if [ $? != 0 ] ; then
	echo "Configure failed; see $rootdir/configure.log.  Aborting."
	exit 1
fi

# make
make 2>&1 > $rootdir/make.log

if [ $? != 0 ] ; then
	echo "Make failed; see $rootdir/make.log.  Aborting."
	exit 1
fi

# look through make output
PEMM=`which pvfs2-extract-make-msgs.pl`
if [ x$PEMM == "x" ] ; then
	if [ ! -x $old_wd/pvfs2-extract-make-msgs.pl ] ; then
		echo "Failed to find pvfs2-extract-make-msgs.pl.  Aborting."
		exit 1
	else
		PEMM=$old_wd/pvfs2-extract-make-msgs.pl 
	fi
fi
$PEMM $rootdir/make.log 2>&1 > $rootdir/make-extracted.log

if [ $? != 0 ] ; then
	echo "Spurious output during make; see $rootdir/make-extracted.log.  Aborting."
	exit 1
fi

# make install
make install 2>&1 > $rootdir/make-install.log

if [ $? != 0 ] ; then
	echo "Make install failed; see $rootdir/make-install.log.  Aborting."
	exit 1
fi

# after installing, create a pvfs volume (PAV)
# then run a test or set of tests (most likely PTS)
echo "Script completed successfully.  Exiting."

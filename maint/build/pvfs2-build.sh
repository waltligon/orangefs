#!/bin/sh 
#
# requires: 
#  expect
#  cvs (if pulling from CVS

rootdir=/tmp/pvfs2-build-test
tarballurl=http://www.mcs.anl.gov/hpio/pvfs2-0.0.6.tar.gz
cvsroot=:pserver:anonymous@cvs.parl.clemson.edu:/anoncvs 


#
# use this method if you want to test a release
#   takes no arguments.  returns nonzero on error
get_dist() {
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

}

# get_cvs requires expect
# use this method if you want to, well, test whatever is in cvs at this very
# moment.  takes no arguments. returns nonzero on error.
get_cvs() {
	expect -c "spawn -noecho cvs -Q -d $cvsroot login; send \r;"
	cvs -Q -d $cvsroot co pvfs2
	if [ $? -ne 0 ] ; then
		echo "Pulling PVFS2 from $cvsroot failed."
		exit 1
	fi
}

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

# could make this some sort of command line option... 
get_cvs || exit 1


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
PEMM=`which pvfs2-extract-make-msgs.pl 2>/dev/null`
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


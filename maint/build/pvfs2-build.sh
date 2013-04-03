#/bin/sh  
#
# requires: 
#  cvs (if pulling from CVS

if [ $DEBUG ] ; then
	set -xv
fi

rootdir=/tmp/pvfs2-build-test
tarballurl=http://www.mcs.anl.gov/hpio/pvfs2-0.0.6.tar.gz
cvsroot=:pserver:anonymous@cvs.parl.clemson.edu:/anoncvs 
# specify extra configure options here; for now we disable karma because
# of all the gtk warnings
with_db_arg=""
if [ $WITH_DB ] ; then
	with_db_arg=--with-db=$WITH_DB
fi
configureopts="$PVFS2_CONFIGOPTS --enable-shared --enable-ucache --enable-strict --disable-karma $with_db_arg"


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

# pulls from CVS the tag or branch specified by the first argument.  returns
# nonzero on error.
get_cvs() {
	#cvs -Q -d $cvsroot co -r $1 pvfs2 
	
	echo "Current directory is `pwd`"
	svn export --force -q http://www.orangefs.org/svn/orangefs/$1/
	if [ $? -ne 0 ] ; then
		echo "Pulling PVFS2 from http://www.orangefs.org/svn/orangefs/$1/ failed."
		exit 1
	fi
	#ls -l 
	
	#mv pvfs2 pvfs2-$1
	#split off last element in path
	BRANCH=`echo $1 | awk -F"/" '{print $NF}'`
	
	echo "Branch is ${BRANCH}"
	
	mv $BRANCH pvfs2-$BRANCH
}

# end of user defines

tarball=`basename $tarballurl`
tarballdir=`echo $tarball | sed -e "s/.tar.gz//" | sed -e "s/.tgz//"`
old_wd=$( cd `dirname $0`; pwd)
build_kernel="false"
build_tests="false"
make_targets="all"
cvs_tag="HEAD"
kerneldir=""

usage()
{
    echo "USAGE: pvfs2-build.sh <-k kernel source> <-r dir>"
    echo "  -k: path to kernel source (enables module build)"
    echo "  -r: path to directory to build and install in"
    echo "  -s: build with key-based security"
    echo "  -c: build with certificate-based security"
    echo "  -t: build test programs"
    echo "  -v: name of tag or branch in CVS"
    echo ""
    echo "set PVFS2_CONFIGOPTS to add platform specific configure options"
    return
}

# get command line arguments
while getopts k:r:cstv: opt
do
    case "$opt" in
	k) build_kernel="true"; kerneldir="$OPTARG";;
	r) rootdir="$OPTARG";;
	c) enable_certs="true";;
	s) enable_keys="true";;
	t) build_tests="true";;
	v) full_cvs_tag="$OPTARG";;
	\?) usage; exit 1;; 
    esac
done   

echo "PVFS2 will be built in ${rootdir}."

#cvs tag is final element of full cvs tag
cvs_tag=`echo $full_cvs_tag | awk -F"/" '{print $NF}'`

if [ ! -d $rootdir ] ; then
	mkdir $rootdir
fi

if [ "$enable_keys" = "true" ]
then
	configureopts="${configureopts} --enable-security-key"
fi

if [ "$enable_certs" = "true" ]
then
# TODO
	echo "certificate support not yet implemented"
	exit 1
fi

date=`date "+%Y-%m-%d-%H-%M"`
host=`uname -n`

srcdir=$rootdir/pvfs2-$cvs_tag
#builddir=$rootdir/BUILD-pvfs2-$cvs_tag
builddir=$srcdir
installdir=$rootdir/INSTALL-pvfs2-$cvs_tag

# clean up src, build, install directories
# clean up misc. files in the root directory too
rm -rf $srcdir $builddir $installdir
rm -rf $rootdir/\*.tgz
rm -rf $rootdir/pvfs2

# move to our root dir
cd $rootdir

# could make this some sort of command line option... 
get_cvs $full_cvs_tag || exit 1


# create build and install directories, configure
#mkdir $builddir
mkdir $installdir
cd $srcdir
$srcdir/prepare > prepare.out
#cd $builddir
#ls $srcdir
#$srcdir/prepare > prepare.out
if [ $build_kernel = "true" ] ; then
	$srcdir/configure $configureopts  --with-kernel=$kerneldir --prefix=$installdir > $rootdir/configure-${cvs_tag}.log 2>&1
	make_targets="all kmod"
else
	$srcdir/configure $configureopts  --prefix=$installdir  > $rootdir/configure-${cvs_tag}.log 2>&1
	make_targets="all"
fi

if [ $? != 0 ] ; then
	echo "Configure failed; see $rootdir/configure-${cvs_tag}.log.  Aborting."
	exit 1
fi

# make
make $make_targets > $rootdir/make-${cvs_tag}.log 2>&1

if [ $? != 0 ] ; then
	echo "Make failed; see $rootdir/make-${cvs_tag}.log.  Aborting."
	exit 1
fi

# look through make output
PEMM=`which pvfs2-extract-make-msgs.pl 2>/dev/null`
if [ x$PEMM = "x" ] ; then
	if [ ! -x $old_wd/pvfs2-extract-make-msgs.pl ] ; then
		echo "Failed to find pvfs2-extract-make-msgs.pl.  Aborting."
		exit 1
	else
		PEMM=$old_wd/pvfs2-extract-make-msgs.pl 
	fi
fi
$PEMM $rootdir/make-${cvs_tag}.log > $rootdir/make-extracted-${cvs_tag}.log 2>&1

if [ $? != 0 ] ; then
	# warnings used to be fatal.  We still want no warnings, but we'll flag
	# that we found some instead of bailing out altogether.
	echo "Unexpected output during make; see $rootdir/make-extracted-${cvs_tag}.log."
	touch $rootdir/pvfs-built-with-warnings
fi

# make install
make install > $rootdir/make-install-${cvs_tag}.log 2>&1

if [ $? != 0 ] ; then
	echo "Make install failed; see $rootdir/make-install-${cvs_tag}.log.  Aborting."
	exit 1
fi

if [ $build_kernel = "true" ] ; then
	make kmod_prefix=${installdir} kmod_install
fi

# build tests if needed 
if [ $build_tests = "true" ] ; then
	cd $builddir/test
	$srcdir/test/configure $configureopts > $rootdir/configure-test-${cvs_tag}.log 2>&1
	if [ $? != 0 ] ; then
		echo "Configure of test programs failed; see $rootdir/configure-test-${cvs_tag}.log.  Aborting."
		exit 1
	fi

	# make
	make  all > $rootdir/make-test-${cvs_tag}.log 2>&1

	if [ $? != 0 ] ; then
		echo "Make failed; see $rootdir/make-test-${cvs_tag}.log.  Aborting."
		exit 1
	fi

	# look through make output
	PEMM=`which pvfs2-extract-make-msgs.pl 2>/dev/null`
	if [ x$PEMM = "x" ] ; then
		if [ ! -x $old_wd/pvfs2-extract-make-msgs.pl ] ; then
			echo "Failed to find pvfs2-extract-make-msgs.pl.  Aborting."
			exit 1
		else
			PEMM=$old_wd/pvfs2-extract-make-msgs.pl 
		fi
	fi
	$PEMM $rootdir/make-test-${cvs_tag}.log  > $rootdir/make-test-extracted-${cvs_tag}.log 2>&1
	if [ $? != 0 ] ; then
		# same as above.  Indicate that we found something, 
		# but don't abort
		echo "Unexpected output during test make; see $rootdir/make-test-extracted-${cvs_tag}.log."
		touch $rootdir/pvfs2-test-built-with-warnings
	fi
	make install > $rootdir/make-test-install-${cvs_tag}.log 2>&1
	if [ $? != 0 ] ; then
		echo "Make install (tests) failed; see $rootdir/make-test-install-${cvs_tag}.log.  Aborting."
		exit 1
	fi
fi

exit 0

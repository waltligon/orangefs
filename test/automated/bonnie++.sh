#!/bin/sh

bonnie_tarballname=bonnie++-1.03a.tgz
bonnie_url=http://www.parl.clemson.edu/~tluck/$bonnie_tarballname

bonnie_srcdir=/tmp/bonnie++.${USER}
bonnie_tarball=$bonnie_srcdir/$bonnie_tarballname
bonnie_tarballdir=`echo $bonnie_tarball | sed -e "s/.tar.gz//" | sed -e "s/.tgz//"`
bonnie_installdir=/tmp/bonnie++.install.${USER}

usage()
{
    echo "USAGE: bonnie++.sh -d <scratch dir>"
    return
}

bonnie_scratchdir=fake

while getopts d: opt
do
    case "$opt" in
        d) bonnie_scratchdir="$OPTARG";;
        \?) usage; exit 1;;
    esac
done

if [ $bonnie_scratchdir = "fake" ] ; then 
	echo "No scratch directory specified with -d; aborting."
	usage
	exit 1
fi

bonnie_configureopts="--prefix=$bonnie_installdir"
bonnie_csv=/tmp/bonnie++-1.03a.csv.$USER
bonnie_log=/tmp/bonnie++-1.03a.log.$USER

scratch_size=20
ram_size=10
files_to_stat=2
min_file_size=0
max_file_size=2
num_directories=3


if [ -d $bonnie_srcdir ] ; then
	rm -rf $bonnie_srcdir
fi
if [ -d $bonnie_installdir ] ; then
	rm -rf $bonnie_installdir
fi
if [ -d $bonnie_scratchdir ] ; then
	rm -rf $bonnie_scratchdir
fi

mkdir -p $bonnie_srcdir
mkdir -p $bonnie_installdir
mkdir -p $bonnie_scratchdir

# get the source (-nv keeps it kinda quiet)
cd $bonnie_srcdir
wget -nv $bonnie_url
if [ $? != 0 ] ; then
	echo "wget of $bonnie_url failed.  Aborting."
	exit 1
fi
# untar the source
tar xzf $bonnie_tarball
if [ $? != 0 ] ; then
	echo "Untarring of $bonnie_tarball failed."
	exit 1
fi
#configure bonnie++
cd $bonnie_tarballdir
./configure $bonnie_configureopts &> /tmp/bonnie++_configure.log.$USER
if [ $? != 0 ] ; then
	echo "Configure of $bonnie_tarballdir failed."
	echo "See log file: /tmp/bonnie++_configure.log.$USER"
	exit 1
fi

# make and install bonnie
make install &> /tmp/bonnie++_make.log.$USER
if [ $? != 0 ] ; then
	echo "Make and install of $bonnie_tarballdir failed."
	echo "See log file: /tmp/bonnie++_make.log.$USER"
	exit 1
fi
	
# run bonnie
rm -f $bonnie_csv $bonnie_log
time $bonnie_installdir/sbin/bonnie++ \
	-d $bonnie_scratchdir \
	-s $scratch_size \
	-r $ram_size \
	-n $files_to_stat:$max_file_size:$min_file_size:$num_directories \
	-q >> $bonnie_csv 2>> $bonnie_log
if [ $? != 0 ] ; then
	echo "Bonnie failed to execute."
	exit 1
fi

cat $bonnie_log

#usage: bonnie++ [-d scratch-dir] [-s size(Mb)[:chunk-size(b)]]
#                [-n number-to-stat[:max-size[:min-size][:num-directories]]]
#								[-m machine-name]
#								[-r ram-size-in-Mb]
#								[-x number-of-tests] [-u uid-to-use:gid-to-use] [-g gid-to-use]
#								[-q] [-f] [-b] [-p processes | -y]


#clean up
rm -f $bonnie_csv $bonnie_log
rm -rf $bonnie_srcdir
rm -rf $bonnie_installdir
rm -rf $bonnie_scratchdir

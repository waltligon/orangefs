#!/bin/sh

cvsroot=:pserver:anonymous@cvs.parl.clemson.edu:/anoncvs

source /usr/share/usepackage/use.bsh
use standard-user
use mpich2

DATE=`date "+%b%d-%Y"`

mkdir -p $HOME/testing/$DATE
cd $HOME/testing/$DATE

expect -c "spawn -noecho cvs -Q -d $cvsroot login; send \r;"
cvs -Q -d $cvsroot co pvfs2
if [ $? -ne 0 ] ; then
    echo "Pulling PVFS2 from $cvsroot failed."
    exit 1
fi

cd pvfs2/test/automated/

./run-test-adenine.sh 
if [ $? -ne 0 ] ; then
    echo "run-test-adenine.sh failed."
    exit 1
fi

exit 0



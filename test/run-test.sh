#!/bin/bash
export SVNBRANCH=$1
script runtest.txt
export LD_LIBRARY_PATH=/opt/db4/lib
mkdir ~/test2
cd ~/test2
echo svn export --force -q http://www.orangefs.org/svn/orangefs/branches/${SVNBRANCH}/test/run-nightly-setup
svn export --force -q http://www.orangefs.org/svn/orangefs/branches/${SVNBRANCH}/test/run-nightly-setup
./run-nightly-setup testingjdb
cd ~/test2/pvfs2/test
ls
./run-nightly testingjdb
exit
exit

#!/bin/sh

email=pvfs2-testing@beowulf-underground.org

cvsroot=:pserver:anonymous@cvs.parl.clemson.edu:/anoncvs

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

mkdir -p $HOME/testing/$DATE/work

sudo /bin/dmesg -c

echo "test output:" > /tmp/test_out.txt
echo "=======================" >> /tmp/test_out.txt
echo " " >> /tmp/test_out.txt


./single-node-kernel-test.sh -k /home/pvfs2/linux-2.6.3/ -r $HOME/testing/$DATE/work/ >> /tmp/test_out.txt 2>&1 
if [ $? -ne 0 ] ; then
    subject="PVFS2 kernel test: FAIL"
else
    subject="PVFS2 kernel test: PASS"
fi

echo "dmesg output:" >> /tmp/test_out.txt
echo "=======================" >> /tmp/test_out.txt
echo " " >> /tmp/test_out.txt

sudo /bin/dmesg 2>&1 >> /tmp/test_out.txt

rm -rf $HOME/testing/$DATE

mail -s "$subject" "$email" < /tmp/test_out.txt

exit 0



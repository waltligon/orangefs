#!/bin/sh

DATE=`date "+%b%d-%Y"`
old_wd=`pwd`

mkdir -p /home/$USER/testing/$DATE/work

sed -e s/DATE/$DATE/g PAVCONFIG.template | sed -e s/USER/$USER/g > PAVCONFIG
sed -e s/DATE/$DATE/g CONFIG.template | sed -e s/USER/$USER/g > CONFIG
sed -e s/DATE/$DATE/g SUBMIT.pbs.template | sed -e s/USER/$USER/g > SUBMIT.pbs

email=`grep EMAIL CONFIG | cut -d "=" -f 2`

cd ../../maint/build/

./pvfs2-build.sh /home/$USER/testing/$DATE/work > tmp.out 2>&1
if [ $? != 0 ] ; then
    mail -s "PVFS2 test: FAIL (pvfs2-build.sh)" "$email" < tmp.out
    exit 1
fi

./mpich2-build.py /home/$USER/testing/$DATE/work > tmp.out 2>&1
if [ $? != 0 ] ; then
    mail -s "PVFS2 test: FAIL (mpich2-build.py)" "$email" < tmp.out
    exit 1
fi

cd $old_wd
qsub -l nodes=4:ppn=1 SUBMIT.pbs

exit 0



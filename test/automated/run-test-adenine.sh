#!/bin/sh

DATE=`date "+%b%d-%Y"`
old_wd=`pwd`

mkdir -p /home/$USER/testing/$DATE/work

sed -e s/DATE/$DATE/g PAVCONFIG.template | sed -e s/USER/$USER/g > PAVCONFIG
sed -e s/DATE/$DATE/g CONFIG.template | sed -e s/USER/$USER/g > CONFIG
sed -e s/DATE/$DATE/g SUBMIT.pbs.template | sed -e s/USER/$USER/g > SUBMIT.pbs

cd ../../maint/build/

./pvfs2-build.sh /home/$USER/testing/$DATE/work
./mpich2-build.py /home/$USER/testing/$DATE/work

cd $old_wd
qsub -l nodes=4:ppn=1 SUBMIT.pbs

exit 0



#!/bin/bash

cd ${WORKSPACE}

rm -rf build 
mkdir build && cd build
if [ $? -ne 0 ]
then
    echo "failure creating build directory"
fi

echo -n "Configuring source ... "
.././configure >/dev/null 2>&1
if [ $? -ne 0 ]
then
    echo "failure configuring source"
    exit 1
fi
echo "okay"

echo -n "Making docs ... "
make docs >/dev/null 2>&1
if [ $? -ne 0 ]
then
    echo "failure making docs"
    exit 1
fi
echo "okay"

files=`find doc/ -regextype posix-egrep -regex ".+\.(pdf|html)"`
file_count=`echo ${files} | wc -l`
if [ ${file_count} -ne 60 ]
then
    echo "Not enough documents, only ${file_count}"
fi

echo -n "Creating tar of docs ... "
tar -cjvf ${WORKSPACE}/orange-branch-docs.tar.bz2 ${files}
if [ $? -ne 0 ]
then
    echo "failed"
fi
echo "okay"

cd ${WORKSPACE}
rm -rf build

exit 0

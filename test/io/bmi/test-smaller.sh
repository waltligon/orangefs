#!/bin/tcsh

if ( $# != 1 ) then
	echo Usage: ${0} \<name of raw output dir\>
	exit -1
endif

if ( -d $1 ) then
	echo AAaagh: $1 exists!
	exit -1
endif

mkdir ${1}

./test-manyonesmaller-tcp.pl | tee ${1}/manyonesmaller-tcp-all

cat ${1}/manyonesmaller-tcp-all | grep "mpi server" > ${1}/mpi_server.dat
cat ${1}/manyonesmaller-tcp-all | grep "bmi server" > ${1}/bmi_server.dat
cat ${1}/manyonesmaller-tcp-all | grep "mpi client" > ${1}/mpi_client.dat
cat ${1}/manyonesmaller-tcp-all | grep "bmi client" > ${1}/bmi_client.dat

cat ${1}/mpi_server.dat | ./ave-bw5 > ${1}/mpi_server.ave
cat ${1}/bmi_server.dat | ./ave-bw5 > ${1}/bmi_server.ave
cat ${1}/bmi_client.dat | ./ave-bw5 > ${1}/bmi_client.ave
cat ${1}/mpi_client.dat | ./ave-bw5 > ${1}/mpi_client.ave

cp test-bw.plt ${1}/
set blah=`pwd`
cd ${1}/
gnuplot test-bw.plt
mv test-bw.eps test-manyonesmaller-tcp.eps
cd $blah

./test-onemanysmaller-tcp.pl | tee ${1}/onemanysmaller-tcp-all

cat ${1}/onemanysmaller-tcp-all | grep "mpi server" > ${1}/mpi_server.dat
cat ${1}/onemanysmaller-tcp-all | grep "bmi server" > ${1}/bmi_server.dat
cat ${1}/onemanysmaller-tcp-all | grep "mpi client" > ${1}/mpi_client.dat
cat ${1}/onemanysmaller-tcp-all | grep "bmi client" > ${1}/bmi_client.dat

cat ${1}/mpi_server.dat | ./ave-bw5 > ${1}/mpi_server.ave
cat ${1}/bmi_server.dat | ./ave-bw5 > ${1}/bmi_server.ave
cat ${1}/bmi_client.dat | ./ave-bw5 > ${1}/bmi_client.ave
cat ${1}/mpi_client.dat | ./ave-bw5 > ${1}/mpi_client.ave

cp test-bw.plt ${1}/
set blah=`pwd`
cd ${1}/
gnuplot test-bw.plt
mv test-bw.eps test-onemanysmaller-tcp.eps
cd $blah

exit(0);

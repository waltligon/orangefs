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

./test-lat-tcp.pl | tee ${1}/lat-tcp-all

cat ${1}/lat-tcp-all | grep "mpi server" > ${1}/mpi_server.dat
cat ${1}/lat-tcp-all | grep "bmi server" > ${1}/bmi_server.dat
cat ${1}/lat-tcp-all | grep "mpi client" > ${1}/mpi_client.dat
cat ${1}/lat-tcp-all | grep "bmi client" > ${1}/bmi_client.dat

cat ${1}/mpi_server.dat | ./ave-lat5 > ${1}/mpi_server.ave
cat ${1}/bmi_server.dat | ./ave-lat5 > ${1}/bmi_server.ave
cat ${1}/bmi_client.dat | ./ave-lat5 > ${1}/bmi_client.ave
cat ${1}/mpi_client.dat | ./ave-lat5 > ${1}/mpi_client.ave

cp test-lat.plt ${1}/
set blah=`pwd`
cd ${1}/
gnuplot test-lat.plt
mv test-lat.eps test-lat-tcp.eps
cd $blah


./test-manyonesmall-tcp.pl | tee ${1}/manyonesmall-tcp-all

cat ${1}/manyonesmall-tcp-all | grep "mpi server" > ${1}/mpi_server.dat
cat ${1}/manyonesmall-tcp-all | grep "bmi server" > ${1}/bmi_server.dat
cat ${1}/manyonesmall-tcp-all | grep "mpi client" > ${1}/mpi_client.dat
cat ${1}/manyonesmall-tcp-all | grep "bmi client" > ${1}/bmi_client.dat

cat ${1}/mpi_server.dat | ./ave-bw5 > ${1}/mpi_server.ave
cat ${1}/bmi_server.dat | ./ave-bw5 > ${1}/bmi_server.ave
cat ${1}/bmi_client.dat | ./ave-bw5 > ${1}/bmi_client.ave
cat ${1}/mpi_client.dat | ./ave-bw5 > ${1}/mpi_client.ave

cp test-bw.plt ${1}/
set blah=`pwd`
cd ${1}/
gnuplot test-bw.plt
mv test-bw.eps test-manyonesmall-tcp.eps
cd $blah

./test-onemanysmall-tcp.pl | tee ${1}/onemanysmall-tcp-all

cat ${1}/onemanysmall-tcp-all | grep "mpi server" > ${1}/mpi_server.dat
cat ${1}/onemanysmall-tcp-all | grep "bmi server" > ${1}/bmi_server.dat
cat ${1}/onemanysmall-tcp-all | grep "mpi client" > ${1}/mpi_client.dat
cat ${1}/onemanysmall-tcp-all | grep "bmi client" > ${1}/bmi_client.dat

cat ${1}/mpi_server.dat | ./ave-bw5 > ${1}/mpi_server.ave
cat ${1}/bmi_server.dat | ./ave-bw5 > ${1}/bmi_server.ave
cat ${1}/bmi_client.dat | ./ave-bw5 > ${1}/bmi_client.ave
cat ${1}/mpi_client.dat | ./ave-bw5 > ${1}/mpi_client.ave

cp test-bw.plt ${1}/
set blah=`pwd`
cd ${1}/
gnuplot test-bw.plt
mv test-bw.eps test-onemanysmall-tcp.eps
cd $blah

./test-manymanybig-tcp.pl | tee ${1}/manymanybig-tcp-all

cat ${1}/manymanybig-tcp-all | grep "mpi server" > ${1}/mpi_server.dat
cat ${1}/manymanybig-tcp-all | grep "bmi server" > ${1}/bmi_server.dat
cat ${1}/manymanybig-tcp-all | grep "mpi client" > ${1}/mpi_client.dat
cat ${1}/manymanybig-tcp-all | grep "bmi client" > ${1}/bmi_client.dat

cat ${1}/mpi_server.dat | ./ave-bw5 > ${1}/mpi_server.ave
cat ${1}/bmi_server.dat | ./ave-bw5 > ${1}/bmi_server.ave
cat ${1}/bmi_client.dat | ./ave-bw5 > ${1}/bmi_client.ave
cat ${1}/mpi_client.dat | ./ave-bw5 > ${1}/mpi_client.ave

cp test-bw.plt ${1}/
set blah=`pwd`
cd ${1}/
gnuplot test-bw.plt
mv test-bw.eps test-manymanybig-tcp.eps
cd $blah



exit(0);

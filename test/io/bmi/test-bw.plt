set data style lines
set title 'aggregate bandwidth'
set xlabel 'Message Size'
set ylabel 'bandwidth (MB/sec)'
set term post eps color "Times-Roman" 20
set size 1.4,1.4
set key right bottom
set pointsize 2
set output "test-bw.eps"
plot	 "mpi_server.ave" using 1:2 title "MPI server" with lines, \
	 "mpi_client.ave" using 1:2 title "MPI client" with lines, \
	 "bmi_server.ave" using 1:2 title "BMI server" with lines, \
	 "bmi_client.ave" using 1:2 title "BMI client" with lines

set data style lines
set title 'round trip latency'
set xlabel 'Message Size'
set ylabel 'time (seconds)'
set term post eps color "Times-Roman" 20
set size 1.4,1.4
set key right bottom
set pointsize 2
set output "test-lat.eps"
plot	 "mpi_server.ave" using 1:3 title "MPI server" with lines, \
	 "mpi_client.ave" using 1:3 title "MPI client" with lines, \
	 "bmi_server.ave" using 1:3 title "BMI server" with lines, \
	 "bmi_client.ave" using 1:3 title "BMI client" with lines

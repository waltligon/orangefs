all: simple1_unaligned test_grep test_kmeans test_vren

#MPICC=mpicc
MPICC = /home/sson/as-branch/mpich2.tcp/bin/mpicc
CFLAGS=-Wall -O2 

simple.o: simple.c
	$(MPICC) -c -O2 simple.c
simple: simple.o
	$(MPICC) $(CFLAGS) -o simple simple.o
test_grep.o: test_grep.c
	$(MPICC) -c -O2 test_grep.c
test_grep: test_grep.o
	$(MPICC) $(CFLAGS) -o test_grep test_grep.o
simple1.o: simple1.c
	$(MPICC) -c simple1.c
simple1: simple1.o
	$(MPICC) $(CFLAGS) -o simple1 simple1.o
simple1_unaligned.o: simple1_unaligned.c
	$(MPICC) -c simple1_unaligned.c
simple1_unaligned: simple1_unaligned.o
	$(MPICC) $(CFLAGS) -o simple1_unaligned simple1_unaligned.o -lm
test_kmeans.o: test_kmeans.c
	$(MPICC) -c test_kmeans.c -g
test_kmeans: test_kmeans.o
	$(MPICC) $(CFLAGS) -o test_kmeans test_kmeans.o -lm -g
test_vren.o: test_vren.c
	$(MPICC) -c test_vren.c -g
test_vren: test_vren.o
	$(MPICC) $(CFLAGS) -o test_vren test_vren.o -lm -g
simple2.o: simple2.c
	$(MPICC) -c simple2.c
simple2: simple2.o
	$(MPICC) $(CFLAGS) -o simple2 simple2.o
simple3.o: simple3.c
	$(MPICC) -c simple3.c
simple3: simple3.o
	$(MPICC) $(CFLAGS) -o simple3 simple3.o
vector.o: vector.c
	$(MPICC) -c vector.c
vector: vector.o cmd.o
	$(MPICC) $(CFLAGS) -o vector vector.o cmd.o
vector1.o: vector1.c
	$(MPICC) -c vector1.c
vector1: vector1.o
	$(MPICC) $(CFLAGS) -o vector1 vector1.o
testwrite: testwrite.o
	$(MPICC) -O2 -o testwrite testwrite.o -L/home/sson/as-branch/subfiling_lib_PnetCDF_v0.1 -lsubfile -L/home/sson/as-branch/parallel-netcdf-1.2.0-as/install/lib -lpnetcdf -lm
testwrite.o: testwrite.c
	$(MPICC) -O2  -c testwrite.c -I/home/sson/as-branch/parallel-netcdf-1.2.0-as/install/include -I/home/sson/as-branch/subfiling_lib_PnetCDF_v0.1
testread: testread.o 
	$(MPICC) -O2 -o testread testread.o -L/home/sson/as-branch/subfiling_lib_PnetCDF_v0.1 -lsubfile -L/home/sson/as-branch/parallel-netcdf-1.2.0-as/install/lib -lpnetcdf -lm
testread.o: testread.c
	$(MPICC) -O2  -c testread.c -I/home/sson/as-branch/parallel-netcdf-1.2.0-as/install/include -I/home/sson/as-branch/subfiling_lib_PnetCDF_v0.1
testreadx: testreadx.o 
	$(MPICC) -O2 -o testreadx testreadx.o -L/home/sson/as-branch/subfiling_lib_PnetCDF_v0.1 -lsubfile -L/home/sson/as-branch/parallel-netcdf-1.2.0-as/install/lib -lpnetcdf -lm
testreadx.o: testreadx.c
	$(MPICC) -O2  -c testreadx.c -I/home/sson/as-branch/parallel-netcdf-1.2.0-as/install/include -I/home/sson/as-branch/subfiling_lib_PnetCDF_v0.1
clean:
	rm -f *.o *~ simple simple1 simple2 simple3 vector vector1 simple1_unaligned test_kmeans testwrite testread
	rm -f test_grep

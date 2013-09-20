#!/usr/bin/perl


# List of test names - Can be easily expanded

#running sysint scripts
my @sysint_tests = qw(
cp
misc
mkdir-sysint
ping
symlink-sysint
zerofill 
);

#running vfs scripts
my @vfs_tests = qw(
append
append2
bonnie
dbench
fdtree
fstest
fsx
iozone
ltp
mkdir-vfs
shelltest
symlink-vfs
tail
vfs-cp
);

#mpi tests
my @mpi_tests = qw(
functions
heidelberg-IO
ior-mpiio
ior-mpiio-2
ior-mpiio-3
mpi-io-test
noncontig
romio-async
romio-coll_test
romio-error
romio-excl
romio-file_info
romio-noncontig_coll2
romio-psimple
romio-simple
romio-split_coll
romio-status
stadler-file-view-test );

#index into table of testnames - start at -1 in case tests not run
my $sysint_idx = -1;
my $vfs_idx = -1;
my $mpi_idx = -1;

# number of tests passed and failed
my $failed = 0;
my $passed = 0;

# parameters: logfile

open (LOGFILE,$ARGV[0]) || die "Could not open logfile $ARGV[0]";

# open the logfile

while (<LOGFILE>)
	{
#	print $_;
	$line = $_;
	chomp($line); 

	# whitespace - skip
	if ( $line eq "" )
		{
		next;
		}
	# did we run sysint tests?
	if ($line =~ /running sysint scripts/)
		{
		#set the index to 0 so that we can check the individual tests
		$sysint_idx = 0;
		next;
		}
	#print "checking sysint test number $sysint_idx $sysint_tests[$sysint_idx]\n";
	
	# test all the sysint scripts in the table
	if ( $sysint_idx >= 0 && $sysint_idx < @sysint_tests)
		{
		#print "checking sysint test number $sysint_idx $sysint_tests[$sysint_idx]\n";
		if ($line =~ /${sysint_tests[$sysint_idx]}(.*)FAILED/)
			{
			print "Test ${sysint_tests[$sysint_idx]} FAILED!\n";
			$failed++;
			
			}
		elsif ($line =~ /${sysint_tests[$sysint_idx]}(.*)OK/)
			{
			$passed++;
			}
		$sysint_idx++;
		next;
		}

	# did we run vfs tests?
	if ($line =~ /running vfs scripts/)
		{
		#print "vfs found";
		$vfs_idx = 0;
		next;
		}
	#print "checking sysint test number $sysint_idx $sysint_tests[$sysint_idx]\n";
	
	# test all the vfs scripts in the table
	if ( $vfs_idx >= 0 && $vfs_idx < @vfs_tests)
		{
		#print "checking vfs test number $vfs_idx $vfs_tests[$vfs_idx]\n";
		if ($line =~ /${vfs_tests[$vfs_idx]}(.*)FAILED/)
			{
			print "Test ${vfs_tests[$vfs_idx]} FAILED!\n";
			$failed++;
			
			}
		elsif ($line =~ /${vfs_tests[$vfs_idx]}(.*)OK/)
			{
			$passed++;
			}
		$vfs_idx++;
		next;
		}

	# did we run mpi tests?
	if ($line =~ /running mpi scripts/)
		{
		#print "mpi found";
		$mpi_idx = 0;
		next;
		}
	
	# test all the mpi scripts in the table
	if ( $mpi_idx >= 0 && $mpi_idx < @mpi_tests)
		{
		#print "checking sysint test number $mpi_idx $mpi_tests[$mpi_idx]\n";
		if ($line =~ /${mpi_tests[$mpi_idx]}(.*)FAILED/)
			{
			print "Test ${mpi_tests[$mpi_idx]} FAILED!\n";
			$failed++;
			}
		elsif ($line =~ /${mpi_tests[$mpi_idx]}(.*)OK/)
			{
			$passed++;
			}
		$mpi_idx++;
		next;
		}
	# did we run userlib tests? Same as VFS tests
	if ($line =~ /running userlib scripts/)
		{
		#print "vfs found";
		$vfs_idx = 0;
		next;
		}
	#print "checking sysint test number $sysint_idx $sysint_tests[$sysint_idx]\n";
	
	# test all the vfs scripts in the table
	if ( $vfs_idx >= 0 && $vfs_idx < @vfs_tests)
		{
		#print "checking vfs test number $vfs_idx $vfs_tests[$vfs_idx]\n";
		if ($line =~ /${vfs_tests[$vfs_idx]}(.*)FAILED/)
			{
			print "Test ${vfs_tests[$vfs_idx]} (userlib) FAILED!\n";
			$failed++;
			
			}
		elsif ($line =~ /${vfs_tests[$vfs_idx]}(.*)OK/)
			{
			$passed++;
			}
		$vfs_idx++;
		next;
		}
	}
	
print "\n";
print "Number of tests passed = $passed\n";
print "Number of tests failed = $failed\n";

exit $failed

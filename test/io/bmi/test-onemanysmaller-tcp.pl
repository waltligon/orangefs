#!/usr/bin/perl 

#----------------------------------------------------------------
# Phil Carns
#
# Simple script to generate bmi test data
#
#----------------------------------------------------------------

print `date`;

$reps = 5;
$msg_len = 600;
$msg_len_jump = 100;
$msg_len_max = 1100;
$total_len = 1000000;

select STDOUT; $| = 1;

while($msg_len <= $msg_len_max)
{
	$i = 0;
	while($i < $reps)
	{
		print `mpirun -np 16 ./driver_bw_multi -m bmi_tcp -l $msg_len -t $total_len -s 15`;
		$i++;
	}

	$msg_len += $msg_len_jump;
}

print "\n";
		

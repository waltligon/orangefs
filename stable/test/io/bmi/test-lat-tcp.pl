#!/usr/bin/perl 

#----------------------------------------------------------------
# Phil Carns
#
# Simple script to generate bmi test data
#
#----------------------------------------------------------------

print `date`;

$reps = 5;
$msg_len = 100;
$msg_len_jump = 100;
$msg_len_max = 3000;

select STDOUT; $| = 1;

while($msg_len <= $msg_len_max)
{
	$i = 0;
	while($i < $reps)
	{
		print `mpirun -np 2 ./driver_latency -m bmi_tcp -l $msg_len`;
		$i++;
	}

	$msg_len += $msg_len_jump;
}

print "\n";
		

#!/usr/bin/perl -w

@files = ();

$print_ct = 0;

if ($#ARGV < 0)
{
	die "usage: <cmd> <filename> ...";
}

#
# why did i do this?
#
foreach $arg (@ARGV)
{
	$files[$#files+1] = $arg;
}

foreach $file (@files)
{
	open (FD, "<$file") || die "error opening $file";

	$have_last_cmd = 0;
	$printed_last_cmd = 0;
	$last_cmd = "";

	while (<>)
	{
		if (/^  CC/ ||
		    /^  SMC/ ||
		    /^  DEP/ ||
		    /^  RANLIB/ ||
		    /^  LD/ ||
		    /^  BISON/ ||
		    /^  FLEX/ ||
		    /^  GENCONFIG/ ||
		    /^  GEN/)
		{
			$last_cmd = $_;
			$have_last_cmd = 1;
			$printed_last_cmd = 0;
		}
		else {
			if ($have_last_cmd == 0 && $printed_last_cmd == 0)
			{
				print $last_cmd;
				$printed_last_cmd = 1;
			}
			print $_;
			$print_ct++;
		}
	}
	close FD;
}

if ($print_ct == 0)
{
	exit 0;
}
else
{
	exit 1;
}

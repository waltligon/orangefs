#! /usr/bin/perl -w

# run this like so:
# nm /path/to/libpvfs//libpvfs2.a | ./maint/pvfs2symbolcheck.pl
@prefixes = ("gossip", "job", "PVFS", "PINT", "trove", "dbpf", "BMI", "flow", "flowproto");

$file_name = "<unknown>";
$badsyms = 0;

sub check_name;

while (<>) {

	if (/^[0123456789abcdef]{8} T /) {
		s/^[0123456789abcdef]{8} T //; # strip off position info
		chop;
		check_name;
	}
	elsif (/^[\w\-\_]+\.o:/) {
		s/://; # strip off ':'s
		s/\.o/\.c/; # strip off ':'s
		chop;
		$file_name = $_;
	}
}

print "\nInstances of bad prefixes: ".$badsyms."\n";
print "Instances of acceptable prefixes:\n";
foreach $K (keys %names) {
	print $K.": ".$names{$K}."\n";
}

sub check_name {
	foreach $S (@prefixes) {
		if (/^$S\_/) {
			$names{$S}++;
			return;
		}
	}

	print "warning: file ".$file_name." has global symbol ".$_."\n";
	$badsyms++;
}

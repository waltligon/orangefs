#! /usr/bin/perl -w

use Getopt::Long;

@files = ();

foreach $arg (@ARGV) {
    $files[$#files+1] = $arg;
}

foreach $file (@files) {
    $modified = 0;
    open(INFD, "<$file") || die "error opening $file";
    open(OUTFD, ">${file}.new") || die "error opening $file";

    while (<INFD>) {
	if (/vim: /) {
	    if (/vim: ts=8 sts=4 sw=4 expandtab/) {
		print OUTFD $_;
	    }
	    else {
		print OUTFD " * vim: ts=8 sts=4 sw=4 expandtab\n";
		$modified = 1;
	    }
	}
	else {
	    print OUTFD $_;
	}
    }

    close(INFD);
    close(OUTFD);

    if ($modified) {
	rename "$file", "${file}.orig";
	rename "${file}.new", "$file";
    }
    else {
	unlink "${file}.new";
    }
}


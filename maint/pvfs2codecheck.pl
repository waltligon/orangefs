#! /usr/bin/perl -w

use Getopt::Long;

$fix_flag = 0;

GetOptions("fix" => \$fix_flag) or die "GetOptions: $!";

@files = ();

foreach $arg (@ARGV) {
    $files[$#files+1] = $arg;
}

if($fix_flag && $#files) {
    print "Error: please don't specify multiple targets when using --fix.\n";
    exit;
}

foreach $file (@files) {
    open(FD, "<$file") || die "error opening $file";

    $has_copyright = 0;
    $has_emacs_c_basic_offset = 0;
    $has_emacs_c_indent_level = 0;
    $has_vim_formatting = 0;

    while (<>) {
	if (/c-basic-offset: 4/) {
	    $has_emacs_c_basic_offset++;
	}
	if (/c-indent-level: 4/) {
	    $has_emacs_c_indent_level++;
	}
	if (/vim: ts=8 sts=4 sw=4 expandtab/) {
	    $has_vim_formatting++;
	}
	if (/See COPYING in top-level directory/) {
	    $has_copyright++;
	}
    }

    close(FD);

    if(!$fix_flag){
	if ($has_copyright < 1) { print "$file: missing or incorrect copyright block.\n"; }
	if ($has_copyright > 1) { print "$file: too many copyright lines!\n"; }

	if ($has_emacs_c_basic_offset < 1) { print "$file: missing emacs c-basic-offset: 4 line.\n"; }
	if ($has_emacs_c_basic_offset > 1) { print "$file: too many emacs c-basic-offset lines!\n"; }

	if ($has_emacs_c_indent_level < 1) { print "$file: missing emacs c-indent-level: 4 line.\n"; }
	if ($has_emacs_c_indent_level > 1) { print "$file: too many emacs c-indent-level lines!\n"; }

	if ($has_vim_formatting < 1) { print "$file: missing or incorrect vim formatting line.\n"; }
	if ($has_vim_formatting > 1) { print "$file: too many vim formatting lines!\n"; }
    }
    else {
    	if(!$has_copyright) {
	    open(TP_FD, "<TEMPLATE_COPYRIGHT_HEADER.txt") || die "error opening $file";
	    while(<TP_FD>){
	    	print $_;
	    }
	    close(TP_FD);
	}

    	open(FD, "<$file") || die "error opening $file";
	while(<FD>){
	    print $_;
	}
	close(FD);

    	if($has_emacs_c_basic_offset < 1 && $has_emacs_c_indent_level < 1 && $has_vim_formatting < 1) {
	    open(TP_FD, "<TEMPLATE_FORMAT_TRAILER.txt") || die "error opening $file";
	    while(<TP_FD>){
	    	print $_;
	    }
	    close(TP_FD);
	}
    }
}


#! /usr/bin/perl -w

# pvfs2latexwrapper.pl
#
# This program was written to overcome the deficiencies of latex
# with respect to input and output file locating and the frequent
# use of relative paths within latex files for figures and such.
#
# The approach implemented is to use symlinks back into the source
# tree and to build in the correct output directory.  This gets all
# ancillary files dropped into the right spot.
#
# Steps:
# 1) Figure out all the appropriate directories and files.
#    a) This requires chdir'ing around a bit.
# 2) Create links if necessary (including PVFS2-specific figure hack).
# 3) Execute latex.
# 4) Cleanup symlinks if necessary.
#
# Notes:
# - Assumes that the output filename is nothing special (i.e. is the
#   default output name of latex, and that only the path is odd).
# - Doesn't check for errors.

use Cwd;
use Getopt::Long;
use File::Basename;

GetOptions("o=s" => \$outputpath, "i=s" => \$inputpath) || die "GetOptions: $!";

if (!defined($outputpath) || !defined($inputpath)) {
    die "usage: pvfs2latexwrapper.pl -o <outputfile> -i <inputfile>\n";
}

# gather up the list of directories and such that we'll need

$startdir = cwd();
$indir    = dirname($inputpath);
$infile   = basename($inputpath);
$outdir   = dirname($outputpath);
@auxfiles = ();
# $outfile  = basename($outputpath);

chdir "$indir";
$indir    = cwd();
chdir "$startdir";
chdir "$outdir";
$outdir   = cwd();

if ($outdir ne $indir) {
    my @info = ();

    symlink $indir."/".$infile, $infile;
    symlink $indir."/figs", "figs";

    # ok, here's a bit of a mess: some LaTeX files include other files. try to
    # handle it:

    open FILE, $infile;
    while (<FILE>) {
    	if (/\\input/) {
		# external .tex files are referenced by
		# \input{foo.tex}
		# so pull out the 'foo.tex' part and make a symlink
		s/\\input\{(.*)\}/$1/;
		chomp;
		symlink $indir."/".$_,  $_;
		push @auxfiles, $_;
	}
    }
    close FILE;


    system "latex $infile";
    system "latex $infile"; # once more to get figures correct
    system "latex $infile"; # and sometimes it takes a *third* pass

    # Note: leaving the "figs" link there; helps in ps/pdf build.
    # unlink $infile, "figs";
    unlink $infile;
    # clean up all the extra flies linked because of \input{} calls
    foreach $link (@auxfiles) {
    	unlink $link;
    }
}
else {
    system "latex $infile";
    system "latex $infile"; # once more to get figures correct
    system "latex $infile"; # and sometimes it takes a *third* pass
}

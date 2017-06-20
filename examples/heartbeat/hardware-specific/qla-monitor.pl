#!/usr/bin/perl 

use strict;
use Getopt::Long;
use File::Find;
use File::Path;
use POSIX qw(setsid);
use Socket;

my $device = undef;
my $mon_device = undef;

&process_args;

# strip path and partition off of device name 
if($device =~ /\/+dev\/+([a-zA-Z]+)[1234567890]*/)
{
   $mon_device = $1;
}
else
{
   print "Error: poorly formated device name.\n";
   exit 1;
}

my $devcount = `powermt display dev=$mon_device 2>&1 |grep -c -E "(qla)|(lpfc)"`;
if ($devcount < 1)
{
   print "Error: could not find device $mon_device.\n";
   exit 1;
}
chomp($devcount);

my $deadcount = `powermt display dev=$mon_device 2>&1 |grep -E "(qla)|(lpfc)" | grep -c dead`;
chomp($deadcount);

if($devcount == $deadcount)
{
   print "Error: all paths for device $device are dead.\n";
   exit 1;
}

exit 0;

sub process_args
{
   # Parse the command line options
   # For a description of the command line options see &print_help
   use vars qw( $opt_help $opt_device);

   Getopt::Long::Configure( "no_ignore_case", "bundling");
   GetOptions( "help",
               "device=s");

   if ($opt_help)
   {
      &print_help;
      exit(0);
   }

   if($opt_device)
   {
      $device = $opt_device;
   }
   else
   {
      &print_help;
      die "Error: must specify device.\n";
   }
}

# --------------- print help information ------------------------------
sub print_help {

   print <<EOF;

This script will check a QLogic or Emulex SAN device to see if it is operating correctly.

usage: qla-monitor.pl --device <SAN device>

options:
   --help                        Print this help and exit.
   --device           <STRING>   SAN device (such as /dev/emcpowera1).

EOF
}

# Local variables:
#  c-basic-offset: 3
#  perl-indent-level: 3
#  tab-width: 3
#  indent-tabs-mode: nil
#  shiftwidth: 3
# End:
#
# vim: ts=3 expandtab


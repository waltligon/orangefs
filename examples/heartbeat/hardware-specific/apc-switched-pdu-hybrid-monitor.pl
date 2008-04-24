#!/usr/bin/perl 

# requires APC MIB file, which is available for download from APC's web site

# this script will first attempt to use SNMP to monitor a power strip,
# and then fail back to using SSH

use strict;
use Getopt::Long;
use File::Find;
use File::Path;
use POSIX qw(setsid);
use Socket;

my $host;
my $ssh_user;
my $ssh_pass; 
my $snmp_user;
my $snmp_pass;

&process_args;

# try snmp first
my $snmp_command = "snmpstatus -v3 -a MD5 -l authNoPriv -u $snmp_user -A $snmp_pass $host";

my $snmp_output = `$snmp_command 2>&1`;
if ( $? == 0 )
{
   # print "Success, snmp\n";
   exit 0;
}

# fall back to ssh
my $ssh_command = "/usr/bin/apc-switched-pdu-ssh-monitor.exp $host $ssh_user $ssh_pass";

my $ssh_output = `$ssh_command 2>&1`;
if ( $? == 0 )
{
   # print "Success, ssh\n";
   exit 0;
}

# if we reach this point, then neither worked
print "Error: failed to contact APC unit.\n";
print "SNMP output: $snmp_output";
print "SSH output: $ssh_output";

exit 1;

sub process_args
{
   # Parse the command line options
   # For a description of the command line options see &print_help
   use vars qw( $opt_help $opt_host $opt_ssh_user $opt_ssh_pass $opt_snmp_user $opt_snmp_pass);

   Getopt::Long::Configure( "no_ignore_case", "bundling");
   GetOptions( "help",
               "host=s",
               "ssh-user=s",
               "ssh-pass=s",
               "snmp-user=s",
               "snmp-pass=s");

   if ($opt_help)
   {
      &print_help;
      exit(0);
   }

   if(!$opt_host || !$opt_ssh_user || !$opt_ssh_pass || 
      !$opt_snmp_user || !$opt_snmp_pass)
   {
      &print_help;
      die "Error: missing arguments.\n";
   }

   $host = $opt_host;
   $ssh_user = $opt_ssh_user;
   $ssh_pass = $opt_ssh_pass; 
   $snmp_user = $opt_snmp_user;
   $snmp_pass = $opt_snmp_pass;
}


# --------------- print help information ------------------------------
sub print_help {

   print <<EOF;

This script will check an APC power control device to see if it is 
responding.  It will first attempt communication via SNMP and then fall 
back to ssh if that fails.

usage: apc-switched-pdu-hybrid-monitor.pl <option>

options:
   --help                   print this help and exit
   --host        <STRING>   hostname of APC unit
   --ssh-user    <STRING>   ssh username for APC unit
   --ssh-pass    <STRING>   ssh password for APC unit
   --snmp-user   <STRING>   SNMP username for APC unit
   --snmp-pass   <STRING>   SNMP authentication pass phrase (MD5) for APC unit

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


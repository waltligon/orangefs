#!/usr/bin/perl 

# requires APC MIB file, which is available for download from APC's web site

# this script will first attempt to use SNMP to send power control commands,
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
my $outlet;
my $cmd;

my $snmp_command;

&process_args;

if($cmd eq "on")
{
   $snmp_command = "snmpset -v3 -a MD5 -l authNoPriv -u $snmp_user -A $snmp_pass -m \"/var/lib/filesystems/powernet387.mib\" $host PowerNet-MIB::rPDUOutletControlOutletCommand.$outlet = 1";
}
elsif($cmd eq "off")
{
   $snmp_command = "snmpset -v3 -a MD5 -l authNoPriv -u $snmp_user -A $snmp_pass -m \"/var/lib/filesystems/powernet387.mib\" $host PowerNet-MIB::rPDUOutletControlOutletCommand.$outlet = 2";
}
elsif($cmd eq "reboot")
{
   $snmp_command = "snmpset -v3 -a MD5 -l authNoPriv -u $snmp_user -A $snmp_pass -m \"/var/lib/filesystems/powernet387.mib\" $host PowerNet-MIB::rPDUOutletControlOutletCommand.$outlet = 3";
}
else
{
   die("Error: $cmd is not a valid outlet command.\n");
}

# try snmp first
my $snmp_output = `$snmp_command 2>&1`;
if ( $? == 0 )
{
   exit 0;
}

# fall back to ssh
my $ssh_command = "/usr/bin/apc-switched-pdu-ssh-control.exp $host $ssh_user $ssh_pass $outlet $cmd";

my $ssh_output = `$ssh_command 2>&1`;
if ( $? == 0 )
{
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
   use vars qw( $opt_help $opt_host $opt_ssh_user $opt_ssh_pass $opt_snmp_user $opt_snmp_pass $opt_outlet $opt_cmd);

   Getopt::Long::Configure( "no_ignore_case", "bundling");
   GetOptions( "help",
               "host=s",
               "ssh-user=s",
               "ssh-pass=s",
               "snmp-user=s",
               "snmp-pass=s",
               "outlet=i",
               "cmd=s");

   if ($opt_help)
   {
      &print_help;
      exit(0);
   }

   if(!$opt_host || !$opt_ssh_user || !$opt_ssh_pass || 
      !$opt_snmp_user || !$opt_snmp_pass || !$opt_outlet || !$opt_cmd)
   {
      &print_help;
      die "Error: missing arguments.\n";
   }

   $host = $opt_host;
   $ssh_user = $opt_ssh_user;
   $ssh_pass = $opt_ssh_pass; 
   $snmp_user = $opt_snmp_user;
   $snmp_pass = $opt_snmp_pass;
   $outlet = $opt_outlet;
   $cmd = $opt_cmd;
}


# --------------- print help information ------------------------------
sub print_help {

   print <<EOF;

This script will control a single outlet on an APC power strip.  It will 
first attempt communication via SNMP and then fall back to ssh if that fails.

usage: apc-switched-pdu-hybrid-control.pl <options>

options:
   --help                   print this help and exit
   --host        <STRING>   hostname of APC unit
   --ssh-user    <STRING>   ssh username for APC unit
   --ssh-pass    <STRING>   ssh password for APC unit
   --snmp-user   <STRING>   SNMP username for APC unit
   --snmp-pass   <STRING>   SNMP authentication pass phrase (MD5) for APC unit
   --outlet      <INTEGER>  APC outlet to control
   --cmd         <on|off|reboot> control command to send (on, off, or reboot)

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


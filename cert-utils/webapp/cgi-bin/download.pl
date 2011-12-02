#!/usr/bin/perl -W

#
# To allow setuid to work properly:
#
#	chown root:apache download.pl && chmod ug+xs,o-rwx download.pl
#
# This will allow the certificates to be created in the user's
# home directory with the correct permissions.

use strict;
use Data::Dumper;
use IPC::Run qw(run);
use CGI qw/:standard/;

use constant TRACE => 1;
use constant DEBUG => 1;

# access control
my $ALLOWED_USERS  = { 'apache' => 1 };    ## users allowed to run this script
my $ALLOWED_GROUPS = { 'cuuser' => 1 };    ## group(s) users must be in to r/w

# debugging
$ALLOWED_USERS->{'www-data'} = 1;
$ALLOWED_GROUPS->{'adm'}     = 1;

# script settings
my $LOG_FILE = "/tmp/ofs-certs.log";

# parameters
my $USER = '';    ## the user we're trying to read/write the certificate for
my $MODE = '';    ## read or write
my $TYPE = '';    ## certificate type: user or proxy

# certificate settings
my $USER_DIR  = "/home";
my $PROXY_DIR = "/tmp";
my $GLOBUS_DIR = "/usr/local/globus-5.0.4";

# calculated later
my $CERTS    = {};       ## hash of certificate functions
my $USER_UID = undef;    ## the user's uid for setuid
my $USER_GID = undef;    ## the user's gid for setgid

# hold on to the original uid's and gid's so we can reset them
my $R_UID = $<;          ## real uid
my $E_UID = $>;          ## effective uid
my $R_GID = $(;          ## real gid
my $E_GID = $);          ## effective gid

################# Helper Functions #################

sub debug {
	last if !DEBUG;
	print LOG ( scalar @_ && $_[0] ne "\n" ? "DEBUG - " . shift @_ : "\n" );
}

sub trace {
	last if !TRACE;
	print LOG ( scalar @_ && $_[0] ne "\n" ? "TRACE - " . shift @_ : "\n" );
}

sub cat {

	# mimic the cat command except that it returns the contents of
	# a file instead of printing it to STDOUT

	my ( $file ) = @_;

	open( IN, "<", $file ) or die "Can't open file: '$file'";
	my $string = join( '', <IN> );
	close( IN ) or warn "Can't close file: $file";

	return $string;
} ## end sub cat

sub is_valid_cert : {
	trace( "Begin is_valid_cert\n" );

	# validate that the certificate exists, is a plain file (not a symlink
	# or something like that), and is a valid certifcate
	my ( $cert ) = @_;

	if ( !( -f $cert && scalar( stat( $cert ) ) ) ) {
		return ( $! = 0 );    ## $! gets set if the file doesn't exist
	}

	debug( "Running openssl command: \$? => $?\n" );
	my $valid = system( "/usr/bin/openssl x509 -noout -in $cert 2> /dev/null" ) == 0;
	debug( "Is valid: cert => $cert, valid => $valid\n" );

	trace( "End is_valid_cert\n" );

	return $valid;
} ## end sub is_valid_cert :

################# Globus User Certificate #################

sub user_cert_get_fn : {
	return $USER_DIR . "/" . $USER . "/.globus/usercert.pem";
}

sub user_cert_read : {
	trace( "Begin user_cert_read\n" );

	my $fn = user_cert_get_fn();

	if ( !is_valid_cert( $fn ) ) {
		user_cert_write();
	}

	my $cert = cat( $fn );
	debug( "User Cert:\n\n$cert\n\n" );

	# print headers
	print "Content-Type: text/plain\n";
	print "Content-Disposition: attachment;filename=cert.1\n";
	print "\n";

	# print content
	print $cert;

	trace( "End user_cert_read\n" );
} ## end sub user_cert_read :

sub user_cert_write : {
	trace( "Begin user_cert_write\n" );

	my ( $fn, @cmd, $globus_uid, $capp );

	$fn = user_cert_get_fn();
	last if is_valid_cert( $fn );
	
	# required by grid-* commands
	$ENV{GLOBUS_LOCATION} = "$GLOBUS_DIR";
	
	@cmd = ( "$GLOBUS_DIR/bin/grid-cert-request", "-nopassphrase" );

	debug( "Running '@cmd'\n\n" );
	run( \@cmd, \*STDIN, \*LOG, \*LOG ) or die "'@cmd' failed: $!";
	debug( "\n" );
	
	# the next command has to be run as user "globus"
	$< = 0;
	die "Set UID for the grid-ca-sign cmd failed: $!" if $!;
	
	@cmd = (
		"$GLOBUS_DIR/bin/grid-ca-sign",
			"-dir", "/home/globus/.globus/simpleCA",
			"-in", "/home/$USER/.globus/usercert_request.pem",
			"-out", "/home/$USER/.globus/usercert.pem",
			"-passin", "file:/home/globus/.globus/simpleCA/private/capp"
	);
	
	debug( "Running '@cmd'\n\n" );
	run( \@cmd, \*STDIN, \*LOG, \*LOG ) or die "'@cmd' failed: $!";
	debug( "\n" );
	
	# reset the real uid to the user
	$< = $USER_UID;
	die "Reset UID for the grid-ca-sign cmd failed: $!" if $!;
	
	trace( "End user_cert_write\n" );

} ## end sub user_cert_write :

################# Globus Proxy Certificate #################

sub proxy_cert_get_fn : {
	return $PROXY_DIR . "/x509up_u" . $USER_UID;
}

sub proxy_cert_read : {
	trace( "Begin proxy_cert_read\n" );

	# make sure we get an updated one
	proxy_cert_write();

	my $fn   = proxy_cert_get_fn();
	my $cert = cat( $fn );
	debug( "Proxy Cert:\n\n$cert\n\n" );

	# print headers
	print "Content-Type: text/plain\n";
	print "Content-Disposition: attachment;filename=cert.0\n";
	print "\n";

	# print content
	print $cert;

	trace( "End proxy_cert_read\n" );

} ## end sub proxy_cert_read :

sub proxy_cert_write : {
	trace( "Begin proxy_cert_write\n" );
	my ( $fn, @cmd, $policy, $hours );

	# the proxy cert relies on the user cert so check it first
	$fn = user_cert_get_fn();

	if ( !is_valid_cert( $fn ) ) {
		user_cert_write();
	}

	$policy = "/home/$USER/.globus/cert-policy";
	$hours    = param( 'exp' );

	# untaint and check the expiration, then convert it to hours
	$hours =~ /([0-9]+)/g
		or die "Invalid number of days for proxy certificate expiration.";
	$hours = int( "$1" );

	# the expiration has to be between 1 and 14 days
	if ( $hours < 1 || $hours > 14 ) {
		die "The number of days until expiration of the proxy certificate is invalid: exp => $hours";
	}

	# convert days to hours
	$hours = $hours * 24;
	
	# for some reason we have to set the effective UID and GID
	# in order to write to the policy file and run grid-proxy-init
	$> = $USER_UID;
	warn "Set effective UID for proxy cert failed: $!" if $!;
	
	$) = $USER_GID;
	warn "Set effective GID for proxy cert failed: $!" if $!;
	
	open( POLICY, ">", $policy ) or die "Can't open cert-policy file: $!";
	print POLICY "$USER_UID/$USER_GID";
	close( POLICY ) or warn "Can't close cert-policy file: $!";
	
	# FIXME: for some reason opening the policy file is setting
	# $! to "Inappropriate ioctl for device"
	$! = 0;
	
	# required by grid-* commands
	$ENV{GLOBUS_LOCATION} = "$GLOBUS_DIR";
	
	@cmd = ( 
		"$GLOBUS_DIR/bin/grid-proxy-init",
			"-valid", "$hours:0", 
			"-policy", "$policy", 
			"-pl", "id-ppl-anyLanguage"
	);
	
	debug( "Running '@cmd'\n\n" );
	run( \@cmd, \*STDIN, \*LOG, \*LOG ) or die "'@cmd' failed: $!";
	debug( "\n" );
	
	$> = $E_UID if ( $> != $E_UID );
	warn "Reset effective UID ($> -> $E_UID) for proxy cert failed: $!" if $!;
	
	$) = $E_GID if ( $) != $E_GID );
	warn "Reset effective UID ($) -> $E_GID) for proxy cert failed: $!" if $!;

} ## end sub proxy_cert_write :

# Hash the certificate fuctions which enables what can be done
$CERTS->{user}{read}  = \&user_cert_read;
$CERTS->{proxy}{read} = \&proxy_cert_read;

#$CERTS->{user}{write}  = \&user_cert_write;
#$CERTS->{proxy}{write} = \&proxy_cert_write;

################# MAIN #################

BEGIN: {
	open( LOG, '>>', $LOG_FILE ) or die "Can't open log file: $!";
	trace( "Begin BEGIN\n" );

	$SIG{__DIE__} = sub {
		print LOG "\nERROR - @_\n" if DEBUG;
		close( LOG ) or warn "Can't close log file: $!";

		# show the default apache error message
		print header(
			-type     => "text/html",
			-location => "/error/HTTP_INTERNAL_SERVER_ERROR.html.var"
		);

		exit 1;
	};

	$SIG{__WARN__} = sub {
		print LOG "WARN - @_\n";
	};

	if ( !exists( $ENV{'REMOTE_USER'} ) ) {
		die "Unauthorized access attempt from $ENV{'REMOTE_ADDRESS'}";
	}

	debug( "\n\n**** BEGIN - $ENV{'REMOTE_USER'} - " . localtime() . " ****\n\n" );
	trace( "End BEGIN\n" );
} ## end BEGIN:

INIT: {
	trace( "Begin INIT\n" );

	$USER = $ENV{'REMOTE_USER'};
	$MODE = param( 'm' ) || "read";
	$TYPE = param( 't' );

	# untaint the query params from the request
	$USER && $USER =~ /^([0-9a-zA-Z_-]+)$/
		or die "User parameter not given or contains invalid charaters: '$USER'";
	$USER = "$1";

	defined( $TYPE )
		or die "The certificate type parameter(t) was not provided.";
	$TYPE =~ /^(user|proxy)$/
		or die "The certificate parameter is not valid: '$TYPE'";
	$TYPE = "$1";

	defined( $MODE )
		or die "The mode parameter(m) was not provided.";
	$MODE && $MODE =~ /^(read|write)$/
		or die "The mode parameter is not valid: '$MODE'";
	$MODE = "$1";

	debug( "Query params: user => $USER, type => $TYPE, mode => $MODE\n" );

	# make %ENV safe since we're using setuid
	$ENV{'PATH'} = '/bin:/usr/bin';
	delete @ENV{qw(IFS CDPATH ENV BASH_ENV)};

	trace( "End INIT\n" );
} ## end INIT:

CHECK_UID: {
	trace( "Begin CHECK_UID\n" );

	# check the user trying to run the script
	my ( $name, $passwd, $uid, $gid, $quota, $comment, $gcos, $dir, $shell, $expire )
		= getpwuid( $< );

	# make sure they're in the list of allowed users
	if ( !exists( $ALLOWED_USERS->{$name} ) || !$ALLOWED_USERS->{$name} ) {
		die "User '$name' is not allowed to run this program.";
	}

	debug( "Real User: $name\n" );
	trace( "End CHECK_UID\n" );
} ## end CHECK_UID:

CHECK_USER: {
	trace( "Begin CHECK_USER\n" );

	# get the user we want to run the script as
	my ( $name, $passwd, $uid, $gid, $quota, $comment, $gcos, $dir, $shell, $expire )
		= getpwnam( $USER );

	if ( !( defined $uid && defined $gid ) ) {
		die "User not found.";
	}

	if ( $uid < 100 || $gid < 100 ) {
		die "UID or GID under minimum.";
	}

	# make sure the user's home directory exists
	if ( !-d "$USER_DIR/$name" ) {
		die "User home directory does not exist.";
	}

	# make sure the user is in one of the allowed groups
	my ( $ok, $err, $groups ) = ( 0, "", "" );
	my @cmd = ( "/usr/bin/id", "-nG", "$USER" );

	debug( "Running '@cmd'\n" );
	if ( !run( \@cmd, \*STDIN, \$groups, \$err ) ) {
		print LOG "\n$err" if DEBUG;
		die "'@cmd' failed: $!";
	}

	foreach my $group ( split( " ", $groups ) ) {
		if ( exists( $ALLOWED_GROUPS->{$group} ) && $ALLOWED_GROUPS->{$group} ) {
			debug( "User allowed by group: $group\n" );
			$ok = 1;
			last;
		}
	}

	if ( !$ok ) {
		die "User not in allowed group list.";
	}

	$USER_UID = $uid;
	$USER_GID = $gid;

	$ENV{USER}    = $name;
	$ENV{HOME}    = $dir;
	$ENV{LOGNAME} = $name;

	trace( "End CHECK_USER\n" );
} ## end CHECK_USER:

CHECK_CERT: {
	trace( "Begin CHECK_CERT\n" );

	# make sure the cert is valid
	if ( !exists( $CERTS->{$TYPE} ) ) {
		die "Certificate '$TYPE' not allowed in certificate list.";
	}

	if ( !exists( $CERTS->{$TYPE}{$MODE} ) ) {
		die "Certificate '$TYPE' has no '$MODE' mode.";
	}

	trace( "End CHECK_CERT\n" );
} ## end CHECK_CERT:

DL_CERT: {
	trace( "Begin DL_CERT\n" );

	# elevate the script's priviledges in order to read/write to users home directories
	$< = $USER_UID;
	die "Set real UID failed: $!" if $!;

	$( = $USER_GID;
	die "Set real GID failed: $!" if $!;

	debug( "Calling: " . $TYPE . "_cert_" . $MODE . "\n" );

	my $result = $CERTS->{$TYPE}{$MODE}();

	debug( "Result of " . $TYPE . "_cert_" . $MODE . ": $result\n" );

	# reset the uid's and gid's
	$< = $R_UID if ( $< != $R_UID );
	warn "Reset real UID ($< -> $R_UID) failed: $!" if $!;

	$( = $R_GID if ( $( != $R_GID );
	warn "Reset real GID ($( -> $R_GID) failed: $!" if $!;

	trace( "End DL_CERT\n" );
} ## end DL_CERT:

END: {
	trace( "End\n" );
	close( LOG ) or warn "Can't close pipe for log file: $!";
}

__END__

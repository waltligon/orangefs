#!/usr/bin/perl -w

#
# this code parses state machines and dumps a dot file showing the states
#

# init_token() - call before calling next_token()
sub init_token()
{
    # skip to start of prototypes
    while(<>) {
	if ( /%%/ ) { last; }
    }
    while(<>) {
	if ( /\{/ ) { last; }
    }

    $pos = 1;
    @tokens = ();
}

# next_token() - returns next token.  starts with first line after BEGIN PROTOTYPES,
#                ends with line before END PROTOTYPES, returns "" once all tokens are
#                read.
#
#
sub next_token()
{
    while (1) {
	if ($pos >= @tokens) {
	    $pos = 0;
	    if ($_ = <>) {
		if ( /%%/ ) { return ""; }
		
		@tokens = split(/([\s|\*+|\(|\)|,])/);
	    }
	    else { return ""; }
	}
	
	if ($tokens[$pos] =~ /^\s+/ || $tokens[$pos] eq "") { $pos++; }
	else { return $tokens[$pos++]; }
    }
}

# spit_out_header() - call before starting to output code
sub spit_out_header()
{
    printf "digraph state { {\n";
}

# spit_out_state() - prints out line defining state
#
# parameters are state name, function name (or nested state), and a
# boolean indicating if the state was nested
#
sub spit_out_state
{
    my($sn, $fn, $nested) = @_;

    if ($nested) { printf "%s [shape = record, label = \"%s\"];\n", $sn, $fn; }
    else         { printf "%s [shape = ellipse, label = \"%s\"];\n", $sn, $fn; }
}

# spit_out_transition() - prints out dot line describing transition
#
# parameters are state name, label for transition, and next state name
sub spit_out_transition
{
    my($sn, $label, $next_sn) = @_;

    printf "\t%s -> %s [label = \"$label\"];\n", $sn, $next_sn, $label;
}

#
# main loop
#

init_token();

spit_out_header();

# while (1) { print '"', next_token(), '"', "\n"; }

#
# Our states:
# 0 - before start of state
# 1 - reading state name
# 2 - reading sub state machine or function name
# 3 - reading sub state machine name
# 4 - end of function
# 5 - continuing to read type of parameter
#
$state = 0;
$token = next_token();

while (1) {
    SWITCH: {
	if ($state == 0) {
	    if ($token ne "state") {
		printf "state 0, expected \"state\", got \"%s\"\n", $token;
		exit -1;
	    }
	    $token = next_token();
	    $state = 1;
	    last SWITCH;
	}
	if ($state == 1) {
	    $state_name = $token;
	    $token = next_token();
	    if ($token ne "{") {
		printf "state 1, expected \"{\", got \"%s\"\n", $token;
		exit -1;
	    }
	    $token = next_token();
	    $state = 2;
	    last SWITCH;
	}
	if ($state == 2) {
	    if ($token eq "jump") {
		$sub_state_name = next_token();
		chop $sub_state_name;
		spit_out_state($state_name, $sub_state_name, 1);
	    }
	    elsif ($token eq "run") {
		$fn_name = next_token();
		chop $fn_name;
		spit_out_state($state_name, $fn_name, 0);
	    }
	    else {
		printf "state 2, expected \"jump\" or \"run\", got \"%s\"\n", $token;
		exit -1;
	    }

	    $token = next_token();
	    $state = 3;
	    last SWITCH;
	}
	if ($state == 3) {
	    if ($token eq "}") {
		$token = next_token();
		$state = 4;
	    }
	    else {
		$transition_label = $token;
		$token = next_token();
		if ($token ne "=>") {
		    printf "state 2, expected \"=>\", got \"%s\"\n", $token;
		    exit -1;
		}
		$next_state_name = next_token();
		chop $next_state_name;
		spit_out_transition($state_name, $transition_label, $next_state_name);
		$token = next_token();
		$state = 3;
	    }

	    last SWITCH;
	}
	if ($state == 4) {
	    if ($token eq "}") { 
		printf "} }\n";
		exit 0;
	    }
	    else {
		printf "\n";
		$state = 0;
		# $token = next_token(); -- leave token alone
	    }
	    last SWITCH;
	}
    }
}

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

$beginning = 1;

# spit_out_state() - prints out line defining state
#
# parameters are state name, function name (or nested state), and a
# boolean indicating if the state was nested
#
sub spit_out_state
{
    my($sn, $fn, $nested, $ending) = @_;

    if($beginning) {
	$fill_color = "green";
    }
    else {
	$fill_color = "red";
    }

    if ($ending or $beginning){
	$node_style = "style = filled, ";
	$beginning = 0;
    }
    else{
	$node_style = "";
    }

    if ($nested) { 
	printf "%s [%s shape = record, fillcolor = %s, label = \"%s\"];\n",
	    $sn, $node_style, $fill_color, $fn; 
    }
    else         { 
	printf "%s [%s shape = ellipse, fillcolor = %s, label = \"%s\"];\n", 
	    $sn, $node_style, $fill_color, $fn; 
    }
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
	    $ending = 0;
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
	    $target_type = $token;
	    if(($target_type ne "jump") and ($target_type ne "run")) {
		printf "state 2, expected \"jump\" or \"run\", got \"%s\"\n", $token;
		exit -1;
	    }
	    $target_name = next_token();
	    chop $target_name;

	    $token = next_token();
	    $state = 3;
	    last SWITCH;
	}
	if ($state == 3) {
	    if ($token eq "}") {
		#    delay describing the state until we have looked at 
		# at all of the transitions, so we can describe it fully
		$token = next_token();
		if ($target_type eq "jump") {
		    spit_out_state($state_name, $target_name, 1, $ending);
		}
		elsif ($target_type eq "run") {
		    spit_out_state($state_name, $target_name, 0, $ending);
		}
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
		# skip drawing transition at completion of state machine
		if(($next_state_name ne "terminate") and
		   ($next_state_name ne "return")) {
		    spit_out_transition($state_name, $transition_label, $next_state_name);
		}
		# instead, mark it as an ending state
		else {
		    $ending = 1;
		}
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

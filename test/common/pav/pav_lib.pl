sub recover_from_bad {
    my $href = shift;
    my $badnode = shift;

    my @compnodes = @{$href->{'COMPNODES'}};
    my @ionodes = @{$href->{'IONODES'}};
    my @metanodes = @{$href->{'MGR'}};

    if (@compnodes) {
	my $newnode = pop(@compnodes);
	$href->{'COMPNODES'} = \@compnodes;

	foreach $meta (@metanodes) {
		my $tmpnode;
		if ($meta eq $badnode) {
			$tmpnode = $newnode;
		} else {
			$tmpnode = $ionode;
		}
		@tmp = (@tmp, $tmpnode);
	}
	$href->{'MGR'} = \@tmp;

	foreach $ionode (@ionodes) {
	    my $tmpnode;
	    
	    if ($ionode eq $badnode) {
		$tmpnode = $newnode;
	    } else {
		$tmpnode = $ionode;
	    }
	    
	    @tmp = (@tmp, $tmpnode);
	}
	
	$href->{'IONODES'} = \@tmp;
    } else {
	print STDERR "ERROR: all nodes in use! cannot attempt recovery, please add more nodes to your machinefile or remove the bad node '$badnode'\n";
	return(2);
    }

    $rc = create_sessionfile($href);
    if ($rc) {
	print STDERR "ERROR: cannot create session file with recovery info! ... bailing\n";
    return(2);
    } 

    return(1);
}


sub create_sessionfile {
    my $href = shift;

    my $ofile = $href->{'WORKINGDIR'} . "/pvfs_autosession";
    my $val = '';

    if (!open(FH, ">$ofile")) {
	print STDERR "ERROR: cannot open file: $ofile\n";
	return(1);
    }

    foreach $key (keys(%{$href})) {
	if ($key eq "IONODES" || $key eq "COMPNODES" || $key eq "MGR") {
	    $val = join(',', @{$href->{"$key"}});
	} else {
	    $val = $href->{"$key"};
	}

	print FH "$key=$val\n";
    }

    close(FH);
    return(0);
}

sub uniq(@)
{
    my %seen = ();
    my @tmp_uniq;
    my $item;
    foreach $item (@_) {
	push(@tmp_uniq, $item) unless $seen{$item}++;
    }
    return(@tmp_uniq);
}

# executes "$_[1]" instances of remote-execing command "$_[0]" (rsh or ssh),
# giving up after "$_[2]" seconds.  The remote machine will execute "$_[3]".  
# XXX: i don't know what $precmd and $postcmd will do
# Every machine in @nodes will run this command.
#
# all instances of '%node' in $_[3] will be replaced with the node name

sub do_remote_command {
    my $prog = shift;
    my $numprocs = shift;
    my $timeout = shift;
    my $cmd = shift;
    my $precmd = shift;
    my $postcmd = shift;
    my @nodes = @_;
    my $pcount = 0;
    my %nodecommand;

    @nodes = uniq(sort(@nodes));
    foreach $node (@nodes) {
	my $nodecmd = $cmd;
	$nodecmd =~ s/%node/$node/g;
	$nodecmd = "$prog $nodecmd";

	#run Pre cmd
	if ($precmd) {
	    $prc = &$precmd($node);
	    if ($prc) {
		print "WARNING: PreCommand failed on node $node\n";
		next;
	    }
	}
	
	$nodecommand{$nodecmd} = $node;
	push(@commands, $nodecmd);
    }

    my ($todo, $done, $active, $maxrc) = (scalar @commands, 0, 0, 0);
    my ($pid, $rc, %child);

    while(@commands  ||  $done < $todo) {
	while (($pid = waitpid(-1, WNOHANG)) > 0) {
	    if (exists $child{$pid}) {
		$rc = $? >> 8;
		$maxrc = $rc if ($rc > $maxrc);
		if ($rc) {
		    while(wait() > 0){}
		    return($rc, $child{$pid});
		}
		    
		$active-- if ($active > 0);
		$todo--   if ($todo > 0);
	    }
	}

	# No commands or at max active
	unless (@commands && $active < $numprocs) {
	    select undef, undef, undef, 0.1; # sleep 0.1 seconds
	    next;
	} 

	# Fork a command
	unless ($pid = fork()) { # Child
	    my $cmd = pop(@commands);
	    $SIG{ALRM} = \&earlyexit;
	    alarm($timeout);
	    
	    #print "Running remote command $cmd\n";
	    $rc = system($cmd);
	    exit($rc >> 8);
	}
		
	my $node = $nodecommand{pop(@commands)};
	$child{$pid} = $node;
	$active++;
    }


    foreach $node (@nodes) {
	#run Post cmd
	if ($postcmd) {
	    $prc = &$postcmd($node);
	    if ($prc) {
		print "WARNING: PostCommand failed on node $node\n";
		next;
	    }
	}

    }

    return($maxrc);
}    

sub earlyexit {
    print STDERR "ERROR: timeout occurred, killing pid $cmdpid, exiting\n";
    kill(-1, 9);
    exit(1);
}

sub dir_parent {
    my $dir = shift;

    my @breakout = split('/', $dir);
    
    while(!$breakout[0]) {
	shift @breakout;
    }

    while(!(reverse(@breakout))[0]) {
	pop(@breakout);
    }

    pop(@breakout);

    while(!(reverse(@breakout))[0]) {
	pop(@breakout);
    }

    my $newdir = "/" . join('/', @breakout);
    return($newdir);
}

sub read_sessionfile {
    my $session_file = shift;
    my $href = shift;

    my $line_num = 0;

    if (!open(FH, "$session_file")) {
	print STDERR "ERROR: cannot open session file: $session_file\n";
	return(1);
    }

    while(<FH>) {
	$line_num++;
	chomp;
	if (/^#/ || !$_) {
	    next;
	}
	my $line = $_;

	my ($key, $val) = split("=", $line);
	$val =~ s/\"//g;
	
	if ($key eq "IONODES" || $key eq "COMPNODES" || $key eq "MGR") {
	    my @tmp = split(',', $val);
	    $href->{"$key"} = \@tmp;
	} else {
	    $href->{"$key"} = $val;
	}
    }
    
    close(FH);
    return(0);
}

sub read_configfile {
    my $config_file = shift;
    my $href = shift;
    my $line_num = 0;

    if (!open(FH, "$config_file")) {
	print STDERR "ERROR: cannot open config file: $config_file\n";
	return(1);
    }

    while(<FH>) {
	$line_num++;
	chomp;
	if (/^#/ || !$_) {
	    next;
	}
	my $line = $_;

	# only split the key=val, not embedded '='s
	my ($key, @val) = split("=", $line);
	$val = join("=", @val);
#	print "key: $key val: $val\n";

        if ($val =~ s/^`(.*)`$//) {
            $val = `$1`;
            chomp($val);
        } else {
	    $val =~ s/"//g;
	}
	
	$href->{"$key"} = $val;

	if ($val =~ /\$/) {
#	    print "Found var inside val: $val\n";
	    $val =~ /\$(\w*)/;
	    my $variable = $1;

	    if (!$href->{"$variable"}) {
		print STDERR "ERROR: undefined variable '$variable' at line $line_num in config file\n";
		return(1);
	    } else {
		$href->{$key} =~ s/\$$variable/$href->{"$variable"}/g;
	    }
	}
    }
    
    close(FH);
    return(0);
}



1

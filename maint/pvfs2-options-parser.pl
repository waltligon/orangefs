#!/usr/bin/perl -w

use strict;

sub generate_option_doc;

if($#ARGV < 2)
{
    print "usage: pvfs2-options-parser.pl <version> <options file> <debug file>\n";
    exit 1;
}

my $version = shift;
my $file = shift;
my $debugfile = shift;

# read options file

unless(open(OPTION, "<", $file))
{
    print "Can't open specified file: $file: $!\n";
    exit 1;
}
my $optionfd = \*OPTION;
my $optstr = "";
my $line = undef; 
while(defined($line = <$optionfd>))
{
    if($line =~ /^\s*\*[^\/].*$/)
    {
	$line =~ s/^\s*\*//;
    }
    $optstr .= $line;
}

close($optionfd);

# move to options struct
my ($optsdoc, $optdef) = $optstr =~ /\/\*((?:[^*]*(?:\*(?!\/))?[^*]*)*)\*\/\s*static\s*const\s*configoption_t[^{]+\{(.+)LAST_OPTION.*$/sg;

print << "EOF"
<? include("../../../header.php"); ?>

<title>PVFS2 $version: Config File Description</title>
<style type="text/css">
<!--
table.tabletype1 {
   border-width: 1px ; 
   border-style: solid; 
   border-color: #000000; 
   background: #000000; 
   border-top-width: 0px;
   font-family: "Lucida Grande", Verdana, Geneva, Arial, sans-serif;
   font-size: small;
} 

table.tabletype2 {
   border-width: 0px ; 
   border-style: solid; 
   border-color: #000000; 
   background: #000000;
   font-family: "Lucida Grande", Verdana, Geneva, Arial, sans-serif;
   font-size: small;
}

.nav {
   font-family: lucida; 
   font-size: 14px;
} 

.nav_dark_gray {
   background: #606060; 
   font-family: "Lucida Grande", Verdana, Geneva, Arial, sans-serif;
   font-size: small;
} 

.nav_light_gray {
   background: #818181; 
   font-family: "Lucida Grande", Verdana, Geneva, Arial, sans-serif;
   font-size: small;
} 

.nav_white {
   background: #ffffff; 
   font-family: "Lucida Grande", Verdana, Geneva, Arial, sans-serif;
   font-size: small;
} 

.nav_light {
   background: #cccccc; 
   font-family: "Lucida Grande", Verdana, Geneva, Arial, sans-serif;
   font-size: small;
} 
.nav_red {
   background: #c20000; 
   font-family: "Lucida Grande", Verdana, Geneva, Arial, sans-serif;
   font-size: small;
}

.nav_dark_gray_tiny {
   background: #606060; 
   font-family: "Lucida Grande", Verdana, Geneva, Arial, sans-serif;
   font-size: 10px;
} 

.nav_light_gray_tiny {
   background: #818181; 
   font-family: "Lucida Grande", Verdana, Geneva, Arial, sans-serif;
   font-size: 10px;
} 

.nav_white_tiny {
   background: #ffffff; 
   font-family: "Lucida Grande", Verdana, Geneva, Arial, sans-serif;
   font-size: 10px;
} 

.nav_light_tiny {
   background: #cccccc; 
   font-family: "Lucida Grande", Verdana, Geneva, Arial, sans-serif;
   font-size: 10px;
} 

.nav_red_tiny {
   background: #c20000; 
   font-family: "Lucida Grande", Verdana, Geneva, Arial, sans-serif;
   font-size: 10px;
}

.head {
   font-family: tahoma; 
   font-size: 9px; 
   color: #dddddd;
}

.pix {
   font-family: verdana; 
   font-size: 0px;
}

a.no-decoration:link { 
   text-decoration: none; 
   font-family: lucida; 
   font-size: 14px;
}

a.no-decoration:active { 
   text-decoration: none; 
   font-family: lucida; 
   font-size: 14px;
}

a.no-decoration:visited { 
   text-decoration: none; 
   font-family: lucida; 
   font-size: 14px;
}  

body {
    font-family: "Lucida Grande", Verdana, Geneva, Arial, sans-serif;
    font-size: small;
}

-->
</style>

</head>

<? include("../../../top.php"); ?>

<body id="documentation">
<h1>PVFS2 $version: Config File Description</h1>
<p>
$optsdoc
<p>
EOF
	;;

my @options = $optdef =~ /\s*((?:\/\*.*?\*\/)?\s*{[^}]*})/sg;

my @options_array = ();

for my $o (@options)
{
    my $comment = undef;
    if($o =~ /\/\*((?:[^*]*(?:\*(?!\/))?[^*]*)*)\*\//s)
    {
	$comment = $1;

	# everything after the comment
	$o = $';
    }
    
    $o =~ /\s*\{/s;
    $o = $';
    my @opts = $o =~ /(?:,?\s*([^,}]+))/gs;

    $opts[0] =~ s/"//g;

    push(@options_array, 
	{NAME => $opts[0], 
	 TYPE => $opts[1],
	 CALLBACK => $opts[2],
	 INFO => $opts[3],
	 CONTEXT => $opts[4],
	 DEFAULT => $opts[5],
	 COMMENT => $comment});
}

my ($contexts, $options) = find_contexts(\@options_array);

my %opts_by_context = ();
for my $opt (@{$options})
{
    my @ctxs = split(/\|/, $opt->{CONTEXT});
    for my $ctx (@ctxs)
    {
	if(exists $opts_by_context{$ctx})
	{
	    push(@{$opts_by_context{$ctx}}, $opt->{NAME});
	}
	else
	{
	    $opts_by_context{$ctx} = [$opt->{NAME}];
	}
    }
}

my %ctxs_by_context = ();
for my $ctx (keys %{$contexts})
{
    my $pctx = $contexts->{$ctx}->{CONTEXT};
    if(exists $ctxs_by_context{$pctx})
    {
	push(@{$ctxs_by_context{$pctx}}, $ctx);
    }
    else
    {
    	$ctxs_by_context{$pctx} = [$ctx];
    }
}

print <<"EOF"
<p>
<a name="OptionsList"><h3>Options Grouped by Context</h3></a>
This is the summarized list of all available options, grouped by the
context where they are allowed to appear.
<p>
EOF
	;;

print_context(
    $contexts, "CTX_GLOBAL", \%ctxs_by_context, \%opts_by_context, "");

print "<a name=\"OptionDetails\"><h3>Option Details</h3></a>";

for my $opt (@{$options})
{
    generate_option_doc($contexts, $opt);
}

print <<"EOF"
<p>
<a name="ContextsList"><h2>Context List</h2></a>
This is the list of possible Contexts that can be used in the config
files in this version of PVFS2.
<p>
EOF
	;;

generate_context_doc($contexts);

sub generate_option_doc
{
    my $contexts = shift;
    my $opt = shift;
    my ($comment, $name, $type, $default);
   
    $comment = $opt->{COMMENT};
    $name = $opt->{NAME};
    $type = $opt->{TYPE};

    my $pcstr = join("<br>", 
	map { "<a href=\"#" . $contexts->{$_}->{NAME} . 
	      "\">" . $contexts->{$_}->{NAME} . 
	      "</a>" 
	  } split(/\|/, $opt->{CONTEXT}));

    $default = $opt->{DEFAULT};

    $comment = "" if !defined($comment);
    $comment =~ s/\n\s*\n/<p>/sg;

    if($comment =~ /__EVENTLOGGING__/ && defined($debugfile))
    {
	my $dbg_str =<<"EOF"
<table cellspacing="1" cellpadding="6" class="tabletype2">
<tr class="nav_light_gray_tiny"><th>Name</th><th>Log output for:</th></tr>
EOF
	;;
	# special case to replace __EVENTLOGGING__ with possible log values
	my $dbghash = get_debug_values($debugfile);
	for my $dbg (sort keys %{$dbghash})
	{
	    $dbg_str .= "<tr class=\"nav_light_tiny\"><td>" . $dbg . "</td><td>";
	    if($dbghash->{$dbg} ne "")
	    {
		$dbg_str .= $dbghash->{$dbg};
	    }
	    $dbg_str .= "</td></tr>";
	}
	$dbg_str .= "</table>";
	$comment =~ s/__EVENTLOGGING__/$dbg_str/;
    }

    $default =~ s/"//g;
    $default =~ s/NULL/None/;

    $type = "String" if($type =~ /ARG_STR/);
    $type = "Integer" if($type =~ /ARG_INT/);
    $type = "List" if($type =~ /ARG_LIST/);

    print << "EOF"
<a name="$name"/>
<table class="tabletype2" cellspacing="1" cellpadding="6" width="100%">
<tr class="nav_red"><td width="20%">Option:</td><td><b>$name</b></td></tr>
<tr class="nav_light_gray"><td>Type:</td><td>$type</td></tr>
<tr class="nav_light"><td>Contexts:</td><td>$pcstr</td></tr>
<tr class="nav_light_gray"><td>Default Value:</td><td>$default</td></tr>
<tr class="nav_light"><td align="justify">Description:</td><td>$comment</td></tr>
</table>
<p>
EOF
    ;;
}

sub find_contexts
{
    my $options = shift;
    my @optsarray = @{options};
    my @opts = ();
    my %contexts = ();

    $contexts{"CTX_GLOBAL"} = 
    	{NAME => "Global",
	 CONTEXT => "None",
     	 COMMENT => "Global Context"};

    for my $ind (0..$#optsarray)
    {
	if($options->[$ind]->{TYPE} =~ /ARG_NONE/)
	{
	    # this is a context option
	    if($options->[$ind]->{NAME} =~ /<([^\/]*)>/)
	    {
		$options->[$ind]->{NAME} = $1;
		my $key = $options->[$ind]->{NAME};
		$key =~ tr/[a-z]/[A-Z]/;
	    	$contexts{"CTX_" . $key} = $options->[$ind];
	    }
	}
	else
	{
	    push(@opts, $options->[$ind]);
	}

    }

    return (\%contexts, \@opts);
}

sub generate_context_doc
{
    my $ctxp = shift;
    my %contexts = %{$ctxp};

    for my $ctx (values %contexts)
    {

	my $name = $ctx->{NAME};
	my $pc = $contexts{$ctx->{CONTEXT}}->{NAME};
	if(!defined($pc))
	{
	    $pc = "None";
	}
	my $comment = $ctx->{COMMENT};
    	$comment =~ s/\n\s*\n/<p>/sg;

	$name =~ s/"//g;

	print << "EOF"
<a name="$name">
<table class="tabletype2" cellspacing="1" cellpadding="6" width="100%">
<tr class="nav_red"><td width="20%">Context:</td><td><b>$name</b></td></tr>
<tr class="nav_light_gray"><td>Parent Context:</td><td><a href="#$pc">$pc</a></td></tr>
<tr class="nav_light"><td align="justify">Description:</td><td><p>$comment</td></tr>
</table>
</a>
<p>
EOF
	;;
    }
}

sub get_debug_values
{
    my $fname = shift;
    my ($comment, $logname);
    my %events = ();

    unless(open(DEBUGF, "<", $debugfile))
    {
	print "Can't open specified file: $debugfile: $!\n";
	exit 1;
    }

    my $line = undef;
    my $content = "";

    while(defined($line = <DEBUGF>))
    {
	$content .= $line;
    }

    close(DEBUGF);

    if($content =~ /__keyword_mask_t[^{]*{/s)
    {
	$content = $';
    }

    $content =~ /(.*?)};/s;
    $content = $1;
    
    my @vals = $content =~ /\s*,?\s*((?:\/\*.*?\*\/)?\s*{[^}]*})/sg;

    for my $v (@vals)
    {
	$comment = "";
	if($v =~ /\/\*(.*)\*\//s)
	{
	    $v = $';
	    $comment = $1;
	    $comment =~ s/\*//;
	    $comment =~ s/\n//;
	}

	$v =~ /\s*{\s*\"([^"]*)\"\s*,[^}]*}/s;
	$logname = $1;

	$events{$logname} = $comment;
    }

    return \%events;
}

sub print_context
{
    my $contexts = shift;
    my $tctx = shift;
    my $ctxmap = shift;
    my $optsmap = shift;
    my $indent = shift;

    for my $opt (@{$optsmap->{$tctx}})
    {
	print "$indent<a href=\"#$opt\">$opt</a><br>";
    }

    if(defined($ctxmap->{$tctx}) && scalar(@{$ctxmap->{$tctx}}) > 0)
    {
	print "<p>";
    }

    for my $ctx (@{$ctxmap->{$tctx}})
    {
	my $ctxname = $contexts->{$ctx}->{NAME};
	print "$indent&lt;<a href=\"#$ctxname\">$ctxname</a>&gt;<br>";
    
	print_context(
	    $contexts, $ctx, $ctxmap, $optsmap, 
	    $indent . "&nbsp;&nbsp;&nbsp;&nbsp;");

	print "$indent&lt;/<a href=\"#ctxname\">$ctxname</a>&gt;<p>";
    }
}

print "<? include(\"../../../bottom.php\"); ?>";

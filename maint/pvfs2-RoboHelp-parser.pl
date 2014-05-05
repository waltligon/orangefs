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
    $line =~ s/<c>/<span class="inline_code">/g;
    $line =~ s/<\/c>/<\/span>/g;
    $line =~ s/<i>/<span class="inline_code" style="font-style: italic;">/g;
    $line =~ s/<\/i>/<\/span>/g;
    $optstr .= $line;
}

close($optionfd);

# move to options struct
my ($optsdoc, $optdef) = $optstr =~ /\/\*((?:[^*]*(?:\*(?!\/))?[^*]*)*)\*\/\s*static\s*const\s*configoption_t[^{]+\{(.+)LAST_OPTION.*$/sg;

#
# Some boiler-plate text
#
print << "EOF"
<?xml version="1.0" encoding="utf-8" ?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Transitional//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
<head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8" />
<meta name="template" content="basic_page.htt" />
<meta name="generator" content="Adobe RoboHelp 9" />
<title>admin_ofs configuration file</title>
<link rel="StyleSheet" href="omni.css" type="text/css" />
</head>
<body style="margin-top: 8px;">
<h1 class="body">OrangeFS Configuration File</h1>
<p class="body">The OrangeFS configuration file is copied to all servers 
 as a single reference point for operation and performance. This is the 
 file in which you specify settings and preferences for the file system. 
 During installation, you use a program called <span class="inline_code">pvfs2-genconfig</span> 
 to automatically generate the OrangeFS configuration file.<span class="inline_code"> 
 </span>The program presents a series of prompts, enabling you to enter 
 basic required settings.</p>
<p class="body">While <span class="inline_code">pvfs2-genconfig</span> 
 is designed to query you about the most important options, there are many 
 additional options with default values that are bypassed during installation.
 After installation, you can revisit the configuration file to 
 make changes and additions from a broad selection of options for:</p>
<ul type="disc">
	<li><p class="body">Server reconfiguration</p></li>
	<li><p class="body">Performance-tuning</p></li>
	<li><p class="body">Storage optimization</p></li>
	<li><p class="body">Troubleshooting</p></li>
</ul>
<p class="spacer">&#160;</p>
<p class="emphasis"><?rh-udv_start name="Note" ?><span style="font-weight: bold;">Note&#160;&#160;&#160;&#160;&#160;</span><?rh-udv_end ?>After 
 installation, any time you change the configuration file, you will need 
 to recopy it to all servers in your OrangeFS installation and also restart 
 each server.</p>
<p class="spacer">&#160;</p>
<p class="emphasis"><?rh-udv_start name="Note" ?><span style="font-weight: bold;">Note&#160;&#160;&#160;&#160;&#160;</span><?rh-udv_end ?>This list
 of options is generated automatically from program comments that have not
 been edited for spelling and grammar.</p>
<h2>What's Inside</h2>
<p>The configuration file is a simple text file that can be opened and 
 manually modified. </p>
<p>It is organized into a number of option categories called contexts. 
 Each context is bracketed by tags and includes a list of one or more option-value 
 pairs, as shown in this example:</p>
<p class="code">&lt;<span style="font-style: italic; font-family: 'Courier New', monospace;">ContextName</span>&gt; 
 &#160;&#160;&#160;&#160;&#160;<br />
&#160;&#160;&#160;&#160;&#160;<span style="font-style: italic; font-family: 'Courier New', monospace;">Option1Name 
 Option1Value</span><br />
&#160;&#160;&#160;&#160;&#160;<span style="font-style: italic; font-family: 'Courier New', monospace;">Option2Name 
 Option2Value</span> <br />
&lt;/<span style="font-style: italic; font-family: 'Courier New', monospace;">ContextName</span>&gt; 
 </p>
<p class="spacer">&#160;</p>
<p>When a server is started, the options associated with its <span class="inline_code">server-alias</span> 
 in the configuration file are executed.</p>
<p>An option cannot span more than one line, and only one option can be 
 specified on each line. The <i>OptionValue</i> should be formatted based 
 on the option's type: </p>
<ul>
	<li><p>Integer - must be an integer value </p></li>
	<li><p>String - must be a string without breaks (newlines) </p></li>
	<li><p>List - a set of strings separated by commas </p></li>
</ul>
<p class="spacer">&#160;</p>
<p>Options must be defined within a specified context or set of contexts. 
 Sub-contexts must be defined within their specified parent contexts. </p>
<p>For example, the <i>Range</i> option is specified in either the <i>DataHandleRanges</i> 
 or <i>MetaHandleRanges</i> contexts. Both of those contexts are specified 
 to be defined in the <i>FileSystem</i> context. </p>
<p>Options and contexts that appear in the top-level (not defined within 
 another context) are considered to be defined in a special <i>Global</i> 
 context. Many options are only specified to appear within the Default 
 context, which is a context that allows a default value to be specified 
 for certain options. The options detailed below specify their type, 
 the context where they appear, a default value, and description. The default 
 value is used if the option is not specified. Options without default 
 values must be defined.</p>
<h2>Option and Context Descriptions</h2>
<p>The remainder of this topic is a reference for all options in the configuration 
 file, grouped by the contexts in which they are allowed to be used. </p>
<p class="spacer">&#160;</p>
<h3>Links</h3>
<p>Click any link from the following list to find more information about 
 the associated option or context.</p>
EOF
	;;
#
# End boiler-plate text
#

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
    my $index = 6;
    while (defined $opts[$index] and length $opts[$index] > 0)
    {
	$opts[5] = $opts[5] . ", " . $opts[$index];
	$index = $index + 1;
    }

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
    my @pctxs = split(/\|/, $contexts->{$ctx}->{CONTEXT});
    for my $pctx (@pctxs)
    {
        if(exists $ctxs_by_context{$pctx})
        {
	    push(@{$ctxs_by_context{$pctx}}, $ctx);
        }
        else
        {
    	    $ctxs_by_context{$pctx} = [$ctx];
        }
    }
}


print_context(
    $contexts, "CTX_GLOBAL", \%ctxs_by_context, \%opts_by_context, "");

#
# Some boiler-plate text
#
print <<"EOF"
<p class="spacer">&#160;</p>
<p class="spacer">&#160;</p>
<p><a name="OptionDetails"></a></p>
<h3>Option Descriptions</h3>
<p>This is the list of possible Options that can be used in the config 
 files in this version of OrangeFS.</p>
EOF
	;;
#
# End boiler-plate text
#


for my $opt (@{$options})
{
    generate_option_doc($contexts, $opt);
}

#
# Some boiler-plate text
#
print <<"EOF"
<p class="spacer">&#160;</p>
<p class="spacer">&#160;</p>
<p><a name="ContextsList"></a></p>
<h3>Context Descriptions</h3>
<p>This is the list of possible Contexts that can be used in the configuration 
 file in this version of OrangeFS. </p>
EOF
	;;
#
# End boiler-plate text
#

generate_context_doc($contexts);

sub generate_option_doc
{
    my $contexts = shift;
    my $opt = shift;
    my ($comment, $name, $type, $pcstr, $default, $format1, $format2);
   
    $comment = $opt->{COMMENT};
    $name = $opt->{NAME};
    $type = $opt->{TYPE};
    $pcstr = join("<br>", map {$contexts->{$_}->{NAME}} split(/\|/, $opt->{CONTEXT}));
    $default = $opt->{DEFAULT};

    $comment = "" if !defined($comment);
    $comment =~ s/\n\s*\n/<\/p><p>/sg;

    if($comment =~ /__EVENTLOGGING__/ && defined($debugfile))
    {
	my $dbg_str = <<"EOF"
<table class="tabletype2" cellspacing="1">
	<tr class="nav_light_gray_tiny">
		<th style="padding-left: 6px; padding-top: 6px; padding-right: 6px; 
			padding-bottom: 6px;"><p>Name</p></th>
		<th style="padding-left: 6px; padding-top: 6px; padding-right: 6px; 
			padding-bottom: 6px;"><p>Log output for:</p></th>
			</tr>
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

#
#   Need special case handling if comment is empty
#
    if($comment eq "")
    {
	$comment = "&#160;";
    }

    if ($pcstr eq "")
    {
	$pcstr = "&#160;";
	$format1 = "";
	$format2 = "";
    }
    else
    {
	$format1 = "<a href=\"#";
	$format2 = "\" style=\"color: #0000ff; text-decoration: underline; \">$pcstr</a>";
    }

    $default =~ s/"//g;
    $default =~ s/NULL/None/;

    $type = "String" if($type =~ /ARG_STR/);
    $type = "Integer" if($type =~ /ARG_INT/);
    $type = "List" if($type =~ /ARG_LIST/);

    print << "EOF"
<p><a name="$name"></a> </p>
<table class="tblspec_brdr" cellspacing="0" width="100%">
	<tr class="t1st">
		<td style="width: 20%; padding-left: 6px; padding-top: 6px; padding-right: 6px; 
			 padding-bottom: 6px;"><p>Option:</p></td>
		<td style="padding-left: 6px; padding-top: 6px; padding-right: 6px; 
			 padding-bottom: 6px;"><p><b style="font-weight: bold;">$name</b></p></td>
	</tr>
	<tr class="t2Row">
		<td style="padding-left: 6px; padding-top: 6px; padding-right: 6px; 
			 padding-bottom: 6px;"><p>Type:</p></td>
		<td style="padding-left: 6px; padding-top: 6px; padding-right: 6px; 
			 padding-bottom: 6px;"><p>$type</p></td>
	</tr>
	<tr class="t1Row">
		<td style="padding-left: 6px; padding-top: 6px; padding-right: 6px; 
			 padding-bottom: 6px;"><p>Contexts:</p></td>
		<td style="padding-left: 6px; padding-top: 6px; padding-right: 6px; 
			 padding-bottom: 6px;"><p>$format1$pcstr$format2</p></td>
	</tr>
	<tr class="t2Row">
		<td style="padding-left: 6px; padding-top: 6px; padding-right: 6px; 
			 padding-bottom: 6px;"><p>Default Value:</p></td>
		<td style="padding-left: 6px; padding-top: 6px; padding-right: 6px; 
			 padding-bottom: 6px;"><p>$default</p></td>
	</tr>
	<tr class="last">
		<td style="text-align: justify; padding-left: 6px; padding-top: 6px; 
			 padding-right: 6px; padding-bottom: 6px;"><p>Description:</p></td>
		<td style="padding-left: 6px; padding-top: 6px; padding-right: 6px; 
			 padding-bottom: 6px;"><p>$comment</p></td>
	</tr>
</table>
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

		# A couple of context names have mismatched spellings.
		# Take care of that here.
		$key =~ s/SERVEROPTIONS/SERVER_OPTIONS/;
		$key =~ s/EXPORTOPTIONS/EXPORT/;
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
    my ($bold1, $bold2);

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
	if($comment eq "")
	{
	    $comment = "&#160;";
	}

	$name =~ s/"//g;

	print << "EOF"
<p><a name="$name"></a> </p>
EOF
	;;

	if ($name eq "")
	{
		$name = "&#160;";
		$bold1 = "";
		$bold2 = "";
	}
	else
	{
		$bold1 = "<b style=\"font-weight: bold;\">";
		$bold2 = "</b>";
	}

	print << "EOF"
<table class="tblspec_brdr" cellspacing="0" width="100%">
	<tr class="t1st">
		<td style="width: 20%; padding-left: 6px; padding-top: 6px; padding-right: 6px; 
			 padding-bottom: 6px;"><p>Context:</p></td>
		<td style="padding-left: 6px; padding-top: 6px; padding-right: 6px; 
			 padding-bottom: 6px;"><p>$bold1$name$bold2</p></td>
	</tr>
	<tr class="t2Row">
		<td style="padding-left: 6px; padding-top: 6px; padding-right: 6px; 
			 padding-bottom: 6px;"><p>Parent Context:</p></td>
		<td style="padding-left: 6px; padding-top: 6px; padding-right: 6px; 
			 padding-bottom: 6px;"><p><a href="#$pc" style="color: #0000ff; 
										 text-decoration: underline; ">$pc</a></p></td>
	</tr>
	<tr class="last">
		<td style="text-align: justify; padding-left: 6px; padding-top: 6px; 
			 padding-right: 6px; padding-bottom: 6px;"><p>Description:</p></td>
		<td style="padding-left: 6px; padding-top: 6px; padding-right: 6px; 
			 padding-bottom: 6px;"><p>$comment</p></td>
	</tr>
</table>
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
	print "$indent<a href=\"#$opt\">$opt</a><br />
";
    }

    for my $ctx (@{$ctxmap->{$tctx}})
    {
	my $ctxname = $contexts->{$ctx}->{NAME};
	print "<p>$indent&lt;<a href=\"#$ctxname\">$ctxname</a>&gt;<br />
";
    
	print_context(
	    $contexts, $ctx, $ctxmap, $optsmap, 
	    $indent . "&#160;&#160;&#160;&#160;");

	print "$indent&lt;/<a href=\"#ctxname\">$ctxname</a>&gt;</p>
";
    }
}

#
# Some boiler-plate text
#
print << "EOF"
<p>&#160;</p>
<p class="body">&#160;</p>
<p class="body">&#160;</p>
<p class="body">&#160;</p>
<p class="body">&#160;</p>
<p class="body">&#160;</p>
<p class="body">&#160;</p>
<p>&#160;</p>
</body>
</html>
EOF
	;;
#
# End boiler-plate text
#


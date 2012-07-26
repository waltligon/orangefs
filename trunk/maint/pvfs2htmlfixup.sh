#!/bin/sh

# this is the complement to the text added to the head of all generated .html
# through latex2html-init .  I couldn't find a good way to make latex2html add
# the closing tags and '#include virtual=...' stuff

basename=$(echo $@ | sed -e "s|\(.*\)\.html$|\1|")
target=$(echo $@ | sed -e "s|^.*/\([^/]*\)/[^/]*\.html$|\1|")

prefixes="../../../.."

cp $basename.html $basename.php

perl -p -i -e 's~</HEAD>~<? include("../../../../header.php"); ?></HEAD>~' $basename.php
perl -p -i -e 's~<BODY.*$~<?include("../../../..//top.php"); ?> <body id="documentation">~' $basename.php
perl -p -i -e 's~^.*top_of_page.*$~~' $basename.php
perl -p -i -e 's~</ADDRESS>~</ADDRESS></table></table></table><?include("../../../../bottom.php"); ?>~' $basename.php
perl -p -i -e 's~ALIGN="CENTER"~ALIGN="LEFT"~' $basename.php
perl -p -i -e 's~align="center"~align="left"~' $basename.php
perl -p -i -e "s~$target.html~$target.php~" $basename.php

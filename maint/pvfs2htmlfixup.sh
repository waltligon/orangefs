#!/bin/sh

# this is the complement to the text added to the head of all generated .html
# through latex2html-init .  I couldn't find a good way to make latex2html add
# the closing tags and '#include virtual=...' stuff

perl -p -i -e 's~</HEAD>~<!--#include virtual="style_defs.html" -->\n</HEAD>~' $@
perl -p -i -e 's~</ADDRESS>~</ADDRESS>\n</td>\n</tr>\n</table>\n<!--#include virtual="bottom_of_page.html" -->\n</td>\n</tr>\n</table>~' $@

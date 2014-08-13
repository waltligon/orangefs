#!/bin/sh
# pull each orange module's name from its Makefile.am  \
orangeModules=`find . -name Makefile.am -exec grep ^install: {} \; | \
  awk '{sub(/libmod_/,"",$2);sub(/.la/,"_module",$2);print $2}'`; \
httpdConfig=`${WP_APXS} -q SYSCONFDIR`; \
echo "orangeModules:"$orangeModules":"; \
echo "httpdConfig:"$httpdConfig":"; \
# Look through httpd.conf and remember which orange module \
# is specified in the top-most LoadModule directive. \
firstOrangeModule=`cat "$httpdConfig/httpd.conf" | \
  ${AWK} -v orangeModules="$orangeModules" ' \
    BEGIN { \
      split(orangeModules,omArray,"\n"); \
      for (var in omArray) { \
        associativeOmArray[omArray[var]]; \
      } \
    } \
    { \
      if (index($0,"LoadModule")) { \
        normalize=$0; \
        gsub(/ /,"",normalize); \
        if (match(normalize,/^#/)) { \
          next; \
        } \
        if ($2 in associativeOmArray) { \
          print $2; \
          exit; \
        } \
      } \
    } '`; \
# nuke any un-commented PVFSInit lines that might be in httpd.conf \
sed -i '/^PVFSInit/d' $httpdConfig/httpd.conf; \
# If any LoadModule directives for orange modules were found \
# assign PVFSInit to the first one. \
if [ -n "$firstOrangeModule" ]; then \
  echo "PVFSInit "$firstOrangeModule >> $httpdConfig/httpd.conf; \
fi


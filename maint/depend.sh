#!/bin/sh
DIR="$1"
shift
if [ -n "$DIR" ] ; then
    DIR="$DIR"/
fi
exec $CC -M "$@" | sed -e "s@^\(.*\)\.o:@$DIR\1.d $DIR\1.o $DIR\1-server.o $DIR\1.po:@"

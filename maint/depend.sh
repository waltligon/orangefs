#!/bin/sh
DIR="$1"
shift 1
case "$DIR" in
"" | ".")
gcc -M "$@" | sed -e 's@^\(.*\)\.o:@\1.d \1.o \1-server.o:@'
;;
*)
gcc -M "$@" | sed -e "s@^\(.*\)\.o:@$DIR/\1.d $DIR/\1.o $DIR/\1-server.o:@"
;;
esac
exit $?

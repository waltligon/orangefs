#!/bin/sh
# DIR="$1"
# shift 1
# case "$DIR" in
# "" | ".")
# gcc -M "$@" | sed -e 's@^\(.*\)\.o:@\1.d \1.o \1-server.o:@'
# ;;
# *)
# gcc -M "$@" | sed -e "s@^\(.*\)\.o:@$DIR/\1.d $DIR/\1.o $DIR/\1-server.o:@"
# ;;
# esac
# exit $?

indent --brace-indent0 \
       --comment-indentation0 \
       --indent-level4 \
       --line-length80 \
       --comment-line-length78 \
       --break-function-decl-args \
       --cuddle-do-while \
       --no-space-after-function-call-names \
       --dont-break-procedure-type \
       -st \
       -nut \
       $@

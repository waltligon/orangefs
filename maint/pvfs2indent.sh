#!/bin/sh

indent --brace-indent0 \
       --comment-indentation0 \
       --indent-level4 \
       --line-length120 \
       --comment-line-length78 \
       --break-function-decl-args \
       --cuddle-do-while \
       --no-space-after-function-call-names \
       --dont-break-procedure-type \
       -st \
       -nut \
       $@


CFLAGS = -g -O0 -I../sidcache/

policycomp : policycomp.o parser.o scanner.o codegen.o
		gcc -o policycomp policycomp.o parser.o scanner.o codegen.o

parser.c : parser.y
		bison -v -d --debug -o parser.c parser.y

scanner.c : scanner.l
		flex -oscanner.c scanner.l

clean::
		rm -f *.o policycomp parser.c parser.h scanner.c

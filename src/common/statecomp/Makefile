
LD= gcc
CFLAGS +=\
	-D__STATIC_METHOD_BMI_TCP__\
	-D__STATIC_FLOWPROTO_TEMPLATE__\
	-D__STATIC_FLOWPROTO_BMI_TROVE__\
	-DUSE_BMI_MSGS=1\
	-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE\
	-I src/common/statecomp\
	-I .\
	-Wall -Wstrict-prototypes -pipe -I ../../../include

OBJ= parser.o scanner.o symbol.o stringtbl.o statecomp.o emalloc.o codegen.o

statecomp : $(OBJ)
	$(LD) $(LDFLAGS) -o statecomp $(OBJ)

parser.c : parser.y
	bison -d parser.y -o parser.c

clean::
	rm -f *.o parser.c scanner.c

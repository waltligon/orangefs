DIR := src/common/statecomp

STATECOMP := $(DIR)/statecomp
STATECOMPSRC := \
    $(DIR)/statecomp.c \
    $(DIR)/codegen.c \
    $(DIR)/parser.c \
    $(DIR)/scanner.c

STATECOMPGEN := \
    $(DIR)/scanner.c \
    $(DIR)/parser.c \
    $(DIR)/parser.h \

# shut up warnings for problems from flex-2.5.33
MODCFLAGS_$(DIR)/scanner.c := -D__STDC_VERSION__=0

.SECONDARY: $(STATECOMPGEN)


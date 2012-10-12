#ifndef STDIO_FLAG_CONV
#define STDIO_FLAG_CONV

#include <stdint.h>

uint64_t stdio_flag_to_num(char *str);
uint64_t stdio_or_flags(int n, char **flags);
uint64_t stdio_fsti(char *str);

#endif

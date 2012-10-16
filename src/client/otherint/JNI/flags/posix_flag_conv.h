#ifndef POSIX_FLAG_CONV
#define POSIX_FLAG_CONV

#include <stdint.h>

uint64_t posix_flag_to_num(char *str);
uint64_t posix_or_flags(int n, char **flags);
uint64_t posix_fsti(char *str);

#endif

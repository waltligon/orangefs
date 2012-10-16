#ifndef FLAG_CONV_COMMON_H
#define FLAG_CONV_COMMON_H

#include <string.h>
#include <stdio.h>

#define MAX_FLAG_LENGTH 64
#define MAX_STRING_LENGTH 512
#define MAX_FLAGS 8

#define SETUP_FLAG_STRING_TO_INT                                            \
    int n = num_flags(str);                                                 \
    if(n <= 0)                                                              \
    {                                                                       \
        fprintf(stderr, "error: invalid flag string: %s\n", str);           \
        return -1;                                                          \
    }                                                                       \
    char split_flags[n][MAX_FLAG_LENGTH];                                   \
    memset(split_flags, 0, n * MAX_FLAG_LENGTH);                            \
    char *flag_ptrs[n];                                                     \
    int i;                                                                  \
    for(i = 0; i < n; i++){flag_ptrs[i] = &split_flags[i][0];}              \
    split_flag_string(str, n, flag_ptrs);

#define CMP(F) (strcmp(str, (F)) == 0)
#define P(M) (printf("%llu\n", (long long unsigned int)(M)))

int num_flags(char * str);
int split_flag_string(char *str, 
    int flag_cnt, 
    char **split);

#endif

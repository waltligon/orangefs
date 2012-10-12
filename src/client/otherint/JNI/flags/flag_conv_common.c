#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "flag_conv_common.h"

int num_flags(char * str)
{
    int i;

    /* Make a copy of the input string. */
    char copy[MAX_STRING_LENGTH];
    memset(copy, 0, MAX_STRING_LENGTH);
    strcpy(copy, str);
    
    char *curr_flag_ptr = strtok(copy, "|");
    for(i = 0; curr_flag_ptr != NULL; i++, curr_flag_ptr = strtok(NULL, "|"))
    {
        char curr_flag[MAX_FLAG_LENGTH];
        memset(curr_flag, 0, MAX_FLAG_LENGTH);
        int rc = sscanf(curr_flag_ptr, "%s", curr_flag);
        if(rc <= 0)
        {
            return -1;
        }
    }
    return i;
}

int split_flag_string(char *str, int flag_cnt, char **split)
{
    int i;

    /* Make a copy of the input string. */
    char copy[MAX_STRING_LENGTH];
    memset(copy, 0, MAX_STRING_LENGTH);
    strcpy(copy, str);

    char *curr_flag_ptr = strtok(copy, "|");
    for(i = 0; curr_flag_ptr != NULL; i++, curr_flag_ptr = strtok(NULL, "|"))
    {
        sscanf(curr_flag_ptr, "%s", split[i]);
        printf("%d %s %zd\n", i, split[i], strlen(split[i]));
    }
    return 0;
}

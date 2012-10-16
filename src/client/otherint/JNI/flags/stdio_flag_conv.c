#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "flag_conv_common.h"

/* TODO: verify all functions specified in JNI layer have the appropriate 
 * flags declared (see stdio.c in src/client/usrint) 
 */
uint64_t stdio_flag_to_num(char *str)
{
    if(CMP("SEEK_SET"))
    {
        return SEEK_SET;
    }
    else if(CMP("SEEK_CUR"))
    {
        return SEEK_CUR;
    }
    else if(CMP("SEEK_END"))
    {
        return SEEK_END;
    }
    else if(CMP("_IOFBF"))
    {
        return _IOFBF;
    }
    else if(CMP("_IOLBF"))
    {
        return _IOLBF;
    }
    else if(CMP("_IONBF"))
    {
        return _IONBF;
    }
    else
    {
        printf("ALERT: unrecognized stdio flag: %s\n", str);
        uint64_t octal = 0;
        int rc = sscanf(str, "%llo", (long long unsigned int *) &octal);
        return (rc == 1 ? octal : 0);
    }
    return 0;
}

uint64_t stdio_or_flags(int n, char **flags)
{
    uint64_t ret = 0;
    int i;
    for(i = 0; i < n; i++)
    {
        ret |= stdio_flag_to_num(flags[i]);
    }
    return ret;
}

uint64_t stdio_fsti(char *str)
{
    /* see flag_conv_common.h */
    SETUP_FLAG_STRING_TO_INT
    uint64_t ret = stdio_or_flags(n, flag_ptrs);
    P(ret);
    return ret;
}


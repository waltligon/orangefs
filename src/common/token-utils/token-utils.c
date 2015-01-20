/*
 * (C) 2014 Clemson University
 *
 * See COPYING in top-level directory.
 */
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "token-utils.h"

#ifdef TOKEN_ENABLE_HEAP_VERSION
#include <stdlib.h>
#endif

#ifdef WIN32
#define strtok_r(s, d, v)    strtok_s(s, d, v)
#endif

/* In-place tokenizer: Nice because it uses no malloc/free/etc...just strncpy.
 * It's up to the user to provide the character arrays this function uses.
 * ===========================================================================*/

/* Iterates over tokens detected in the input string w/ the specified delimiter.
 * Some limit can be applied to the number of tokens processed (and
 * thereby limit the number potential actions run).
 *
 * User must provide "copy" which is either:
 *  preferrably stack allocated memory initialized to {0};
 *  heap allocated memory that has been memset to 0 (don't forget to free!)
 *
 * User must provide input_limit which is one less than the size of the space
 * allocated for "copy". This ensures the null terminating byte cannot be
 * overwritten by the implementation below.
 *
 * It's convenient to specify your max input length as a pre-processor
 *  macro. Then, use the LEN_PLUS_ONE() macro, defined in token-utils.h to set
 *  the size of your "copy" region to one plus your max input length. Then you
 *  simply pass the "copy" ptr, and then the max input length macro as
 *  "input_length".
 *
 * A user can register an action function with this iterator. The action
 *  function must be of type "int (*action)(const char *, void *)".
 *
 * This is so the user can supply to the action function some struct ptr that
 * will vary in type between various action function implementations.
 * 
 * A user can provide copy_out which is a char * array initialized to
 *  potentially multiple (hopefully stack) allocated strings initialized to {0}.
 * In this case, the user must also provide an int [] containing the sizes of
 *  the corresponding strings.
 * 
 * If copy_out and copy_out_token_lengths are specified, the user must also
 *  specify a token limit which corresponds to the element count of these
 *  arrays and the maximum number of tokens that may be copied out.
 *
 * Returns 0 on success, -1, on failure which can be caused by:
 *      bad function input
 *      failure of a registered action.
 *
 * Upon last success or failure, returns the total count of successfull actions.
 */
int iterate_tokens_inplace(const char * input, /* Input string */
                           const char * delim, /* Input delimiter */
                           char * copy, /* space on stack for strtok_r to use */
                           unsigned int input_limit, /* input limit, ex: 4096 */
                           unsigned int token_limit, /* limit strtok_r count */
                           unsigned int *count, /* token/action-SUCCESS count */
                           int (*action)(const char *, void *), /* action func ptr */
                           void *action_arg2,
                           char ** copy_out, /* space to memcpy discovered tokens to user */
                           unsigned int * copy_out_token_lengths) /* token lengths */
{
    char *savep = NULL; /* used for reentrant strtok, strtok_r. */
    char *token = NULL; /* returned by strtok_r */
    unsigned int running_count = 0; /* count of successfully completed actions */
    int ret = 0;

    if(count)
    {
        *count = 0;
    }

    if(!input || !delim || !copy)
    {
        return -1;
    }

    /* array initialization is expected to set all elements to 0, so the
     * following should be safe.
     * Copy the input to a char * strtok_r can consume.
     */
    strncpy(copy, input, input_limit);

    token_limit = token_limit == 0 ? UINT_MAX : token_limit;

    /* Get the first token */
    token = strtok_r(copy, delim, &savep);
    while(token && running_count < token_limit)
    {
        if(copy_out && copy_out_token_lengths)
        {
            strncpy(*copy_out, token, *copy_out_token_lengths);
            copy_out++;
            copy_out_token_lengths++;
        }
        if(action)
        {
            ret = action(token, action_arg2);
            if(ret < 0)
            {
                /* TODO add debug message */
                ret = -1;
                goto done;
            }
        }
        running_count++;
        if(running_count == token_limit)
        {
            break;
        }
        /* Get the next token */
        token = strtok_r(NULL, delim, &savep);
    }
    /* DONE */
done:
    if(count)
    {
        *count = running_count; /* return successfull action count to user */
    }
    return ret;
}

int no_op_inplace(const char *token, void *arg2)
{
    return 0;
}

int dump_token_inplace(const char *token, void *arg2)
{
    printf("token = %s\n", token);
    return 0;
}

/* Provide a string, dumps all detected tokens
 * Returns 0 on success.
 */
int dump_tokens_inplace(const char *input,
                        const char *delim,
                        char * copy, 
                        unsigned int input_limit,
                        unsigned int token_limit,
                        unsigned int *count)
{
    return iterate_tokens_inplace(input,
                                  delim,
                                  copy,
                                  input_limit,
                                  token_limit,
                                  count,
                                  &dump_token_inplace,
                                  NULL,
                                  NULL,
                                  0);
}

/* End of inplace tokenizer code:
 * ===========================================================================*/

#ifdef TOKEN_ENABLE_HEAP_VERSION

/* Iterate over many char * performing some registered action.
 * Returns a negative count equal to:
 *      (how_many_actions_failed + number_of_NULL_char_* detected)
 * Returns zero if everything succeeded.
 */
int iterate_tokens(char **tokens,
                   unsigned int count,
                   int (*action)(char *))
{
    int ret = 0;
    if(tokens && count > 0 && action)
    {
        unsigned int index;
        for(index = 0; index < count; tokens++, index++)
        {
            /* printf("action iteration = %u\n", index); */
            if(*tokens)
            {
                ret = action(*tokens);
                if(ret != 0)
                {
                    /* TODO add debug message. */
                    ret--;
                }
            }
            else
            {
                /* TODO add debug message. */
                ret--;
            }
        }
    }
    return ret;
}

int free_token(char *token)
{
        free((void *) token);
        return 0;
}

/* Uses iterate_tokens to iterate over tokens, freeing each non-zero ptr, then
 * also frees "tokens" double ptr. */
int free_tokens(char **tokens, unsigned int count)
{
    int ret = -1;
    //printf("iteration operation = %s\n", __PRETTY_FUNCTION__);
    if(tokens)
    {
        ret = iterate_tokens(tokens, count, &free_token);
        free(tokens);
    }
    return ret;
}

int dump_token(char *token)
{
    printf("token = %s\n", token);
    return 0;
}

int dump_tokens(char **tokens, unsigned int count)
{
    return iterate_tokens(tokens, count, &dump_token);
}

/* Return the count of tokens, split on "delim" of the string "input".
 * Uses the re-entrant version of strtok: strtok_r.
 * 
 * Establish max input string length and use local char [] w/ memcpy
 * instead of strdup.
 * 
 */
int get_token_count(const char * input,
                    const char * delim,
                    char * copy,
                    unsigned int input_limit,
                    unsigned int token_limit,
                    unsigned int *count)
{
    char *savep = NULL;
    char * token = NULL;
    unsigned int tokens_counted = 0;

    if(!input || !delim || !copy || !count)
    {
        return -1;
    }

    *count = 0;

    printf("initialized duped_str=%s\n", copy);

    /* array initialization is expected to set all elements to 0, so the
     * following should be safe.
     */
    strncpy(copy, input, input_limit);

    printf("after memcpy duped_str=%s\n", copy);

    /* Limit of zero implies limit of UINT_MAX, otherwise limit. */
    token_limit = token_limit == 0 ? UINT_MAX : token_limit;
    //printf("ultimate limit = %u\n", limit);

    /* Get the first token */
    token = strtok_r(copy, delim, &savep);
    while(token && tokens_counted < token_limit)
    {
        tokens_counted++;
        //printf("token=%s\n", token);
        /* Get the next token */
        token = strtok_r(NULL, delim, &savep);
    }
    /* DONE */
    *count = tokens_counted;
    return 0;
}

/* This version uses the re-entrant version of strtok: strtok_r.
 * limit = 0 implies very high limit of UINT_MAX.
 * 
 * Users that call this function MUST run the free_tokens function using the 
 * returned char** iff the char ** is NOT NULL!  
 */
char ** get_tokens(const char *input,
                   const char * delim,
                   char *copy,
                   unsigned int input_limit,
                   unsigned int token_limit,
                   unsigned int *count)
{
    char *savep = NULL;
    char * token = NULL;
    char ** ret  = NULL;
    char ** ret_tmp = NULL;
    int int_ret = 0; // return code for generic_tokenizer_get_count
    unsigned int token_count = 0; //actual token count determined
    unsigned int tokens_saved = 0;

    if(!input || !delim || !count || !copy)
    {
        return NULL;
    }

    strncpy(copy, input, input_limit);

    int_ret = get_token_count(input,
                              delim,
                              copy,
                              input_limit,
                              token_limit,
                              &token_count);

    if(int_ret < 0)
    {
        return NULL;
    }

    /* need to copy the input yet again, since the above function indirectly
     * modifies it. */
    strncpy(copy, input, input_limit);

    token_limit = token_limit == 0 ? UINT_MAX : token_limit;

    /* Actual count must be less than or equal to the imposed limit. */
    *count = token_count > token_limit ? token_limit : token_count;

    ret = (char **) calloc(*count, sizeof(char *));

    if(!ret)
    {
        return NULL;
    }

    ret_tmp = ret;

    /* Get the first token */
    token = strtok_r(copy, delim, &savep);
    while(token && tokens_saved < token_limit)
    {
        tokens_saved++;
        //printf("token=%s\n", token);
        /* Copy the token */
        *ret_tmp = strdup(token);
        if(!*ret_tmp)
        {
            free_tokens(ret, tokens_saved - 1);
            return NULL;
        }
        ret_tmp++;
        /* Get the next token */
        token = strtok_r(NULL, delim, &savep);
    }
    /* DONE */
    return ret;
}
#endif /* TOKEN_ENABLE_HEAP_VERSION */


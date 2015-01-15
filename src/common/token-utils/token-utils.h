#ifndef TOKEN_UTILS_H
#define TOKEN_UTILS_H

#define TOKEN_UTILS_DEFAULT_MAX_INPUT_STRLEN 4095
#define PLUS_ONE(IN) (IN + 1)

#if 0
#define TOKEN_ENABLE_HEAP_VERSION
#endif

int iterate_tokens_inplace(const char * input,
                           const char * delim,
                           char * copy,
                           unsigned int input_limit,
                           unsigned int token_limit,
                           unsigned int * count,
                           int (*action)(const char *, void *),
                           void * action_arg2,
                           char ** copy_out,
                           unsigned int * copy_out_token_lengths);

/* Useful for debug, profiling, etc. Programmer should implement their 
 * own custom action functions inside their own modules and pass a pointer to
 * their custom action function as a parameter to iterate_tokens_inplace. */
int no_op_inplace(const char *token, void * arg2);
int dump_token_inplace(const char *token, void * arg2);
int dump_tokens_inplace(const char *input,
                        const char *delim,
                        char * copy,
                        unsigned int input_limit,
                        unsigned int token_limit,
                        unsigned int *count);

/* END OF inplace functions */

#ifdef TOKEN_ENABLE_HEAP_VERSION
int iterate_tokens(char **tokens,
                   unsigned int count,
                   int (*action)(char *));

int free_token(char *token);

int free_tokens(char **tokens, unsigned int count);

int dump_token(char *token);

int dump_tokens(char **tokens, unsigned int count);

int get_token_count(const char * input,
                    const char * delim,
                    char * copy,
                    unsigned int input_limit,
                    unsigned int token_limit,
                    unsigned int *count);

char ** get_tokens(const char *input,
                   const char * delim,
                   char *copy,
                   unsigned int input_limit,
                   unsigned int token_limit,
                   unsigned int *count);
#endif /* TOKEN_ENABLE_HEAP_VERSION */

#endif /* TOKEN_UTILS_H */


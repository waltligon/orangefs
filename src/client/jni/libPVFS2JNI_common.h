#ifndef LIBPVFS2JNI_COMMON_H
#define LIBPVFS2JNI_COMMON_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef _FORTIFY_SOURCE
# undef _FORTIFY_SOURCE
# define _FORTIFY_SOURCE 0
#endif

#include <errno.h>
#include <stdio.h>

#define JNI_INITIAL_ARRAY_LIST_SIZE 1024
#define NULL_JOBJECT ((jobject) NULL)

//#define JNI_DEBUG

/* Helpful Debugging Macros for JNI Layer */
#ifdef JNI_DEBUG
#undef JNI_DEBUG
#define JNI_DEBUG 1 /* Enable debugging information for JNI Layer. */

#define JNI_PRINT(...)                                                         \
    do                                                                         \
    {                                                                          \
        fprintf(stdout, __VA_ARGS__);                                          \
        fflush(stdout);                                                        \
    } while(0)

#define JNI_ERROR(...)                                                         \
    do                                                                         \
    {                                                                          \
        fprintf(stderr, __VA_ARGS__);                                          \
        fflush(stderr);                                                        \
    } while(0)

#define JNI_PFI()                                                              \
    do                                                                         \
    {                                                                          \
        printf("function called: {%s}\n", __PRETTY_FUNCTION__);                \
        fflush(stdout);                                                        \
    } while(0)

#define JNI_PERROR()                                                           \
    do                                                                         \
    {                                                                          \
        if(errno != 0)                                                         \
        {                                                                      \
            fprintf(stderr, "errno= %d\t"                                      \
                    "Error detected on line %d in function %s\n\t",            \
                    errno, __LINE__, __PRETTY_FUNCTION__);                     \
            perror("");                                                        \
            fflush(stderr);                                                    \
        }                                                                      \
    } while(0)

#else /* No debugging info. */

#define JNI_DEBUG 0
#define JNI_PRINT(...) do {} while(0)
#define JNI_ERROR(...) do {} while(0)
#define JNI_PFI() do {} while(0)
#define JNI_PERROR() do {} while(0)
#endif

#endif /* #ifndef LIBPVFS2JNI_COMMON_H */

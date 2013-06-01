#ifndef LIBPVFS2JNI_COMMON_H
#define LIBPVFS2JNI_COMMON_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <stdio.h>

/* Uncomment the following line to enable debugging information for the C side 
 * of the OrangeFS JNI Layer 
 */
#define JNI_DEBUG

/* Helpful Debugging Macros for JNI Layer */
#if defined(JNI_DEBUG) /* Enable debugging information for JNI Layer. */
#undef JNI_DEBUG
#define JNI_DEBUG 1

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
            fprintf(stderr, "%s%s\n\t", "Error detected in function: ",        \
                    __PRETTY_FUNCTION__);                                      \
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

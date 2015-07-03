/*
 * (C) 2014 Clemson University
 *
 * See COPYING in top-level directory.
 */

#ifndef RECURSIVE_REMOVE_H
#define RECURSIVE_REMOVE_H

#include <dirent.h>
#include <stdio.h>
#include <errno.h>

/* ------------------ Uncomment to turn on debug info. ----------------------*/
//#define ENABLE_RR_PFI
//#define ENABLE_RR_PRINT
#define ENABLE_RR_ERROR
#define ENABLE_RR_PERROR
/* --------------------------------------------------------------------------*/

#ifdef ENABLE_RR_PFI
 #define RR_PFI()                                                              \
     do                                                                        \
     {                                                                         \
         printf("function called: {%s}\n", __PRETTY_FUNCTION__);               \
     } while(0)
#else
 #define RR_PFI() do {} while(0)
#endif

#ifdef ENABLE_RR_PRINT
 #define RR_PRINT(...)                                                         \
     do                                                                        \
     {                                                                         \
         fprintf(stdout, __VA_ARGS__);                                         \
     } while(0)
#else
 #define RR_PRINT(...) do {} while(0)
#endif

#ifdef ENABLE_RR_ERROR
#define RR_ERROR(...)                                                          \
     do                                                                        \
     {                                                                         \
         fprintf(stderr, __VA_ARGS__);                                         \
     } while(0)
#else
 #define RR_ERROR(...) do {} while(0)
#endif

#ifdef ENABLE_RR_PERROR
#define RR_PERROR(message)                                                     \
    do                                                                         \
    {                                                                          \
        if(errno != 0)                                                         \
        {                                                                      \
            fprintf(stderr, "errno= %d\t"                                      \
                    "Error detected on line %d in function %s\n\t",            \
                    errno, __LINE__, __PRETTY_FUNCTION__);                     \
            perror(message);                                                   \
        }                                                                      \
    } while(0)
#else
 #define RR_PERROR(message) do {} while(0)
#endif

int recursive_delete_dir(char *dir);
int remove_files_in_dir(char *dir, DIR* dirp);

#endif


/* 
 * (C) 2011 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup usrint
 *
 *  PVFS2 user interface routines - implementation of stdio for pvfs
 */

#ifndef STDIO_OPS_H
#define STDIO_OPS_H

struct stdio_ops_s
{
    FILE *(*fopen)(const char *path, const char *mode);
    FILE *(*fdopen)(int fd, const char *mode);
    FILE *(*freopen)(const char *path, const char *mode, FILE *stream);
    size_t (*fwrite)(const void *ptr, size_t size, size_t nmemb, FILE *stream);
    size_t (*fread)(void *ptr, size_t size, size_t nmemb, FILE *stream);
    int (*fclose)(FILE *stream);
    int (*fseek)(FILE *stream, long offset, int whence);
    int (*fseek64)(FILE *stream, const off64_t offset, int whence);
    int (*fsetpos)(FILE *stream, const fpos_t *pos);
    void (*rewind)(FILE *stream);
    long int (*ftell)(FILE *stream);
    off64_t (*ftell64)(FILE *stream);
    int (*fgetpos)(FILE *stream, fpos_t *pos);
    int (*fflush)(FILE *stream);
    int (*fputc)(int c, FILE *stream);
    int (*fputs)(const char *s, FILE *stream);
    int (*putc)(int c, FILE *stream);
    int (*putchar)(int c);
    int (*puts)(const char *s);
    char *(*fgets)(char *s, int size, FILE *stream);
    int (*fgetc)(FILE *stream);
    int (*getc)(FILE *stream);
    int (*getchar)(void);
    char *(*gets)(char * s);
    int (*ungetc)(int c, FILE *stream);
    int (*vfprintf)(FILE *stream, const char *format, va_list ap);
    int (*vprintf)(const char *format, va_list ap);
    int (*fprintf)(FILE *stream, const char *format, ...);
    int (*printf)(const char *format, ...);
    int (*fscanf)(FILE *stream, const char *format, ...);
    int (*scanf)(const char *format, ...);
    void (*clearerr)(FILE *stream);
    int (*feof)(FILE *stream);
    int (*ferror)(FILE *stream);
    int (*fileno)(FILE *stream);
    int (*remove)(const char *path);
    void (*setbuf)(FILE *stream, char *buf);
    void (*setbuffer)(FILE *stream, char *buf, size_t size);
    void (*setlinebuf)(FILE *stream);
    int (*setvbuf)(FILE *stream, char *buf, int mode, size_t size);
    char *(*mkdtemp)(char *template);
    int (*mkstemp)(char *template);
    FILE *(*tmpfile)(void);
    DIR *(*opendir)(const char *name);
    DIR *(*fdopendir)(int fd);
    int (*dirfd)(DIR *dir);
    struct dirent *(*readdir)(DIR *dir);
    struct dirent64 *(*readdir64)(DIR *dir);
    void (*rewinddir)(DIR *dir);
    void (*seekdir)(DIR *dir, off_t offset);
    off_t (*telldir)(DIR *dir);
    int (*closedir)(DIR *dir);
    int (*scandir)(const char *dir,
                    struct dirent ***namelist,
                    int(*filter)(const struct dirent *),
                    int(*compar)(const void *, const void *));
    int (*scandir64 )(const char *dir,
                      struct dirent64 ***namelist,
                      int(*filter)(const struct dirent64 *),
                      int(*compar)(const void *, const void *));
};

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

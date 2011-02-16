#include <usrint.h>
#include <dirent.h>

/* STDIO implementation - this gives users something to link to
 * that will call back to the PVFS lib - also lets us optimize
 * in a few spots like buffer sizes and stuff
 */


/*
 *fopen wrapper 
 */ 

FILE *fopen(const char * path, const char * mode)
{ 
    int flags = 0, fd = 0;
    mode_t perm = 0;
    pvfs_descriptor *pd = NULL;
    const char *p = NULL;
    int append = false, read = false, write = false, update = false;
    int exclusive = false;

    if(!pvfs_lib_init)
    { 
        load_glibc(); 
    } 

    /* look for fopen modes */ 
    for(p = mode; *p; p++)
    { 
        switch(*p) { 
            case 'a':
                append = true; 
                if (read || write)
                {
                    errno = EINVAL;
                    return NULL;
                }
                break; 
            case 'r':
                read = true; 
                if (append || write)
                {
                    errno = EINVAL;
                    return NULL;
                }
                break; 
            case 'w':
                write = true; 
                if (read || append)
                {
                    errno = EINVAL;
                    return NULL;
                }
                break; 
            case '+':
                update = true; 
                if (!(read || write || append))
                {
                    errno = EINVAL;
                    return NULL;
                }
                break;
            case 'b': /* this is ignored in POSIX */
            case 'c': /* used in glibc ignored here */
            case 'm': /* used in glibc ignored here */
                break;
            case 'x': /* glibc extension */
                exclusive = true;
                if (!(read || write || append))
                {
                    errno = EINVAL;
                    return NULL;
                }
                break;
            default:
                errno = EINVAL;
                return NULL;
                break; 
        }
    }
    /* this catches an empty mode */
    if (!(read || write || append))
    {
        errno = EINVAL;
        return NULL;
    }
    if (read)
    { 
        flags = O_RDONLY; 
    }
    else if(read && update)
    { 
        flags = O_RDWR; 
    }
    else if(write)
    { 
        flags = O_WRONLY | O_CREAT | O_TRUNC; 
    } 
    else if(write && update)
    { 
        flags = O_RDWR | O_CREAT | O_TRUNC; 
    }
    else if(append)
    { 
        flags = O_WRONLY | O_APPEND | O_CREAT; 
    } 
    else if (append && update)
    { 
        flags = O_RDWR | O_APPEND | O_CREAT; 
    }
    if (exclusive) /* check this regardless of the above */
    {
        flags |= O_EXCL;
    }
     
    /* this is our version of open */
    fd = open(path, flags, 0666); 

    if(fd)
    { 
        pd = pvfs_find_descriptor(fd);
        /* set up buffering here */
        pd->bufsize = pd->fsops->buffsize();
        pd->buf = malloc(pd->bufsize);
        pd->buftotal = 0;
        pd->buf_off = 0;
        pd->bufptr = 0;
        return PFILE2FILE(pd); 
    }
    else
    { 
        pvfs_debug("pvfs call to open failed\n"); 
        return NULL; 
    }
}

FILE *fdopen(int fd, const char *mode)
{
    pvfs_descriptor *pd;
    pd = pvfs_find_descriptor(fd);
    /* check for valid mode here */
    /* set up buffering here */
    return PFILE2FILE(pd); 
}

FILE *freopen(const char *path, const char *mode, FILE *stream)
{
    pvfs_descriptor *pd = FILE2PFILE(stream)
    /* see if stream is in use - if so close the file */
    /* open the file and put its info in the given descriptor */
    return PFILE2FILE(pd);
}

/*
 * fwrite wrapper
 */
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    off64_t rsz, rstart, rend;
    off64_t firstblk, firstoff, firstsize;
    off64_t lastblk, lastoff, lastsize;
    off64_t curblk, numblk;

    if (!stream || !stream->is_in_use || !ptr) {
        errno = EBADF;
        return 0;
    }
    /* rsz: request size in bytes */
    rsz = size * nmemb;
    /* rstart: first byte of request */
    rstart = stream->file_pointer;
    /* rend: last byte of request */
    rend = rstart + rsz - 1;

    /* file is divided into blocks from 0..N-1 */
    /* curblk: block number of current buffer */
    if (stream->buf_off == -1)
    {
        curblk = -1;
        stream->dirty = 0;
    }
    else
    {
        curblk = stream->buff_off/stream->bufsize;
    }
    /* firstblk: block number with first byte of request */
    firstblk = rstart/stream->bufsize;
    /* firstoff: offset from start of first block */
    /* firstoff is in 0..blocksize */
    firstoff = rstart%stream->bufsize;
    /* lastblk: block number with last byte of request */
    lastblk = rend/stream->bufsize;
    /* lastoff: offset from start of last block */
    /* lastoff is in 0..blocksize */
    lastoff = rend%stream->bufsize;
    /* numblk: number of blocks involved in request */
    numblk = (lastblk - firstblk) + 1;
    /* firstsize: number of bytes on first block of request */
    /* firstsize is in 1..blocksize */
    firstsize = (firstblk==lastblk) ? lastoff-firstoff+1 :
    stream->bufsize-firstoff;
    /* lastsize: number of bytes on last block of request */
    /* lastsize is in 0..blocksize */
    lastsize = (firstblk == lastblk) ? 0 : lastoff+1;
    

    /* if the current buffer is not involved, flush it */
    if (curblk < firstblk || curblk > lastblk)
    {
        if (stream->dirty)
            write(stream->fd, stream->buffer, stream->bufsize);
        stream->dirty = 0;
        stream->buf_off = -1;
        curblk = -1;
        /* write completes below - file pointer already in position */
    }
    /* if current buffer is completely overwritten, ignore it */
    else if ((curblk > firstblk && curblk < lastblk) ||
            (curblk == firstblk && firstsize == stream->bufsize) ||
            (curblk == lastblk && lastsize = stream->bufsize))
    {
        stream->dirty = 0;
        stream->buf_off = -1;
        curblk = -1;
        /* write completes below - file pointer already in position */
    }
    /* if current buffer is first, copy bytes */
    else if (curblk == firstblk && firstsize < stream->bufsize)
    {
        memcopy(stream->buf+firstoff, ptr, firstsize);
        if (numblk > 1)
        {
            lseek(stream->fd, stream->buf_off, SEEK_SET);
            write(stream->fd, stream->buf, stream->bufsize);
            stream->dirty = 0;
            stream->buf_off = -1;
        }
        ptr += firstsize;
        rsz -= firstsize;
        /* write completes below - file pointer left in position */
    }
    /* now write the data */
    if (lastsize > 0) /* write to the middle of a block */
    {
        /* should already be in position to write request data */
        write(stream->fd, ptr, rsz-lastsize);
        /* see if the last block is not the one already loaded */
        if (curblk != lastblk)
        {
            /* cannot have a valid block in the buffer */
            if (curblk != -1)
            {
                errno = 100;
                return -1;
            }
            /* locate new buffer block */
            stream->buf_off = lastblock * stream->bufsize;
            /* load a new buffer - file pointer should be in position */
            stream->buftotal = read(stream->fd, stream->buf, stream->bufsize);
        }
        /* move last of request data to buffer */
        memcopy(stream->buf, ptr+(rsz-lastsize), lastsize);
        /* move file pointer back to end of request */
        stream->file_pointer = stream->buf_off+lastsize;
        /* see of we extended the file */
        if (lastsize > stream->buftotal)
            stream->buftotal = lastsize;
    }
    else /* writes to the end of a block */
    {
        /* should already be in position */
        write(stream->fd, ptr, rsz);
    }
    /* file pointer should be in right place */
    return rsz;
}

/*
 * fread wrapper
 */
size_t fread(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    off64_t rsz, rstart, rend;
    off64_t firstblk, firstoff, firstsize;
    off64_t lastblk, lastoff, lastsize;
    off64_t curblk, numblk;

    if (!stream || !stream->is_in_use || !ptr) {
        errno = EBADF;
        return 0;
    }
    /* rsz: request size in bytes */
    rsz = size * nmemb;
    /* rstart: first byte of request */
    rstart = stream->file_pointer;
    /* rend: last byte of request */
    rend = rstart + rsz - 1;

    /* file is divided into blocks from 0..N-1 */
    /* curblk: block number of current buffer */
    if (stream->buf_off == -1)
    {
        curblk = -1;
        stream->dirty = 0;
    }
    else
    {
        curblk = stream->buff_off/stream->bufsize;
    }
    /* firstblk: block number with first byte of request */
    firstblk = rstart/stream->bufsize;
    /* firstoff: offset from start of first block */
    /* firstoff is in 0..blocksize */
    firstoff = rstart%stream->bufsize;
    /* lastblk: block number with last byte of request */
    lastblk = rend/stream->bufsize;
    /* lastoff: offset from start of last block */
    /* lastoff is in 0..blocksize */
    lastoff = rend%stream->bufsize;
    /* numblk: number of blocks involved in request */
    numblk = (lastblk - firstblk) + 1;
    /* firstsize: number of bytes on first block of request */
    /* firstsize is in 1..blocksize */
    firstsize = (firstblk==lastblk) ? lastoff-firstoff+1 :
    stream->bufsize-firstoff;
    /* lastsize: number of bytes on last block of request */
    /* lastsize is in 0..blocksize */
    lastsize = (firstblk == lastblk) ? 0 : lastoff+1;
    

    /* if the current buffer is not involved, flush it */
    if (curblk < firstblk || curblk > lastblk)
    {
        if (stream->dirty)
            write(stream->fd, stream->buffer, stream->bufsize);
        stream->dirty = 0;
        stream->buf_off = -1;
        curblk = -1;
        /* write completes below - file pointer already in position */
    }
    /* if current buffer is completely read, break into two reads */
    else if ((curblk > firstblk && curblk < lastblk) ||
            (curblk == firstblk && firstsize == stream->bufsize) ||
            (curblk == lastblk && lastsize = stream->bufsize))
    {
        /* read from start up to buffer */
        /* copy buffer */
        /* if buffer dirty, flush */
        if (stream->dirty)
        {
            lseek(stream->fd, stream->buf_off, SEEK_SET);
            write(stream->fd, stream->buf, stream->bufsize);
            stream->dirty = 0;
            stream->buf_off = -1;
            curblk = -1;
        }
        /* read completes below - file pointer already in position */
    }
    /* if current buffer is first, copy bytes */
    else if (curblk == firstblk && firstsize < stream->bufsize)
    {
        memcopy(ptr, stream->buf+firstoff, firstsize);
        /* if buffer dirty, flush */
        if (stream->dirty)
        {
            lseek(stream->fd, stream->buf_off, SEEK_SET);
            write(stream->fd, stream->buf, stream->bufsize);
            stream->dirty = 0;
            stream->buf_off = -1;
        }
        ptr += firstsize;
        rsz -= firstsize;
        /* read completes below - file pointer left in position */
    }
    /* now read the data */
    if (lastsize > 0) /* read to the middle of a block */
    {
        /* should already be in position to read request data */
        read(stream->fd, ptr, rsz-lastsize);
        /* see if the last block is not the one already loaded */
        if (curblk != lastblk)
        {
            /* cannot have a valid block in the buffer */
            if (curblk != -1)
            {
                errno = 100;
                return -1;
            }
            /* locate new buffer block */
            stream->buf_off = lastblock * stream->bufsize;
            /* load a new buffer - file pointer should be in position */
            stream->buftotal = read(stream->fd, stream->buf, stream->bufsize);
        }
        /* move last of request data to buffer */
        memcopy(ptr+(rsz-lastsize), stream->buf, lastsize);
        /* move file pointer back to end of request */
        stream->file_pointer = stream->buf_off+lastsize;
    }
    else /* writes to the end of a block */
    {
        /* should already be in position */
        read(stream->fd, ptr, rsz);
    }
    /* file pointer should be in right place */
    return rsz;
}

/*
 * fclose wrapper
 */
int fclose(FILE *stream)
{
    return close(stream->fd);
}

/*
 * fseek wrapper
 */
off_t fseek(FILE *stream, off_t offset, int whence)
{
    return (off_t)lseek64(stream->fd, (off64_t)offset, whence);
}

/*
 * fseek64 wrapper
 */
off64_t fseek64(FILE *stream, off64_t offset, int whence)
{
    return lseek64(stream->fd, offset, whence);
}

/*
 * fsetpos wrapper
 */
int fsetpos(FILE *stream, fpos_t *pos)
{
    return lseek(stream->fd, *pos, SEEK_SET);
}

/*
 * rewind wrapper
 */
void rewind(FILE *stream)
{
    lseek(stream->fd, 0, SEEK_SET);
}

/*
 * ftell wrapper
 */
long ftell(FILE *stream)
{
    return (long)stream->file_pointer;
}

/*
 * fgetpos wrapper
 */
int fgetpos(FILE *stream, fpos_t *pos)
{
    *pos = stream->file_pointer;
    return 0;
}

/*
 * fflush wrapper
 */
int fflush(FILE *stream)
{
    int rc = 0;
    if (stream)
    {
        if (stream->buf && stream->dirty)
        {
            lseek64(stream->fd, stream->buf_off, SEEK_SET);
            /* buftotal should equal bufsize unless the buffer */
            /* is at the end of the file and the buffer is too */
            /* big - then buftotal is smaleer */
            write(stream->fd, stream->buf, stream->buftotal);
            stream->dirty = 0;
        }
    }
    else
    {
        errno = EINVAL;
        rc = -1;
    }
    return rc;
}

/*
 * fputc wrapper
 */
int fputc(int c, FILE *stream)
{
    size_t len;
    int rc;
    if (!stream || !stream->is_in_use || !s)
    {
        errno = EBADF;
        return 0;
    }
    rc = fwrite (&c, 1, 1, stream);
    if (rc != 1)
        rc = -1;
    else
        rc = c;
    return rc;
}

/*
 * fputs wrapper
 */
int fputs(const char *s, FILE *stream)
{
    size_t len;
    int rc;
    if (!stream || !stream->is_in_use || !s)
    {
        errno = EBADF;
        return 0;
    }
    len = strlen(s);
    return fwrite (s, len, 1, stream);
}

int putc(int c, FILE *stream)
{
    return fputc(c, stream);
}

int putchar(int c)
{
    return fputc(c, stdout);
}

int puts(const char *s, FILE *stream)
{
    fputs(s, stream);
    return fputs("\n", stream);
}

/**
 *
 * fgets wrapper
 */
char *fgets(char *s, int size, FILE *stream)
{
    char *p;
    long pos, current;
    int rc;

    if (!pvfs_lib_init) {
        load_glibc();
    }
    if (!stream || stream->_fileno < 0) {
        errno = EBADF;
        return 0;
    }
    if (pvfs_find_descriptor(stream->_fileno)) {
        pos = ftell(stream);
#ifdef DEBUG
        pvfs_debug("fgets: pos=%ld\n", pos);
#endif
        rc = pvfs_read(stream->_fileno, s, size);
        if (rc > 0) {
            p = strchr(s, '\n');
            if (p) {
                ++p;
                *p = '\0';
                pos += strlen(s);
                current = ftell(stream);
#ifdef DEBUG
                pvfs_debug("fgets: s=%s (len=%d)", s, strlen(s));
#endif
                if (pos != current) {
#ifdef DEBUG
                    pvfs_debug("fgets: new pos=%ld, current=%ld\n", pos, current);
#endif
                    pvfs_lseek(stream->_fileno, pos, SEEK_SET);
#ifdef DEBUG
                    pvfs_debug("fgets: set pos=%ld\n", ftell(stream));
#endif
                }
                return s;
            }
        }
        return NULL;
    }
    else {
#ifdef DEBUG
        pvfs_debug("fgets: calling glibc_ops.fgets, s=%p, size=%ld, fd=%d\n",
                    s, size, fileno(stream));
#endif
        return glibc_ops.fgets(s, size, stream);
    }
}

/**
 *
 * fgetc wrapper
 */
int fgetc(FILE *stream)
{
    int rc, ch;

    if (!pvfs_lib_init) {
        load_glibc();
    }
    if (!stream || stream->_fileno < 0) {
        errno = EBADF;
        return 0;
    }
    if (pvfs_find_descriptor(stream->_fileno)) {
#ifdef DEBUG
        pvfs_debug("fgetc: calling pvfs_read\n");
#endif
        rc = pvfs_read(stream->_fileno, &ch, 1);
        if (rc > 0) {
            return ch;
        }
        else {
            errno = EBADF;
            return EOF;
        }
    }
    else {
#ifdef DEBUG
        pvfs_debug("fgetc: calling glibc_ops.fgetc\n");
#endif
        return glibc_ops.fgetc(stream);
    }
}

/**
 *
 * getc wrapper
 */
int getc(FILE *stream)
{
    return fgetc(stream);
}

int getchar(void)
{
    return fgetc(stream);
}

char *gets(char * s)
{
}

/**
 *
 * ungetc wrapper
 */
int ungetc(int c, FILE *stream)
{
    long pos;

    if (!pvfs_lib_init) {
        load_glibc();
    }
    if (!stream || stream->_fileno < 0) {
        errno = EBADF;
        return 0;
    }
    if (pvfs_find_descriptor(stream->_fileno)) {
#ifdef DEBUG
        pvfs_debug("ungetc: current pos=%ld\n", ftell(stream));
#endif
        pos = ftell(stream) - 1;
        pvfs_lseek(stream->_fileno, pos, SEEK_SET);
#ifdef DEBUG
        pvfs_debug("ungetc: set pos=%ld\n", ftell(stream));
#endif
        return c;
    }
    else {
#ifdef DEBUG
        pvfs_debug("ungetc: calling glibc_ops.ungetc\n");
#endif
        return glibc_ops.ungetc(c, stream);
    }
}

/**
 *
 * fprintf wrapper
 */
int fprintf(FILE *stream, const char *format, ...)
{
    char *buf = NULL;
    size_t len;
    va_list ap;
    int rc;

    if (!pvfs_lib_init) {
        load_glibc();
    }
    if (!stream || stream->_fileno < 0) {
        errno = EBADF;
        return 0;
    }
    if (pvfs_find_descriptor(stream->_fileno)) {
        va_start(ap, format);
        len = vasprintf(&buf, format, ap);
        va_end(ap);
        if (len > 0 && buf) {
            rc = pvfs_write(stream->_fileno, buf, len);
        }
        else {
#ifdef DEBUG
            pvfs_debug("fprintf: vasprintf error\n", buf);
#endif
            errno = EIO;
            rc = -1;
        }
        if (buf) {
            free(buf);
        }
        return rc;
    }
    else {
#ifdef DEBUG
        pvfs_debug("fprintf: calling glibc_ops.fprintf for %d\n", stream->_fileno );
#endif
        return glibc_ops.fprintf(stream, format, ap);
    }
}

#if 0
printf()
{
}

fscanf()
{
}

scanf()
{
}
#endif

/*
 * Stdio utilities for EOF, error, etc
 */
void clearerr (FILE *pf)
{
    if (pf && pf->is_in_use)
    {
        pf->eof = 0;
        pf->error = 0;
    }
}

int feof (FILE *pf)
{
    if (pf && pf->is_in_use)
    {
        if (pf->eof)
            return -1;
        return 0;
    }
    return 0;
}

int ferror (FILE *pf)
{
    if (pf && pf->is_in_use)
    {
        if (pf->error)
            return -1;
        return 0;
    }
    return 0;
}

int fileno (FILE *pf)
{
    if (pf && pf->is_in_use)
        return pf->fd;
    errno = EBADF;
    return -1;
}

int remove (const char *path)
{
    int rc;
    struct stat buf;

    rc = stat(path, &buf);
    if (S_ISDIR(buf.st_mode))
        return rmdir (path);
    return unlink (path);
}

void setbuf (FILE *pf, char *buf)
{
    setvbuf(pd, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

void setbuffer (FILE *pf, char *buf, size_t size)
{
    setvbuf(pd, buf, buf ? _IOFBF : _IONBF, size);
}

void setlinbuf (FILE *pf)
{
    setvbuf(pd, (chat *)NULL, buf ? _IOLBF : 0);
}

int setvbuf (FILE *pf, char *buf, int mode, size_t size)
{
}

FILE *tmpfile(void)
{
}

DIR *opendir (const char *name)
{
}

int dirfd (DIR *dir)
{
}

int readdir (unsigned in fd, sruct dirent *dirp, unsigned in count)
{
}

void rewinddir (DIR *dir)
{
}

int scandir (const char *dir, struct dirent ***namelist,
             int(*filter)(const struct dirent *),
             int(*compar)(const struct dirent **,cost struct dirent **))
{
}

void seekdir (DIR *dir, off_t offset)
{
}

off_t telldir (DIR *dir)
{
}

int closedir (DIR *dir)
{
}


/* tmpname ??? */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

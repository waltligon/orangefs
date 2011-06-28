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
#include <usrint.h>
#include <dirent.h>

#define ISFLAGSET(s,f) ((stream->_flags & (f)) == (f))

/* STDIO implementation - this gives users something to link to
 * that will call back to the PVFS lib - also lets us optimize
 * in a few spots like buffer sizes and stuff
 */

/** struct representing a directory stream for buffered dir io
 *
 * this struct type is undefined in /usr/include as it is opaque
 * it is defined in this file only.  This design is based loosely
 * on the buffered IO scheme used in Linux for files.
 */
struct __dirstream {
    int flags;      /**< general flags field */
    int fileno;     /**< file dscriptor of open dir */
    char *buf_base; /**< pointer to beginning of buffer */
    char *buf_end;  /**< pointer to end of buffer */
    char *buf_act;  /**< pointer to end of active portion of buffer */
    char *buf_ptr;  /**< pointer to current position in buffer */
};

#define DIRSTREAM_MAGIC 0xfd100000
#define DIRBUFSIZE (512*1024)
#define ASIZE 256
#define MAXTRIES 16 /* arbitrary - how many tries to get a unique file name */

/** This function coverts from stream style mode to ssycall style flags
 *
 */
static int mode2flags(const char *mode)
{
    int i;
    int flags;
    int append = false, read = false, write = false, update = false;
    int exclusive = false;

    /* look for fopen modes */ 
    for(i = 0; mode[i]; i++)
    { 
        switch(mode[i]) { 
            case 'a':
                append = true; 
                if (read || write)
                {
                    errno = EINVAL;
                    return -1;
                }
                break; 
            case 'r':
                read = true; 
                if (append || write)
                {
                    errno = EINVAL;
                    return -1;
                }
                break; 
            case 'w':
                write = true; 
                if (read || append)
                {
                    errno = EINVAL;
                    return -1;
                }
                break; 
            case '+':
                update = true; 
                if (!(read || write || append))
                {
                    errno = EINVAL;
                    return -1;
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
                    return -1;
                }
                break;
            default:
                errno = EINVAL;
                return -1;
                break; 
        }
    }
    /* this catches an empty mode */
    if (!(read || write || append))
    {
        errno = EINVAL;
        return -1;
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
    return flags;
}
 
/**
 * fopen opens a file, then adds a stream to it
 */ 
FILE *fopen(const char * path, const char * mode)
{ 
    int fd = 0;
    int flags = 0;
    FILE *newfile = NULL;

    flags = mode2flags(mode);
    if (flags == -1)
    {
        return NULL;
    }

    fd = open(path, flags, 0666);
    if (fd == -1)
    {
        return NULL;
    }

    newfile = fdopen(fd, mode);

    return newfile;
}

/** this function sets up a new stream's buffer area
 *
 */
static int init_stream (FILE *stream, int flags, int bufsize)
{
    /* set up stream here */
    stream->_flags = _IO_MAGIC;
    if (!(flags & O_WRONLY))
        stream->_flags |= _IO_NO_READS;
    if (!(flags & O_RDONLY))
        stream->_flags |= _IO_NO_WRITES;
    /* set up default buffering here */
    stream->_IO_buf_base   = (char *)malloc(bufsize);
    if (!stream->_IO_buf_base)
    {
        return -1;
    }
    stream->_IO_buf_end    = stream->_IO_buf_base + bufsize;
    stream->_IO_read_base  = stream->_IO_buf_base;
    stream->_IO_read_ptr   = stream->_IO_buf_base;
    stream->_IO_read_end   = stream->_IO_buf_base;
    stream->_IO_write_base = stream->_IO_buf_base;
    stream->_IO_write_ptr  = stream->_IO_buf_base;
    stream->_IO_write_end  = stream->_IO_buf_end;
    return 0;
}

/**
 * fdopen adds a stream to an existing open file
 */
FILE *fdopen(int fd, const char *mode)
{
    int rc = 0;
    FILE *newfile = NULL;
    int flags;

    /* need to check for valid mode here */
    /* it must be compatible with the existing mode */
    flags = mode2flags(mode);

    newfile = (FILE *)malloc(sizeof(FILE));
    if (!newfile)
    {
        errno = ENOMEM;
        return NULL; 
    }
    memset(newfile, 0, sizeof(FILE));

    newfile->_fileno = fd;
    rc = init_stream(newfile, flags, PVFS_BUFSIZE);
    if(rc)
    {
        free(newfile);
        return NULL;
    }
    return newfile;
}

/**
 * freopen closes the file and opens another one for the stream
 */
FILE *freopen(const char *path, const char *mode, FILE *stream)
{
    int fd = 0;
    int flags = 0;
    /* see if stream is in use - if so close the file */
    if (stream->_fileno > -1)
    {
        int rc;
        rc = close(stream->_fileno);
        if (rc == -1)
        {
            return NULL;
        }
    }

    /* translate mode to flags */
    flags = mode2flags(mode);

    /* open the file */
    fd = open(path, flags, 0666);
    if (fd == -1)
    {
        return NULL;
    }
    stream->_fileno = fd;

    /* reset buffering here */
    if (stream->_IO_buf_base)
        free (stream->_IO_buf_base);
    init_stream(stream, flags, PVFS_BUFSIZE);

    return stream;
}

/** Implements buffered write using Linux pointer model
 * 
 *  Two sets of pointers, one for reading one for writing
 *  flag determins which mode we are in.  start always 
 *  points to beginning of buffer, end points to end
 *  In read, end points to end of actual data read and
 *  coincides with the file pointer.  In write the start
 *  coincides with file pointer.  In either case ptr is
 *  where user stream pointer is.
 *
 *  The FILE struct is struct _IO_FILE defined in /usr/include/libio.h
 */
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    off64_t rsz, rsz_buf, rsz_extra;
    int rc;

    if (!stream || !ISFLAGSET(stream, _IO_MAGIC) ||
            ISFLAGSET(stream, _IO_NO_WRITES) ||
            !ptr || size <= 0 || nmemb <= 0)
    {
        errno = EINVAL;
        return -1;
    }

    /* Check to see if switching from read to write */
    if (!ISFLAGSET(stream, _IO_CURRENTLY_PUTTING))
    {
        /* write buffer back */
        rc = write(stream->_fileno, stream->_IO_write_base,
                stream->_IO_write_ptr - stream->_IO_write_base); 
        if (rc == -1)
        {
            stream->_flags |= _IO_ERR_SEEN;
            return -1;
        }
        /* reset read pointer */
        stream->_IO_read_ptr = stream->_IO_read_end;
        /* set flag */
        stream->_flags |= _IO_CURRENTLY_PUTTING;
        /* indicate read buffer empty */
        stream->_IO_read_end = stream->_IO_read_base;
        stream->_IO_read_ptr = stream->_IO_read_end;
        /* indicate write buffer empty */
        stream->_IO_write_end = stream->_IO_buf_end;
        stream->_IO_write_ptr = stream->_IO_write_base;
    }

    rsz = size * nmemb;
    rsz_buf = PVFS_util_min(rsz, stream->_IO_write_end - stream->_IO_write_ptr);
    rsz_extra = rsz - rsz_buf;

    if (rsz_buf) /* is only zero if buffer is full */
    {
        memcpy(stream->_IO_write_ptr, ptr, rsz_buf);
        stream->_IO_write_ptr += rsz_buf;
    }

    /* if there is more to write */
    if (rsz_extra)
    {
        /* buffer is full - write the current buffer */
        rc = write(stream->_fileno, stream->_IO_write_base,
                        stream->_IO_write_ptr - stream->_IO_write_base);
        if (rc == -1)
        {
            stream->_flags |= _IO_ERR_SEEN;
            return -1;
        }
        /* reset buffer */
        stream->_IO_write_ptr = stream->_IO_write_base;
        /* if there more data left in request than fits in a buffer */
        if(rsz_extra > stream->_IO_buf_end - stream->_IO_buf_base)
        {
            /* write data directly */
            rc = write(stream->_fileno, ptr + rsz_buf, rsz_extra);
            if (rc == -1)
            {
                stream->_flags |= _IO_ERR_SEEN;
                return -1;
            }
        }
        else
        {
            memcpy(stream->_IO_write_ptr, ptr + rsz_buf, rsz_extra);
            stream->_IO_write_ptr += rsz_extra;
        }
    }
    
    return rsz;
}

/*
 * fread implements the same buffer scheme as in fwrite
 */
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    int fd;
    int rsz, rsz_buf, rsz_extra;
    int bytes_read;
    int rc;

    if (!stream || !ISFLAGSET(stream, _IO_MAGIC) ||
            ISFLAGSET(stream, _IO_NO_READS) ||
            !ptr || size < 0 || nmemb < 0)
    {
        errno = EINVAL;
        return -1;
    }

    /* Check to see if switching from write to read */
    if (ISFLAGSET(stream, _IO_CURRENTLY_PUTTING))
    {
        /* write buffer back */
        rc = write(stream->_fileno, stream->_IO_write_base,
                stream->_IO_write_ptr - stream->_IO_write_base); 
        if (rc == -1)
        {
            stream->_flags |= _IO_ERR_SEEN;
            return -1;
        }
        /* reset write pointer */
        stream->_IO_write_ptr = stream->_IO_write_base;
        /* clear flag */
        stream->_flags &= ~_IO_CURRENTLY_PUTTING;
        /* indicate read buffer empty */
        stream->_IO_read_end = stream->_IO_read_base;
        stream->_IO_read_ptr = stream->_IO_read_end;
    }

    /* see if anything is in read buffer */
    if (stream->_IO_read_end == stream->_IO_read_base ||
        stream->_IO_read_ptr == stream->_IO_read_end)
    {
        /* buffer empty so read new buffer */
        bytes_read = read(stream->_fileno, stream->_IO_read_base,
                stream->_IO_buf_end - stream->_IO_buf_base);
        if (bytes_read == -1)
        {
            stream->_flags |= _IO_ERR_SEEN;
            return -1;
        }
        /* indicate end of read area */
        stream->_IO_read_end = stream->_IO_read_base + bytes_read;
        /* reset read pointer */
        stream->_IO_read_ptr = stream->_IO_read_base;
    }

    /* 
     * we assume there is a block in the buffer now
     * and that the current file pointer corresponds
     * to the end of the read buffer.  The user has
     * only up to the read pointer.
     */
    rsz = size * nmemb;  /* total bytes requested */
    rsz_buf = PVFS_util_min(rsz, stream->_IO_read_end - stream->_IO_read_ptr);
    rsz_extra = rsz - rsz_buf;  /* bytes beyond the buffer */
    
    /* copy rz_buf bytes from buffer */
    if (rsz_buf) /* zero if at EOF */
    {
        memcpy(ptr, stream->_IO_read_ptr, rsz_buf);
        stream->_IO_read_ptr += rsz_buf;
    }

    /* if more bytes requested */
    if (rsz_extra)
    {
        /* if current buffer not at EOF */
        if (stream->_IO_read_end == stream->_IO_buf_end)
        {
            /* if more data requested than fits in buffer */
            if (rsz_extra > (stream->_IO_buf_end - stream->_IO_buf_base))
            {
                /* read directly from file for remainder of request */
                bytes_read = read(stream->_fileno, ptr+rsz_buf, rsz_extra);
                if (bytes_read == -1)
                {
                    stream->_flags |= _IO_ERR_SEEN;
                    return -1;
                }
                if (bytes_read == rsz_extra)
                {
                    /* then read next buffer */
                    bytes_read = read(stream->_fileno, stream->_IO_buf_base,
                            stream->_IO_buf_end - stream->_IO_buf_base);
                    if (bytes_read == -1)
                    {
                        stream->_flags |= _IO_ERR_SEEN;
                        return -1;
                    }
                    stream->_IO_read_end = stream->_IO_read_base + bytes_read;
                    stream->_IO_read_ptr = stream->_IO_read_base;
                    return rsz;
                }
                /* have read to EOF */
                stream->_flags |= _IO_EOF_SEEN;
                return rsz_buf + bytes_read;
            }
            /* rest of request fits in a buffer - read next buffer */
            bytes_read = read(stream->_fileno, stream->_IO_buf_base,
                    stream->_IO_buf_end - stream->_IO_buf_base);
            if (bytes_read == -1)
            {
                stream->_flags |= _IO_ERR_SEEN;
                return -1;
            }
            stream->_IO_read_end = stream->_IO_read_base + bytes_read;
            stream->_IO_read_ptr = stream->_IO_read_base;
            /* transfer remainder */
            rsz_extra = PVFS_util_min(rsz_extra,
                    stream->_IO_read_end - stream->_IO_read_ptr);
            if (rsz_extra) /* zero if at EOF */
            {
                memcpy(ptr, stream->_IO_read_ptr, rsz_extra);
                stream->_IO_read_ptr += rsz_extra;
            }
            if (rsz_buf + rsz_extra < rsz)
            {
                stream->_flags |= _IO_EOF_SEEN;
            }
            return rsz_buf + rsz_extra;
        }
        else
        {
            /* at EOF so return bytes read */
            stream->_flags |= _IO_EOF_SEEN;
            return rsz_buf;
        }
    }
    /* request totally within current buffer */
    return rsz;
}

/**
 * fclose first writes any dirty data in the buffer
 */
int fclose(FILE *stream)
{
    int rc;
    if (!stream || !ISFLAGSET(stream, _IO_MAGIC))
    {
        errno = EINVAL;
        return -1;
    }
    /* write any pending data */
    if (ISFLAGSET(stream, _IO_CURRENTLY_PUTTING))
    {
        if (stream->_IO_write_ptr > stream->_IO_write_base)
        {
            rc = write(stream->_fileno, stream->_IO_write_ptr,
                    stream->_IO_write_ptr - stream->_IO_write_base);
            if (rc == -1)   
            {
                return -1;
            }
        }
    }
    if (!ISFLAGSET(stream, _IO_USER_BUF))
    {
        /* free the buffer */
        free(stream->_IO_buf_base);
    }
    if (!ISFLAGSET(stream, _IO_DELETE_DONT_CLOSE))
    {
        return close(stream->_fileno);
    }
    free(stream);
    return 0;
}

/**
 * fseek wrapper
 */
int fseek(FILE *stream, long offset, int whence)
{

    return fseek64(stream, (off64_t)offset, whence);
}

/** This is the main code for seeking on a stream
 * 
 *  If we seek a short distance within the current buffer
 *  we can just move the stream pointer.  Otherwise we
 *  have to clear the buffer, seek, and start fresh
 */
int fseek64(FILE *stream, const off64_t offset, int whence)
{
    int rc = 0;
    if (!stream || !ISFLAGSET(stream, _IO_MAGIC))
    {
        errno = EINVAL;
        return -1;
    }
    /* if not just getting the position */
    if ((offset != 0L) || (whence != SEEK_CUR))
    {
        int64_t filepos, fileend;
        filepos = lseek64(stream->_fileno, 0, SEEK_CUR);
        fileend = lseek64(stream->_fileno, 0, SEEK_END);
        /* figure out if we are only seeking within the */
        /* bounds of the current buffer to minimize */
        /* unneccessary reads/writes */
        if (whence == SEEK_CUR && ISFLAGSET(stream, _IO_CURRENTLY_PUTTING) &&
                (offset < stream->_IO_write_end - stream->_IO_write_ptr) &&
                (offset > stream->_IO_write_base - stream->_IO_write_ptr))
        {
            stream->_IO_write_ptr += offset;
            return 0;
        }
        if (whence == SEEK_CUR && !ISFLAGSET(stream, _IO_CURRENTLY_PUTTING) &&
                (offset < stream->_IO_read_end - stream->_IO_read_ptr) &&
                (offset > stream->_IO_read_base - stream->_IO_read_ptr))
        {
            stream->_IO_read_ptr += offset;
            return 0;
        }
        if (whence == SEEK_SET && ISFLAGSET(stream, _IO_CURRENTLY_PUTTING) &&
                (offset > filepos) && (offset < filepos +
                (stream->_IO_write_end - stream->_IO_write_base)))
        {
            stream->_IO_write_ptr += offset - filepos;
            return 0;
        }
        if (whence == SEEK_SET && !ISFLAGSET(stream, _IO_CURRENTLY_PUTTING) &&
                (offset < filepos) && (offset > filepos -
                (stream->_IO_read_end - stream->_IO_read_base)))
        {
            stream->_IO_read_ptr += offset - filepos;
            return 0;
        }
        if (whence == SEEK_END && ISFLAGSET(stream, _IO_CURRENTLY_PUTTING) &&
                ((fileend - offset) > filepos) &&
                ((fileend - offset) < filepos +
                (stream->_IO_write_end - stream->_IO_write_base)))
        {
            stream->_IO_write_ptr += (fileend - offset) - filepos;
            return 0;
        }
        if (whence == SEEK_END && !ISFLAGSET(stream, _IO_CURRENTLY_PUTTING) &&
                ((fileend - offset) < filepos) &&
                ((fileend - offset) > filepos -
                (stream->_IO_read_end - stream->_IO_read_base)))
        {
            stream->_IO_read_ptr += (fileend - offset) - filepos;
            return 0;
        }
        /* at this point the seek is beyond the current buffer */
        /* if we are in write mode write back the buffer */
        if (ISFLAGSET(stream, _IO_CURRENTLY_PUTTING) &&
            stream->_IO_write_ptr > stream->_IO_write_base)
        {
            /* write buffer back */
            rc = write(stream->_fileno, stream->_IO_write_base,
                       stream->_IO_write_ptr - stream->_IO_write_base); 
            if (rc < 0)
            {
                return rc;
            }
            /* reset write pointer */
            stream->_IO_write_ptr = stream->_IO_write_base;
        }
        else
        {
            /* in read mode simply clear the buffer */
            /* will force a read at next fread call */
            stream->_IO_read_end = stream->_IO_read_base;
            stream->_IO_read_ptr = stream->_IO_read_end;
        }
        lseek64(stream->_fileno, offset, whence);
    }
    /* seek to current position, no change */
    return 0;
}

/**
 * fsetpos wrapper
 */
int fsetpos(FILE *stream, const fpos_t *pos)
{
    fseek64(stream, (off64_t)(pos->__pos), SEEK_SET);
    return 0;
}

/**
 * rewind wrapper
 */
void rewind(FILE *stream)
{
    fseek64(stream, 0L, SEEK_SET);
}

/**
 * ftell wrapper
 */
long int ftell(FILE *stream)
{
    return (long int)ftell64(stream);
}

off64_t ftell64(FILE* stream)
{
    int64_t filepos;
    if (!stream || !ISFLAGSET(stream, _IO_MAGIC))
    {
        errno = EINVAL;
        return -1;
    }
    filepos = lseek64(stream->_fileno, 0, SEEK_CUR);
    if (ISFLAGSET(stream, _IO_CURRENTLY_PUTTING))
    {
        return filepos + (stream->_IO_write_ptr - stream->_IO_write_base);
    }
    else
    {
        return filepos - (stream->_IO_read_end - stream->_IO_read_ptr);
    }
}

/**
 * fgetpos wrapper
 */
int fgetpos(FILE *stream, fpos_t *pos)
{
    pos->__pos = ftell64(stream);
    return 0;
}

/** forces a write back of potentially dirty buffer
 * 
 *  we don't have a dirty flag, so if user seeks
 *  ahead within the buffer then does a flush
 *  we will do an uncessary write
 */
int fflush(FILE *stream)
{
    int rc;
    if (!stream || !ISFLAGSET(stream, _IO_MAGIC))
    {
        errno = EBADF;
        return -1;
    }
    /* if we are in write mode write back the buffer */
    if (ISFLAGSET(stream, _IO_CURRENTLY_PUTTING) &&
        stream->_IO_write_ptr > stream->_IO_write_base)
    {
        /* write buffer back */
        rc = write(stream->_fileno, stream->_IO_write_base,
                stream->_IO_write_ptr - stream->_IO_write_base); 
        if (rc < 0)
        {
            stream->_flags |= _IO_ERR_SEEN;
            return rc;
        }
        /* reset write pointer */
        stream->_IO_write_ptr = stream->_IO_write_base;
    }
    return 0;
}

/*
 * fputc wrapper
 */
int fputc(int c, FILE *stream)
{
    int rc;
    rc = fwrite(&c, 1, 1, stream);
    if (ferror(stream))
    {
        return EOF;
    }
    return c;
}

/**
 * fputs writes up to a null char
 */
int fputs(const char *s, FILE *stream)
{
    size_t len;
    int rc;
    if (!s)
    {
        errno = EINVAL;
        return EOF;
    }
    len = strlen(s);
    rc = fwrite(s, len, 1, stream);
    if (ferror(stream))
    {
        return EOF;
    }
    return rc;
}

/**
 * putc wrapper
 */
int putc(int c, FILE *stream)
{
    return fputc(c, stream);
}

/**
 * putchar wrapper
 */
int putchar(int c)
{
    return fputc(c, stdout);
}

/**
 * puts wrapper
 */
int puts(const char *s)
{
    int rc;
    rc = fputs(s, stdout);
    if (rc == EOF)
    {
        return EOF;
    }
    return fputs("\n", stdout);
}

/**
 * fgets reads up to size or a newline
 */
char *fgets(char *s, int size, FILE *stream)
{
    char c, *p;

    if (!s || size < 1)
    {
        errno = EINVAL;
        return NULL;
    }
    if (size == 1)
    {
        *s = '\0';
        return s;
    }
    p = s;
    size--;
    do {
        *p++ = c = fgetc(stream);
    } while (--size && c != '\n' && !feof(stream) && !ferror(stream));
    if (ferror(stream))
    {
        return NULL;
    }
    *p = '\0';
    return s;
}

/**
 * fgetc wrapper
 */
int fgetc(FILE *stream)
{
    int rc, ch;

    rc = fread(&ch, 1, 1, stream);
    if (ferror(stream))
    {
        return EOF;
    }
    return ch;
}

/**
 * getc wrapper
 */
int getc(FILE *stream)
{
    return fgetc(stream);
}

/**
 * getchar wrapper
 */
int getchar(void)
{
    return fgetc(stdin);
}

/**
 * gets
 */
char *gets(char * s)
{
    char c, *p;

    if (!s)
    {
        errno = EINVAL;
        return NULL;
    }
    p = s;
    do {
        *p++ = c = fgetc(stdin);
    } while (c != '\n' && !feof(stdin) && !ferror(stdin));
    if (ferror(stdin))
    {
        return NULL;
    }
    return s;
}

/**
 * ungetc wrapper
 *
 * TODO: at the moment this will not unget beyond the current
 *       buffer - needs a better implementation using the backup
 *       buffer area _IO_save_base, _IO_save_end, _IO_backup_base
 */
int ungetc(int c, FILE *stream)
{
    int64_t rc;

    rc = fseek64(stream, -1L, SEEK_CUR);
    if (rc < 0)
    {
        return EOF;
    }
    return c;
}

/**
 * fprintf using a var arg list
 */
int vfprintf(FILE *stream, const char *format, va_list ap)
{
    char *buf;
    int len, rc;

    len = vasprintf(&buf, format, ap);
    if (len < 0)
    {
        return len;
    }
    if (len > 0 && buf)
    {
        rc = fwrite(buf, len, 1, stream);
    }
    if (buf)
    {
        free(buf);
    }
    return rc;
}

int vprintf(const char *format, va_list ap)
{
    return vfprintf(stdout, format, ap);
}

/**
 * fprintf wrapper
 */
int fprintf(FILE *stream, const char *format, ...)
{
    size_t len;
    va_list ap;

    va_start(ap, format);
    len = vfprintf(stream, format, ap);
    va_end(ap);
    return len;
}

/**
 * printf wrapper
 */
int printf(const char *format, ...)
{
    size_t len;
    va_list ap;

    va_start(ap, format);
    len = vfprintf(stdout, format, ap);
    va_end(ap);
    return len;
}

#if 0
/* TODO: These are not implemented yet */

fscanf()
{
}

scanf()
{
}
#endif

/**
 * Stdio utilitie to clear error and eof for a stream
 */
void clearerr (FILE *stream)
{
    if (!stream || !ISFLAGSET(stream, _IO_MAGIC))
    {
        return;
    }
    stream->_flags &= ~_IO_ERR_SEEN;
    stream->_flags &= ~_IO_EOF_SEEN;
}

/**
 * Stdio utilitie to check if a stream is at EOF
 */
int feof (FILE *stream)
{
    if (!stream || !ISFLAGSET(stream, _IO_MAGIC))
    {
        errno = EBADF;
        return -1;
    }
    return stream->_flags & _IO_EOF_SEEN;
}

/**
 * Stdio utilitie to check for error on a stream
 */
int ferror (FILE *stream)
{
    if (!stream || !ISFLAGSET(stream, _IO_MAGIC))
    {
        errno = EBADF;
        return -1;
    }
    return stream->_flags & _IO_ERR_SEEN;
}

/**
 * Stdio utilitie to get file descriptor from a stream
 */
int fileno (FILE *stream)
{
    if (!stream || !ISFLAGSET(stream, _IO_MAGIC))
    {
        errno = EBADF;
        return -1;
    }
    return stream->_fileno;
}

/** stdio function to delete a file
 *
 */
int remove (const char *path)
{
    int rc;
    struct stat buf;

    rc = stat(path, &buf);
    if (S_ISDIR(buf.st_mode))
        return rmdir (path);
    return unlink (path);
}

/**
 *  setbuf wrapper
 */
void setbuf (FILE *stream, char *buf)
{
    setvbuf(stream, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

/**
 *  setbuffer wrapper
 */
void setbuffer (FILE *stream, char *buf, size_t size)
{
    setvbuf(stream, buf, buf ? _IOFBF : _IONBF, size);
}

/**
 *  setlinbuf wrapper
 */
void setlinebuf (FILE *stream)
{
    setvbuf(stream, (char *)NULL, _IOLBF, 0);
}

/**
 *
 * This should only be called on a stream that has ben opened
 * but not used so we can assume any exitinf buff is not dirty
 */
int setvbuf (FILE *stream, char *buf, int mode, size_t size)
{
    if (!stream || !ISFLAGSET(stream, _IO_MAGIC))
    {
        errno = EBADF;
        return -1;
    }
    if ((stream->_IO_read_end != stream->_IO_buf_base) ||
        (stream->_IO_write_ptr != stream->_IO_buf_base))
    {
        /* fread or fwrite has been called */
        errno - EINVAL;
        return -1;
    }
    switch (mode)
    {
    case _IOFBF : /* full buffered */
        /* this is the default */
        break;
    case _IOLBF : /* line buffered */
        stream->_flags |= _IO_LINE_BUF; /* TODO: This is not implemented */
        break;
    case _IONBF : /* not buffered */
        stream->_flags |= _IO_UNBUFFERED; /* TODO: This is not implemented */
        break;
    default :
        errno = EINVAL;
        return -1;
    }
    if (buf && size > 0)
    {
        stream->_flags |= _IO_USER_BUF;
        free(stream->_IO_buf_base);
        stream->_IO_buf_base   = buf;
        stream->_IO_buf_end    = stream->_IO_buf_base + size;
        stream->_IO_read_base  = stream->_IO_buf_base;
        stream->_IO_read_ptr   = stream->_IO_buf_base;
        stream->_IO_read_end   = stream->_IO_buf_base;
        stream->_IO_write_base = stream->_IO_buf_base;
        stream->_IO_write_ptr  = stream->_IO_buf_base;
        stream->_IO_write_end  = stream->_IO_buf_end;
    }
}

/**
 * mkdtemp makes a temp dir and returns an fd 
 */
char *mkdtemp(char *template)
{
    int fd;
    int len;
    int rnum;
    int try;

    if (!template)
    {
        errno = EINVAL;
        return NULL;
    }
    len = strlen(template);
    if (!strncmp(&template[len-6],"XXXXXX",6))
    {
        errno = EINVAL;
        return NULL;
    }
    for(try = 0; try < MAXTRIES; try++)
    {
        rnum = PINT_random() % 1000000;
        sprintf(&template[len-6],"%06d", rnum);
        fd = mkdir(template, 0700);
        if (fd < 0)
        {
            if (errno == EEXIST)
            {
                continue;
            }
            return NULL;
        }
    }
    return template;
}

/**
 * mkstemp makes a temp file and returns an fd 
 */
int mkstemp(char *template)
{
    int fd;
    int len;
    int rnum;
    int try;

    if (!template)
    {
        errno = EINVAL;
        return -1;
    }
    len = strlen(template);
    if (!strncmp(&template[len-6],"XXXXXX",6))
    {
        errno = EINVAL;
        return -1;
    }
    for(try = 0; try < MAXTRIES; try++)
    {
        rnum = PINT_random() % 1000000;
        sprintf(&template[len-6],"%06d", rnum);
        fd = open(template, O_RDWR|O_EXCL|O_CREAT, 0600);
        if (fd < 0)
        {
            if (errno == EEXIST)
            {
                continue;
            }
            return -1;
        }
    }
    return fd;
}

/**
 * tmpfile makes a temp file and returns a stream 
 */
FILE *tmpfile(void)
{
    char *template = "/tmp/tmpfileXXXXXX";
    int fd;
    fd = mkstemp(template);
    if (fd < 0)
    {
        return NULL;
    }
    return fdopen(fd, "r+");
}

/**
 * opendir opens a directory as a stream
 */
DIR *opendir (const char *name)
{
    int fd;
    if(!name)
    {
        errno = EINVAL;
        return NULL;
    }
    fd = open(name, O_RDONLY|O_DIRECTORY);
    if (fd == -1);
    {
        return NULL;
    }
    return fdopendir(fd);
}

/**
 * creates a stream for an already open directory
 */
DIR *fdopendir (int fd)
{
    DIR *dstr;
    dstr = (DIR *)malloc(sizeof(DIR));
    if (dstr == NULL)
    {
        return NULL;
    }
    dstr->flags = DIRSTREAM_MAGIC;
    dstr->fileno = fd;
    dstr->buf_base = (char *)malloc(DIRBUFSIZE);
    dstr->buf_end = dstr->buf_base + DIRBUFSIZE;
    dstr->buf_act = dstr->buf_base;
    dstr->buf_ptr = dstr->buf_base;
}

/**
 * returns the file descriptor for a directory stream
 */
int dirfd (DIR *dir)
{
    if (!dir || !(dir->flags == DIRSTREAM_MAGIC))
    {
        errno = EBADF;
        return -1;
    }
    return dir->fileno;
}

/**
 * readdir wrapper
 */
struct dirent *readdir (DIR *dir)
{
    struct dirent64 *de64;
    /* this buffer is so we can return the smaller struct for
     * single use as is the prerogative of the call.  This
     * approach sucks, not reentrant TODO: find a better way
     */
    static struct dirent *de = NULL;

    if (!de)
    {
        de = (struct dirent *)malloc(sizeof(struct dirent));
        if (!de)
        {
            return NULL;
        }
    }

    de64 = readdir64(dir);
    if (de64 == NULL)
    {
        return NULL;
    }
    memcpy(de->d_name, de64->d_name, 256);
    de->d_ino = de64->d_ino;
    /* these are Linux specific fields from the dirent */
#if 1
    de->d_off = de64->d_off;
    de->d_reclen = de64->d_reclen;
    de->d_type = de64->d_type;
#endif
    return de;
}

/**
 * reads a single dirent64 in buffered mode from a stream
 */
struct dirent64 *readdir64 (DIR *dir)
{
    struct dirent64 *rval;
    if (!dir || !(dir->flags == DIRSTREAM_MAGIC))
    {
        errno = EBADF;
        return NULL;
    }
    if (dir->buf_ptr >= dir->buf_act)
    {
        int bytes_read;
        /* read a block of dirents into the buffer */
        bytes_read = getdents(dir->fileno, dir->buf_base,
                             (dir->buf_end - dir->buf_base));
        dir->buf_act = dir->buf_base + bytes_read;
        dir->buf_ptr = dir->buf_base;
    }
    rval = (struct dirent64 *)dir->buf_ptr;
    dir->buf_ptr += rval->d_reclen;
    return rval;
}

/**
 * rewinds a directory stream
 */
void rewinddir (DIR *dir)
{
    off_t filepos;
    if (!dir || !(dir->flags == DIRSTREAM_MAGIC))
    {
        errno = EBADF;
        return;
    }
    filepos = lseek(dir->fileno, 0, SEEK_CUR);
    if ((filepos - (dir->buf_act - dir->buf_base)) == 0)
    {
        dir->buf_ptr = dir->buf_base;
    }
    else
    {
        dir->buf_act = dir->buf_base;
        dir->buf_ptr = dir->buf_base;
        lseek(dir->fileno, 0, SEEK_SET);
    }
}

/**
 * seeks in a direcotry stream
 */
void seekdir (DIR *dir, off_t offset)
{
    off_t filepos;
    if (!dir || !(dir->flags == DIRSTREAM_MAGIC))
    {
        errno = EBADF;
        return;
    }
    filepos = lseek(dir->fileno, 0, SEEK_CUR);
    if ((filepos - (dir->buf_act - dir->buf_base)) <= offset &&
        filepos >= offset)
    {
        dir->buf_ptr = dir->buf_act - (filepos - offset);
    }
    else
    {
        dir->buf_act = dir->buf_base;
        dir->buf_ptr = dir->buf_base;
        lseek(dir->fileno, offset, SEEK_SET);
    }
}

/**
 * returns current position in a direcotry stream
 */
off_t telldir (DIR *dir)
{
    off_t filepos;
    if (!dir || !(dir->flags == DIRSTREAM_MAGIC))
    {
        errno = EBADF;
        return -1;
    }
    filepos = lseek(dir->fileno, 0, SEEK_CUR);
    if (filepos == -1)
    {
        return -1;
    }
    return filepos - (dir->buf_act - dir->buf_ptr);
}

/**
 * closes a direcotry stream
 */
int closedir (DIR *dir)
{
    if (!dir || !(dir->flags == DIRSTREAM_MAGIC))
    {
        errno = EBADF;
        return -1;
    }
    free(dir->buf_base);
    dir->flags = 0;
    free(dir);
    return 0;
}

int scandir (const char *dir,
             struct dirent ***namelist,
             int(*filter)(const struct dirent *),
             int(*compar)(const void *, const void *))
{
    struct dirent *de;
    DIR *dp;
    int len, i, rc;
    int asz = ASIZE;

    /* open directory */
    dp = opendir(dir);
    /* allocate namelist */
    *namelist = (struct dirent **)malloc(asz * sizeof(struct dirent *));
    if (!*namelist)
    {
        return -1;
    }
    /* loop through the dirents */
    for(i = 0, de = readdir(dp); de; i++, de = readdir(dp))
    {
        if (!filter || filter(de))
        {
            if (i >= asz)
            {
                struct dirent **darray;
                /* ran out of space, realloc */
                darray = (struct dirent **)realloc(*namelist, asz + ASIZE);
                if (!rc)
                {
                    int j;
                    for (j = 0; j < i; j++)
                    {
                        free(*namelist[j]);
                    }
                    free(*namelist);
                    return -1;
                }
                *namelist = darray;
                asz += ASIZE;
            }
            /* find the size of this entry */
            len = strnlen((*namelist)[i]->d_name, NAME_MAX + 1) +
                           sizeof(struct dirent);
            /* add to namelist */
            *namelist[i] = (struct dirent *)malloc(len);
            memcpy((*namelist)[i], de, len);
        }
    }
    /* now sort entries */
    qsort(*namelist, i, sizeof(struct dirent *), compar);
    rc = closedir(dp);
    if (rc == -1)
    {
        return -1;
    }
    return i;
}

/**
 * 64 bit version of scandir
 *
 * TODO: Would prefer not to copy code - modify to a generic version
 * and then call from two wrapper versions would be beter
 * pass in a flag to control the copy of the dirent into the array
 */
int scandir64 (const char *dir, struct dirent64 ***namelist,
             int(*filter)(const struct dirent64 *),
             int(*compar)(const void *, const void *))
{
    struct dirent64 *de;
    DIR *dp;
    int len, i, rc;
    int asz = ASIZE;

    /* open directory */
    dp = opendir(dir);
    /* allocate namelist */
    *namelist = (struct dirent64 **)malloc(asz * sizeof(struct dirent64 *));
    if (!*namelist)
    {
        return -1;
    }
    /* loop through the dirents */
    for(i = 0, de = readdir64(dp); de; i++, de = readdir64(dp))
    {
        if (!filter || filter(de))
        {
            if (i >= asz)
            {
                struct dirent64 **darray;
                /* ran out of space, realloc */
                darray = (struct dirent64 **)realloc(*namelist, asz + ASIZE);
                if (!rc)
                {
                    int j;
                    for (j = 0; j < i; j++)
                    {
                        free(*namelist[j]);
                    }
                    free(*namelist);
                    return -1;
                }
                *namelist = darray;
                asz += ASIZE;
            }
            /* find the size of this entry */
            len = strnlen((*namelist)[i]->d_name, NAME_MAX + 1) +
                           sizeof(struct dirent64);
            /* add to namelist */
            (*namelist)[i] = (struct dirent64 *)malloc(len);
            memcpy((*namelist)[i], de, len);
        }
    }
    /* now sort entries */
    qsort(*namelist, i, sizeof(struct dirent64 *), compar);
    rc = closedir(dp);
    if (rc == -1)
    {
        return -1;
    }
    return i;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

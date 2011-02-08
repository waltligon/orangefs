#include <usrint.h>

static int pvfs_is_sys_initialized = 0; 

#define PREALLOC 3
static int descriptor_table_count = 0; 
static int descriptor_table_size = 0; 
static int next_descriptor = 0; 
static pvfs_descriptor **descriptor_table; 

pvfs_descriptor pvfs_stdin =
{
    .fd = 0,
    .fsops = &glibc_ops,
    .posix_fd = STDIN_FILENO,
    .pvfs_ref.fs_id = 0,
    .pvfs_ref.handle = 0,
    .flags = O_RDONLY,
    .file_pointer = 0,
    .is_in_use = 1,
    .dirty = 0,
    .buf = NULL,
    .buftotal = 0,
    .bufsize = 0,
    .buf_off = 0,
    .bufptr = NULL
};

pvfs_descriptor pvfs_stdout =
{
    .fd = 1,
    .fsops = &glibc_ops,
    .posix_fd = STDOUT_FILENO,
    .pvfs_ref.fs_id = 0,
    .pvfs_ref.handle = 0,
    .flags = O_WRONLY | O_APPEND,
    .file_pointer = 0,
    .is_in_use = 1,
    .dirty = 0,
    .buf = NULL,
    .buftotal = 0,
    .bufsize = 0,
    .buf_off = 0,
    .bufptr = NULL
};

pvfs_descriptor pvfs_stderr =
{
    .fd = 2,
    .fsops = &glibc_ops,
    .posix_fd = STDERR_FILENO,
    .pvfs_ref.fs_id = 0,
    .pvfs_ref.handle = 0,
    .flags = O_WRONLY | O_APPEND,
    .file_pointer = 0,
    .is_in_use = 1,
    .dirty = 0,
    .buf = NULL,
    .buftotal = 0,
    .bufsize = 0,
    .buf_off = 0,
    .bufptr = NULL
};


/* 
 * Perform PVFS initialization tasks
 */ 

int pvfs_sys_init() { 
	struct rlimit rl; 
	int rc; 

	/* initalize the file system */ 
	PVFS_util_init_defaults(); 

	rc = getrlimit(RLIMIT_NOFILE, &rl); 
	/* need to check for "INFINITY" */

	descriptor_table_size = rl.rlim_max;
	descriptor_table =
			(pvfs_descriptor **)malloc(sizeof(pvfs_descriptor *) *
			descriptor_table_size);
	memset(descriptor_table, 0,
			(sizeof(pvfs_descriptor *) * descriptor_table_size));
    descriptor_table[0] = &pvfs_stdin;
    descriptor_table[1] = &pvfs_stdout;
    descriptor_table[2] = &pvfs_stderr;
	next_descriptor = PREALLOC;

	/* Mark the initialization complete */ 
	pvfs_is_sys_initialized = 1; 
	return PVFS_FD_SUCCESS; 
}

int pvfs_descriptor_table_size(void)
{
    return descriptor_table_size;
}


/*
 * Allocate a new pvfs_descriptor
 * initialize fsops to the given set
 */
 pvfs_descriptor *pvfs_alloc_descriptor(posix_ops *fsops)
 {
 	int i; 
	if (descriptor_table_count == (descriptor_table_size - PREALLOC))
	{
		// print error
		return NULL;
	}

   /* find next empty slot in table */
	for (i = next_descriptor; descriptor_table[i];
			i = (i == descriptor_table_size-1) ? PREALLOC : i++);

   /* found a slot */
	descriptor_table[i] = malloc(sizeof(pvfs_descriptor));
	if (descriptor_table[i] == NULL)
	{
		// print error
		return NULL;
	}
	next_descriptor = ((i == descriptor_table_size-1) ? PREALLOC : i++);
	descriptor_table_count++;

	/* fill in descriptor */
	descriptor_table[i]->fd = i;
	descriptor_table[i]->dup_cnt = 1;
	descriptor_table[i]->fsops = fsops;
	descriptor_table[i]->posix_fd = i;
	descriptor_table[i]->pvfs_ref.fs_id = 0;
	descriptor_table[i]->pvfs_ref.handle = 0;
	descriptor_table[i]->flags = 0;
	descriptor_table[i]->file_pointer = 0;
	descriptor_table[i]->is_in_use = 0;
	descriptor_table[i]->dirty = 0;
	descriptor_table[i]->eof = 0;
	descriptor_table[i]->error = 0;
	descriptor_table[i]->buf = NULL;
	descriptor_table[i]->buftotal = 0;
	descriptor_table[i]->bufsize = 0;
	descriptor_table[i]->buf_off = 0;
	descriptor_table[i]->bufptr = NULL;

   return descriptor_table[i];
}

/*
 * Function for dupliating a descriptor - used in dup and dup2 calls
 */
int pvfs_dup_descriptor(int oldfd, int newfd)
{
    if (newfd == -1)
    {
        /* find next empty slot in table */
        for (newfd = next_descriptor; descriptor_table[newfd];
            newfd = (newfd == descriptor_table_size-1) ? PREALLOC : newfd++);
    }
    else
    {
        if (descriptor_table[newfd] != NULL)
        {
            /* close old file in new slot */
            pvfs_close(newfd);
        }
    }
    descriptor_table[newfd] = descriptor_table[oldfd];
    descriptor_table[newfd]->dup_cnt++;
	descriptor_table_count++;
}

/*
 * Return a pointer to the pvfs_descriptor for the file descriptor or null
 * if there is no entry for the given file descriptor
 * should probably be inline if we can get at static table that way
 */
pvfs_descriptor *pvfs_find_descriptor(int fd)
{
	return descriptor_table[fd];
}

int pvfs_free_descriptor(int fd)
{
    pvfs_descriptor *pd;

    pd = descriptor_table[fd];

	/* clear out table entry */
	descriptor_table[fd] = NULL;

	/* keep up with used descriptors */
	descriptor_table_count++;

    /* check if last copy */
    if (--(pd->dup_cnt) <= 0)
    {
	    /* free buffer space */
	    if (pd->buf)
        {
		    free(pd->buf);
        }
	    /* free descriptor - wipe memory first */
	    memset(pd, 0, sizeof(pvfs_descriptor));
	    free(pd);
    }

	return 0;
}

/* 
 * takes a path that is relative to the working dir and
 * expands it all the way to the root
 */
char * pvfs_qualify_path(const char *path)
{
    char *rc = NULL;
    int i = 1;
    int cdsz;
    int psz;
    char *curdir;
    char *newpath;

    if(path[0] != '/')
    {
        /* loop until our temp buffer is big enough for the */
        /* current directory */
        do
        {
            if (i > 1)
                free(curdir);
            curdir = (char *)malloc(i * 256);
            if (curdir == NULL)
                return NULL;
            rc = getcwd(curdir, i * 256);
            i++;
        } while ((rc == NULL) && (errno == ERANGE));
        if (rc == NULL)
        {
            /* some other error, bail out */
            free(curdir);
            return NULL;
        }
        cdsz = strlen(curdir);
        psz = strlen(path);
        /* allocate buffer for whole path and copy */
        newpath = (char *)malloc(cdsz+psz+2);
        strncpy(newpath, curdir, cdsz);
        free(curdir);
        strncat(newpath, "/", 1);
        strncat(newpath, path, psz);
    }
    else
        newpath = (char *)path;
    return newpath;
}

/* 
 *Determines if a path is part of a PVFS Filesystem 
 */

int is_pvfs_path(const char *path) {
   struct statfs file_system;
   char * directory = NULL ;
   char *str;

   if(path[0] != '/') {
      directory = getcwd(NULL, 0);
      str  = malloc((strlen(directory)+1)*sizeof(char));
      strcpy(str, directory);
      free(directory);
   }
   else {
      str = malloc((strlen(path)+1) *sizeof(char));
      strcpy(str, path);
   }
   if(str == NULL) {
      pvfs_debug("Malloc has failed\n");
   }

   int count;
   for(count = strlen(str) -2; count > 0; count--) {
      if(str[count] == '/') {
         str[count] = '\0';
         break;
      }
   }
   /* this must call standard glibc statfs */
   glibc_ops.statfs(str, &file_system);
   free(str);
   if(file_system.f_type == PVFS_FS) {
#ifdef DEBUG
   printf("IS PVFS_PATH\n");
#endif
      return true;
   }
   else if(file_system.f_type == LINUX_FS) {
#ifdef DEBUG
   printf("IS NOT PVFS_PATH\n");
#endif
      return false;
   }
   else {
      printf("NO A LINUX OR PVFS FILE SYSTEM!! (BAILING OUT!!!)\n");
      exit(1);
   }
}

void pvfs_debug(char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

/**
 * Split a pathname into a directory and a filename.  If non-null
 * is passed as the directory or filename, the field will be allocated and
 * filled with the correct value
 */
int split_pathname( const char *path,
                    char **directory,
                    char **filename)
{
	/* chop off pvfs2 prefix */
	if (strncmp(path,"pvfs2:",strlen("pvfs2:")) == 0)
		path = &path[strlen("pvfs2:")];

    /* Split path into a directory and filename */
    int path_length = strlen(path);
    if ('/' == path[0])
    {
        int i;
        for (i = path_length - 1; i >= 0; --i)
        {
            if ( '/' == path[i] )
            {
                /* parse the directory */
                if (0 != directory)
                {
                    *directory = malloc(i + 1);
                    if (0 != directory)
                    {
                        strncpy(*directory, path, i);
                        (*directory)[i] = '\0';
                    }
                }
                /* parse the filename */
                if (0 != filename)
                {
                    *filename = malloc(path_length - i + 1);
                    if (0 != filename)
                    {
                        strncpy(*filename, path + i + 1, path_length - i);
                        (*filename)[path_length - i] = '\0';
                    }
                }
                break;
            }
        }
    }
    else
    {
        fprintf(stderr, "Error: Not an absolute path: %s\n", path);
        return PVFS_FD_FAILURE;
    }
    return PVFS_FD_SUCCESS;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4 sts=4 sw=4 expandtab
 */

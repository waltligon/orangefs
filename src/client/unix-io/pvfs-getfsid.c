/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#include "pint-userlib.h"
#include <config-manage.h>
//#include <limits.h>
//#include <sys/param.h>
//#include <sys/stat.h>
//#include <alloca.h>
//#include <sys/types.h>

/* Function Prototypes */
static int canonicalize(const char *name, char *outbuf);
static int unix_lstat(const char *fn, struct stat *s_p);
extern pvfs_mntlist mnt;

/* Function: pvfs_getfsid() 
 * 
 * if the given file is a PVFS file, determines the canonicalized
 * file name, and the collection id(fsid) 
 * 
 * returns 0 on success, -1 on error
 *
 */
int pvfs_getfsid(const char *fname, int *result, char **abs_fname,
		PVFS_fs_id *collid)
{
	pvfs_mntent ent; /* Mount entry */
	int len, ret = 0;
	char *remainder;
	char canonicalize_outbuf[PVFS_NAME_MAX]; /* Absolute pathname */
	char snambuf[PVFS_NAME_MAX+1]; /* Absolute pathname relative to server */

	/* canonicalize the filename, result in canonicalize_outbuf 
	 * Obtains the absolute pathname on the client side
	 */
	if (canonicalize(fname, canonicalize_outbuf)) 
		return(-1);

	/* check for pvfs fstab match, return 0 if no match 
	 * returns the directory that has been mounted
	 */
	/* Can we search pvfstab instead of fstab ? What we
	 * get from fstab is a full mount entry, but do we
	 * need all that info? We mainly need the client mount
	 * point and maybe the server mount point?
	 * We can go ahead and use pvfstab */
	ret  = search_pvfstab(canonicalize_outbuf, mnt, &ent); 
	if (ret < 0)
	{
		/*printf("Not a PVFS file\n");*/
		*result = FS_UNIX;
		return(0);
	}
	*result = FS_PVFS;

	/* piece together the filename at the server end
	 *
	 * Steps:
	 * 1) skip over hostname and ':' in fsname
	 * 2) replace directory name in filename with one on server
	 *
	 * Assumption: unless mnt_dir refers to the root directory, there
	 * will be no trailing /.
	 */
	len = strlen(ent.local_mnt_dir);
	/* Get only the file name */
	remainder = canonicalize_outbuf + len;
	/* Get the filename */
	*abs_fname = (char *)malloc(strlen(remainder) + 1);
	if (!(*abs_fname))
	{
		printf("Error in memory allocation\n");	
		return(-ENOMEM);
	}
	strncpy(*abs_fname,remainder,strlen(remainder));
	(*abs_fname)[strlen(remainder)] = '\0';
	/* Need to return the fs id(Coll Id) .How to determine that? */
	/* mnt_dir is the mount point..client side */
	ret = config_fsi_get_fsid(collid,ent.local_mnt_dir); 
	/* snambuf now contains the absolute pathname on the server */
	snprintf(snambuf, PVFS_NAME_MAX, "%s/%s", ent.service_name, remainder);

	return(0);

}

/* MODIFIED CANONICALIZE FUNCTION FROM GLIBC BELOW */

/* Return the canonical absolute name of file NAME.	A canonical name
	does not contain any `.', `..' components nor any repeated path
	separators ('/') or symlinks.

	Path components need not exist.	If they don't, it will be assumed
	that they are not symlinks.	This is necessary for our work with
	PVFS.

	Output is returned in canonicalize_outbuf.

	If RESOLVED is null, the result is malloc'd; otherwise, if the
	canonical name is PATH_MAX chars or more, returns null with `errno'
	set to ENAMETOOLONG; if the name fits in fewer than PATH_MAX chars,
	returns the name in RESOLVED.	If the name cannot be resolved and
	RESOLVED is non-NULL, it contains the path of the first component
	that cannot be resolved.	If the path can be resolved, RESOLVED
	holds the same value as the value returned.
 */

#define __set_errno(x) errno = (x)
#define __alloca alloca
#define __getcwd getcwd
#define __readlink readlink
#define __lxstat(x,y,z) unix_lstat((y),(z))

int canonicalize(const char *name, char *outbuf)
{
	char *rpath, *dest, *extra_buf = NULL;
	const char *start, *end, *rpath_limit;
	long int path_max;
	int num_links = 0, err = 0;

	if (name == NULL) {
		/* As per Single Unix Specification V2 we must return an error if
			 either parameter is a null pointer.	We extend this to allow
			 the RESOLVED parameter be NULL in case the we are expected to
			 allocate the room for the return value.	*/
		__set_errno (EINVAL);
		return(-1) ;
	}

	if (name[0] == '\0') {
		/* As per Single Unix Specification V2 we must return an error if
			 the name argument points to an empty string.	*/
		__set_errno (ENOENT);
		return(-1);
	}

#ifdef PATH_MAX
	path_max = PATH_MAX;
#else
	path_max = pathconf(name, _PC_PATH_MAX);
	if (path_max <= 0)
		path_max = 1024;
#endif

	rpath = (char *) __alloca (path_max);
	rpath_limit = rpath + path_max;

	if (name[0] != '/') {
		if (!__getcwd (rpath, path_max))
			goto error;
		dest = strchr (rpath, '\0');
	}
	else {
		rpath[0] = '/';
		dest = rpath + 1;
	}

	for (start = end = name; *start; start = end) {
		struct stat st;
		int n;

		/* Skip sequence of multiple path-separators.	*/
		while (*start == '/')
			++start;

		/* Find end of path component.	*/
		for (end = start; *end && *end != '/'; ++end)
			/* Nothing.	*/;

		if (end - start == 0)
			break;
		else if (end - start == 1 && start[0] == '.')
			/* nothing */;
		else if (end - start == 2 && start[0] == '.' && start[1] == '.') {
			/* Back up to previous component, ignore if at root already.	*/
			if (dest > rpath + 1)
				while ((--dest)[-1] != '/');
		}
		else {
			if (dest[-1] != '/')
				*dest++ = '/';

			if (dest + (end - start) >= rpath_limit) {
				__set_errno (ENAMETOOLONG);
				goto error;
			}

#if 0
			/* NOTE: gnuism; should remove */
			dest = (char *)__mempcpy (dest, start, end - start);
#endif
			memmove(dest, start, end - start);
			dest = (char *) dest + (end - start);
			*dest = '\0';

			/* we used to crap out in this case; now we simply note that we
			 * hit an error and stop trying to stat from now on. -- Rob
			 */
			if (!err && __lxstat (_STAT_VER, rpath, &st) < 0) {
				err++;
			}
			if (!err && (S_ISLNK (st.st_mode))) {
				char *buf = (char *) __alloca (path_max);
				size_t len;

				if (++num_links > MAXSYMLINKS) {
					__set_errno (ELOOP);
					goto error;
				}

				n = __readlink (rpath, buf, path_max);
				if (n < 0)
					goto error;
				buf[n] = '\0';

				if (!extra_buf)
					extra_buf = (char *) __alloca (path_max);

				len = strlen (end);
				if ((long int) (n + len) >= path_max) {
					__set_errno (ENAMETOOLONG);
					goto error;
				}

				/* Careful here, end may be a pointer into extra_buf... */
				memmove (&extra_buf[n], end, len + 1);
				name = end = memcpy (extra_buf, buf, n);

				if (buf[0] == '/')
					dest = rpath + 1;	/* It's an absolute symlink */
				else
					/* Back up to previous component, ignore if at root already: */
					if (dest > rpath + 1)
						while ((--dest)[-1] != '/');
			}
		}
	}
	if (dest > rpath + 1 && dest[-1] == '/')
		--dest;
	*dest = '\0';

	memcpy(outbuf, rpath, dest - rpath + 1);
	return(0);

error:
	/* copy in component causing trouble */
	strcpy (outbuf, rpath);
	return(-1);
}

static int unix_lstat(const char *fn, struct stat *s_p)
{
	return lstat(fn,s_p);
}

/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "str-utils.h"

/* PINT_string_count_segments()
 *
 * Count number of segments in a path.
 *
 * Parameters:
 * pathname   - pointer to string
 *
 * Returns number of segments in pathname; -1 if
 * pathname is invalid or has no components
 *
 * Example inputs and return values:
 *
 * NULL              - returns -1
 * /                 - returns -1
 * filename          - returns  1
 * /filename         - returns  1
 * /filename/        - returns  1
 * /filename//       - returns  1
 * /dirname/filename - returns  2
 * dirname/filename
 *
 */
int PINT_string_count_segments(char *pathname)
{
    int count = 0;
    char *segp = (char *)0;
    void *segstate;

    while(!PINT_string_next_segment(pathname,&segp,&segstate))
    {
        count++;
    }
    return count;
}

/* PINT_get_base_dir()
 *
 * Get base (parent) dir of a absolute path
 *
 * Parameters:
 * pathname     - pointer to directory string
 * out_base_dir - pointer to out base dir string
 * max_out_len  - max length of out_base_dir buffer
 *
 * All incoming arguments must be valid and non-zero
 *
 * Returns 0 on success; -1 if args are invalid
 *
 * Example inputs and outputs/return values:
 *
 * pathname: /tmp         - out_base_dir: /         - returns  0
 * pathname: /tmp/foo     - out_base_dir: /tmp      - returns  0
 * pathname: /tmp/foo/bar - out_base_dir: /tmp/foo  - returns  0
 *
 *
 * invalid pathname input examples:
 * pathname: /            - out_base_dir: undefined - returns -1
 * pathname: NULL         - out_base_dir: undefined - returns -1
 * pathname: foo          - out_base_dir: undefined - returns -1
 *
 */
int PINT_get_base_dir(char *pathname, char *out_base_dir, int out_max_len)
{
    int ret = -1, len = 0;
    char *start, *end;

    if (pathname && out_base_dir && out_max_len)
    {
        if ((strcmp(pathname,"/") == 0) || (pathname[0] != '/'))
        {
            return ret;
        }

        start = pathname;
        end = (char *)(pathname + strlen(pathname));

        while(end && (end > start) && (*(--end) != '/'));

        /*
          get rid of trailing slash unless we're handling
          the case where parent is the root directory
          (in root dir case, len == 1)
        */
        len = (int)((char *)(++end - start));
        if (len != 1)
        {
            len--;
        }
        if (len < out_max_len)
        {
            memcpy(out_base_dir,start,len);
            out_base_dir[len] = '\0';
            ret = 0;
        }
    }
    return ret;
}

/* PINT_remove_base_dir()
 *
 * Get absolute path minus the base dir
 *
 * Parameters:
 * pathname     - pointer to directory string
 * out_base_dir - pointer to out dir string
 * max_out_len  - max length of out_base_dir buffer
 *
 * All incoming arguments must be valid and non-zero
 *
 * Returns 0 on success; -1 if args are invalid
 *
 * Example inputs and outputs/return values:
 *
 * pathname: /tmp/foo     - out_base_dir: foo       - returns  0
 * pathname: /tmp/foo/bar - out_base_dir: bar       - returns  0
 *
 *
 * invalid pathname input examples:
 * pathname: /            - out_base_dir: undefined - returns -1
 * pathname: NULL         - out_base_dir: undefined - returns -1
 * pathname: foo          - out_base_dir: undefined - returns -1
 *
 */
int PINT_remove_base_dir(char *pathname, char *out_dir, int out_max_len)
{
    int ret = -1, len = 0;
    char *start, *end, *end_ref;

    if (pathname && out_dir && out_max_len)
    {
        if ((strcmp(pathname,"/") == 0) || (pathname[0] != '/'))
        {
            return ret;
        }

        start = pathname;
        end = (char *)(pathname + strlen(pathname));
        end_ref = end;

        while(end && (end > start) && (*(--end) != '/'));

        len = (int)((char *)(end_ref - ++end));
        if (len < out_max_len)
        {
            memcpy(out_dir,end,len);
            out_dir[len] = '\0';
            ret = 0;
        }
    }
    return ret;
}

/* PINT_string_next_segment()
 *
 * Parameters:
 * pathname   - pointer to string
 * inout_segp - address of pointer; NULL to get first segment,
 *              pointer to last segment to get next segment
 * opaquep    - address of void *, used to maintain state outside
 *              the function
 *
 * Returns 0 if segment is available, -1 on end of string.
 *
 * This approach is nice because it keeps all the necessary state
 * outside the function and concisely stores it in two pointers.
 *
 * Internals:
 * We're using opaquep to store the location of where we placed a
 * '\0' separator to get a segment.  The value is undefined when
 * called with *inout_segp == NULL.  Afterwards, if we place a '\0',
 * *opaquep will point to where we placed it.  If we do not, then we
 * know that we've hit the end of the path string before we even
 * start processing.
 *
 * Note that it is possible that *opaquep != NULL and still there are
 * no more segments; a trailing '/' could cause this, for example.
 */
int PINT_string_next_segment(char *pathname,
                             char **inout_segp,
                             void **opaquep)
{
    char *ptr = (char *)0;

    /* initialize our starting position */
    if (*inout_segp == NULL) {
	ptr = pathname;
    }
    else if (*opaquep != NULL) {
	/* replace the '/', point just past it */
	ptr = (char *) *opaquep;
	*ptr = '/';
	ptr++;
    }
    else return -1; /* NULL *opaquep indicates last segment returned last time */

    /* at this point, the string is back in its original state */

    /* jump past separators */
    while ((*ptr != '\0') && (*ptr == '/')) ptr++;
    if (*ptr == '\0') return -1; /* all that was left was trailing '/'s */

    *inout_segp = ptr;

    /* find next separator */
    while ((*ptr != '\0') && (*ptr != '/')) ptr++;
    if (*ptr == '\0') *opaquep = NULL; /* indicate last segment */
    else {
	/* terminate segment and save position of terminator */
	*ptr = '\0';
	*opaquep = ptr;
    }
    return 0;
}

/* PINT_remove_dir_prefix()
 *
 * Strips prefix directory out of the path, output includes beginning
 * slash
 *
 * Parameters:
 * pathname     - pointer to directory string (absolute)
 * prefix       - pointer to prefix dir string (absolute)
 * out_path     - pointer to output dir string
 * max_out_len  - max length of out_base_dir buffer
 *
 * All incoming arguments must be valid and non-zero
 *
 * Returns 0 on success; -errno on failure
 *
 * Example inputs and outputs/return values:
 *
 * pathname: /mnt/pvfs2/foo, prefix: /mnt/pvfs2
 *     out_path: /foo, returns 0
 * pathname: /mnt/pvfs2/foo, prefix: /mnt/pvfs2/
 *     out_path: /foo, returns 0
 * pathname: /mnt/pvfs2/foo/bar, prefix: /mnt/pvfs2
 *     out_path: /foo/bar, returns 0
 * pathname: /mnt/pvfs2/foo/bar, prefix: /
 *     out_path: /mnt/pvfs2/foo/bar, returns 0
 *
 * invalid pathname input examples:
 * pathname: /mnt/foo/bar, prefix: /mnt/pvfs2
 *     out_path: undefined, returns -ENOENT
 * pathname: /mnt/pvfs2fake/foo/bar, prefix: /mnt/pvfs2
 *     out_path: undefined, returns -ENOENT
 * pathname: /mnt/foo/bar, prefix: mnt/pvfs2
 *     out_path: undefined, returns -EINVAL
 * pathname: mnt/foo/bar, prefix: /mnt/pvfs2
 *     out_path: undefined, returns -EINVAL
 * out_max_len not large enough for buffer, returns -ENAMETOOLONG
 */
int PINT_remove_dir_prefix(char *pathname, char* prefix, char *out_path, 
    int out_max_len)
{
    int ret = -EINVAL;
    int prefix_len, pathname_len;
    int cut_index;

    if(!pathname || !prefix || !out_path || !out_max_len)
    {
	return(-EINVAL);
    }

    /* make sure we are given absolute paths */
    if ((pathname[0] != '/') || (prefix[0] != '/'))
    {
	return ret;
    }

    prefix_len = strlen(prefix);
    pathname_len = strlen(pathname);

    /* account for trailing slashes on prefix */
    while(prefix[prefix_len-1] == '/')
    {
	prefix_len--;
    }

    /* if prefix_len is now zero, then prefix must have been root
     * directory; return copy of entire pathname
     */
    if(prefix_len == 0)
    {
	cut_index = 0;
    }
    else
    {
	
	/* make sure prefix would fit in pathname */
	if(prefix_len > (pathname_len + 1))
	    return(-ENOENT);

	/* see if we can find prefix at beginning of path */
	if(strncmp(prefix, pathname, prefix_len) == 0)
	{
	    /* apparent match; see if next element is a slash */
	    if(pathname[prefix_len] != '/')
		return(-ENOENT);
	    
	    /* this was indeed a match */
	    cut_index = prefix_len;
	}
	else
	{
	    return(-ENOENT);
	}
    }

    /* if we hit this point, then we were successful */

    /* is the buffer large enough? */
    if((1+strlen(&(pathname[cut_index]))) > out_max_len)
	return(-ENAMETOOLONG);

    /* copy out appropriate part of pathname */
    strcpy(out_path, &(pathname[cut_index]));
    return(0);
}

/*
 * PINT_parse_handle_ranges:  the first time this is called, set 'status' to
 * zero.  get back the first range in the string in the 'first' and 'last'
 * variables.  keep calling PINT_parse_handle_ranges until it returns zero
 *
 * range:   string representing our ranges
 * first:   (output) beginning of range
 * last:    (output) end of range
 * status:  (opaque) how far we are in the range string 
 *
 * returns:
 *  0: no more ranges
 *  1: found a range.  look at 'first' and 'last' for the values
 *  -1: something bad happened
 */
int PINT_parse_handle_ranges(char *range, int *first, int *last, int *status)
{ 
    /* what started out as a "hm, maybe i can use strtoul to help parse this"
     * turned into a lot harier parser than i had hoped. */
    char *p, *endchar; 

    p = range + *status;

    /* from strtol(3):
       If endptr is not NULL, strtoul() stores the address of the
       first invalid character in *endptr.  If there were no dig­
       its at all, strtoul() stores the original value of nptr in
       *endptr  (and  returns 0).  In particular, if *nptr is not
       `\0' but **endptr is `\0' on return, the entire string  is
       valid.  */ 
    *first = *last = (int) strtoul(p, &endchar, 0);
    if ( p == endchar )  /* all done */
	return 0; 
    /* strtoul eats leading space, but not trailing space.  take care of ws
     * between number and delimiter (- or ,) */
    while (isspace(*endchar)) endchar++; 
    
    p = endchar+1; /* skip over the ',' or '-'*/

    switch (*endchar) {
	case '-': /* we got the first half of the range. grab 2nd half */
	    *last = (int)strtoul(p, &endchar, 0);
	    /* again, skip trailing space ...*/
	    while (isspace(*endchar)) endchar++;
	    /* ... and the delimiter */ 
	    if (*endchar == ',') endchar ++;
	    /* 'status' tells us how far we are in the string */
	    *status = ( endchar - range);
	    break; 
	case ',': /* end of a range */
	case '\0': /* end of the whole string */
	    *status = ( p - range );
	    break;
	default:
	    printf("illegal characher %c\n", *endchar);
	    return -1;
    }
    return 1;
}

/*
 * PINT_get_path_element:  gets the specified segment in the
 * provided path.
 *
 * pathname   :  string containing a valid pathname
 * segment_num:  the desired segment number in the path
 * out_segment:  where the segment will be stored on success
 * out_max_len:  max num bytes to store in out_segment
 *
 * returns:
 *  0 : if the segment was found and copied
 *  -1: if an invalid segment was specified
 *
 * Example inputs and outputs/return values:
 *
 * pathname: /mnt/pvfs2/foo, segment_num: 0
 *     out_segment: mnt, returns 0
 * pathname: /mnt/pvfs2/foo, segment_num: 2
 *     out_segment: foo, returns 0
 * pathname: /mnt/pvfs2/foo, segment_num: 5
 *     out_segment: undefined, returns -1
 */
int PINT_get_path_element(
    char *pathname,
    int segment_num,
    char *out_segment,
    int out_max_len)
{
    int count = -1;
    char *segp = (char *)0;
    void *segstate;
    char local_pathname[MAX_PATH_LEN] = {0};

    strncpy(local_pathname,pathname,MAX_PATH_LEN);

    while(!PINT_string_next_segment(local_pathname,&segp,&segstate))
    {
        if (++count == segment_num)
        {
            strncpy(out_segment,segp,(size_t)out_max_len);
            break;
        }
    }
    return ((count == segment_num) ? 0 : -1);
}

/* get_next_path
 *
 * gets remaining path given number of path segments to skip
 *
 * returns 0 on success, -errno on failure
 */
int get_next_path(char *path, char **newpath, int skip)
{
    int pathlen=0, i=0, num_slashes_seen=0;
    int delimiter1=0;

    pathlen = strlen(path) + 1;

    /* find our starting point in the old path, it could be past multiple 
     * segments*/
    for(i =0; i < pathlen; i++)
    {
	if (path[i] == '/')
	{
	    num_slashes_seen++;
	    if (num_slashes_seen > skip)
	    {
		break;
	    }
	}
    }

    delimiter1 = i;
    if (pathlen - delimiter1 < 1)
    {
        return (-EINVAL);
    }

    *newpath = malloc(pathlen - delimiter1);
    if (*newpath == NULL)
    {
        return (-ENOMEM);
    }
    memcpy(*newpath, &path[delimiter1], pathlen - delimiter1 );
    /* *newpath[pathlen - delimiter1 -1 ] = '\0';*/
    return(0);
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 noexpandtab
 */



/* 
 * (C) 2001 Clemson University and The University of Chicago 
 *
 * See COPYING in top-level directory.
 */

#include <stdio.h>
#include <string.h>

/* PINT_string_count_segments()
 *
 * Count number of segments in a path.
 *
 * Parameters:
 * pathname   - pointer to string
 *
 * Returns number of segments in pathname; -1 if
 * pathname does not begin with a slash or is invalid
 *
 * Example inputs and return values:
 *
 * filename          - returns -1
 * NULL              - returns -1
 * /                 - returns  0
 * /filename         - returns  1
 * /filename/        - returns  1
 * /filename//       - returns  1
 * /dirname/filename - returns  2
 *
 * NOTE (known limitation):
 * //foo             - returns  2
 *
 */
int PINT_string_count_segments(char *pathname)
{
    int segct = 0;
    char *cur_ch = pathname;

    /* insist on a valid and an absolute path */
    if (!cur_ch || (*cur_ch != '/')) return -1;

    while(cur_ch && *cur_ch)
    {
        if (*cur_ch == '/')
        {
            segct++;
        }
        cur_ch++;
    }

    /* ignore trailing slash(es) if any */
    while ((cur_ch > pathname) && (*(--cur_ch) == '/'))
    {
        segct--;
    }
    return segct;
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
    char *ptr;

    /* insist on an absolute path */
    if (pathname[0] != '/') return -1;

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


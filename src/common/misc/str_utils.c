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
 * TODO: CLEAN UP!
 */
int PINT_string_count_segments(char *pathname)
{
    int pos, nullpos = 0, killed_slash, segct = 0;

    /* insist on an absolute path */
    if (pathname[0] != '/') return -1;
    
    for(;;) {
	pos = nullpos;
	killed_slash = 0;

	/* jump to next separator */
	while ((pathname[nullpos] != '\0') && (pathname[nullpos] != '/')) nullpos++;

	if (nullpos == pos) {
	    /* extra slash or trailing slash; ignore */
	}
	else {
	    segct++; /* found another real segment */
	}
	if (pathname[nullpos] == '\0') break;
	nullpos++;
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


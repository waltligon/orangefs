/* Error handler for noninteractive utilities
   Copyright (C) 1990-2012 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

/* Written by David MacKenzie <djm@gnu.ai.mit.edu>.  */

/* modifed for OrangeFS (PVFSv3) to a much simplified form
 * WBL 4/2013
 */

#define USRINT_SOURCE 1
#include "usrint.h"

#define __REDIRECT(n,p,a)

#include <error.h>

static void error_tail (int status,
                        int errnum,
                        const char *message,
                        va_list args)
{
    vfprintf (stderr, message, args);
    va_end (args);

    ++error_message_count;
    if (errnum)
    {
        char const *s;
        s = strerror (errnum);
      
        if (! s)
        {
            fprintf (stderr, ": Unknown system error\n");
        }
        else
        {
            fprintf (stderr, ": %s\n", s);
        }
    }
    fflush (stderr);
    if (status)
    {
        exit (status);
    }
}

/* Print the program name and error message MESSAGE, which is a printf-style
   format string with optional args.
   If ERRNUM is nonzero, print its corresponding system error message.
   Exit with status STATUS if it is nonzero.  */
void error (int status, int errnum, const char *message, ...)
{
    va_list args;

    fflush (stdout);
    if (error_print_progname)
    {
        (*error_print_progname) ();
    }
    else
    {
        fprintf (stderr, "%s: ", program_invocation_name);
    }

    va_start (args, message);
    error_tail (status, errnum, message, args);
    va_end (args);
}

void error_at_line (int status,
                    int errnum,
                    const char *file_name,
	                unsigned int line_number,
                    const char *message,
                    ...)
{
    va_list args;

    if (error_one_per_line)
    {
        static const char *old_file_name;
        static unsigned int old_line_number;
  
        if (old_line_number == line_number
	        && (file_name == old_file_name
	            || (old_file_name != NULL
		        && file_name != NULL
		        && strcmp (old_file_name, file_name) == 0)))
        {
	        /* Simply return and print nothing.  */
	        return;
        }

        old_file_name = file_name;
        old_line_number = line_number;
    }

    fflush (stdout);
    if (error_print_progname)
    {
        (*error_print_progname) ();
    }
    else
    {
        fprintf (stderr, "%s:", program_invocation_name);
    }

    fprintf (stderr,
             file_name != NULL ? "%s:%d: " : " ",
	         file_name,
             line_number);

    va_start (args, message);
    error_tail (status, errnum, message, args);
    va_end (args);
}


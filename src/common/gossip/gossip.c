/*
 * copyright (c) 2000 Clemson University, all rights reserved.
 *
 * Written by Phil Carns.
 *
 * This program is free software; you can redistribute it and/or
 * modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Contacts:  Phil Carns  pcarns@parl.clemson.edu
 */

/*
 * April 2001
 *
 * This is a basic application logging facility.  It uses printf style
 * formatting and provides several mechanisms for output. 
 */ 

#include <stdio.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <syslog.h>

#include <gossip.h>

/* controls whether debugging is on or off */
int gossip_debug_on = 0;

/* controls the mask level for debugging messages */
int gossip_debug_mask = 0;

enum
{
	GOSSIP_STDERR = 1,
	GOSSIP_FILE = 2,
	GOSSIP_SYSLOG = 4
};

enum
{
	GOSSIP_BUF_SIZE = 128
};

/* determines which logging facility to use */
int gossip_facility = 0;

/* file handle used for file logging */
static FILE* internal_log_file = NULL;

/* syslog priority setting */
static int internal_syslog_priority = LOG_USER;

/*****************************************************************
 * prototypes
 */

static int gossip_debug_stderr(const char* format, va_list ap);
static int gossip_err_stderr(const char* format, va_list ap);
static int gossip_disable_stderr(void);

static int gossip_debug_file(const char* format, va_list ap);
static int gossip_err_file(const char* format, va_list ap);
static int gossip_disable_file(void);

static int gossip_debug_syslog(const char* format, va_list ap);
static int gossip_err_syslog(const char* format, va_list ap);
static int gossip_disable_syslog(void);


/*****************************************************************
 * visible functions
 */

/* gossip_enable_syslog()
 * 
 * Turns on syslog logging facility.  The priority argument is a
 * combination of the facility and level to use, as seen in the
 * syslog(3) man page.
 *
 * returns 0 on success, -errno on failure
 */
int gossip_enable_syslog(int priority)
{
	
	/* keep up with the existing logging settings */
	int tmp_debug_on = gossip_debug_on;
	int tmp_debug_mask = gossip_debug_mask;

	/* turn off any running facility */
	gossip_disable();

	internal_syslog_priority = priority;
	gossip_facility = GOSSIP_SYSLOG;

	/* restore the logging settings */
	gossip_debug_on = tmp_debug_on;
	gossip_debug_mask = tmp_debug_mask;
		
	return(0);
}

/* gossip_enable_stderr()
 *
 * Turns on logging to stderr.
 *
 * returns 0 on success, -errno on failure
 */
int gossip_enable_stderr(void)
{

	/* keep up with the existing logging settings */
	int tmp_debug_on = gossip_debug_on;
	int tmp_debug_mask = gossip_debug_mask;

	/* turn off any running facility */
	gossip_disable();

	gossip_facility = GOSSIP_STDERR;

	/* restore the logging settings */
	gossip_debug_on = tmp_debug_on;
	gossip_debug_mask = tmp_debug_mask;
		
	return(0);
}

/* gossip_enable_file()
 * 
 * Turns on logging to a file.  The filename argument indicates which
 * file to use for logging messages, and the mode indicates whether the
 * file should be truncated or appended (see fopen() man page).
 *
 * returns 0 on success, -errno on failure
 */
int gossip_enable_file(const char* filename, const char* mode)
{

	/* keep up with the existing logging settings */
	int tmp_debug_on = gossip_debug_on;
	int tmp_debug_mask = gossip_debug_mask;

	/* turn off any running facility */
	gossip_disable();

	internal_log_file = fopen(filename, mode);
	if(!internal_log_file)
	{
		return(-errno);
	}

	gossip_facility = GOSSIP_FILE;

	/* restore the logging settings */
	gossip_debug_on = tmp_debug_on;
	gossip_debug_mask = tmp_debug_mask;
		
	return(0);

}

/* gossip_disable()
 * 
 * Turns off any active logging facility and disables debugging.
 *
 * returns 0 on success, -errno on failure
 */
int gossip_disable(void)
{
	int ret = -EINVAL;
	
	switch(gossip_facility)
	{
		case GOSSIP_STDERR:
			ret = gossip_disable_stderr();
			break;
		case GOSSIP_FILE:
			ret = gossip_disable_file();
			break;
		case GOSSIP_SYSLOG:
			ret = gossip_disable_syslog();
			break;
		default:
			break;
	}

	gossip_debug_on = 0;
	gossip_debug_mask = 0;
	
	return ret;
}

/* gossip_set_debug_mask()
 *
 * Determines whether debugging messages are turned on or off.  Also
 * specifies the mask that determines which debugging messages are
 * printed.
 *
 * returns 0 on success, -errno on failure
 */
int gossip_set_debug_mask(int debug_on, int mask)
{
	if((debug_on != 0)&&(debug_on != 1))
	{
		return(-EINVAL);
	}

	if(mask < 0)
	{
		return(-EINVAL);
	}

	gossip_debug_on = debug_on;
	gossip_debug_mask = mask;
	return(0);
}

/* __gossip_debug_stub()
 * 
 * stub for gossip_debug that doesn't do anything; used when debugging
 * is "compiled out" on non-gcc builds
 *
 * returns 0
 */
int __gossip_debug_stub(int mask, const char* format, ...)
{
	return(0);
}


/* __gossip_debug()
 * 
 * Logs a standard debugging message.  It will not be printed unless the
 * mask value matches (logical "and" operation) with the mask specified in
 * gossip_set_debug_mask() and debugging is turned on.
 *
 * returns 0 on success, -errno on failure
 */
int __gossip_debug(int mask, const char* format, ...)
{
	va_list ap;
	int ret = -EINVAL;

/* NOTE: this check happens in the macro (before making a function call)
 * if we use gcc 
 */
#ifndef __GNUC__
	/* exit quietly if we aren't meant to print */
	if((!gossip_debug_on) || !(gossip_debug_mask & mask) ||
		(!gossip_facility))
	{
		return(0);
	}
#endif 

	/* rip out the variable arguments */
	va_start(ap, format);

	switch(gossip_facility)
	{
		case GOSSIP_STDERR:
			ret = gossip_debug_stderr(format, ap);
			break;
		case GOSSIP_FILE:
			ret = gossip_debug_file(format, ap);
			break;
		case GOSSIP_SYSLOG:
			ret = gossip_debug_syslog(format, ap);
			break;
		default:
			break;
	}

	va_end(ap);

	return(ret);
}

/* gossip_err()
 * 
 * Logs a critical error message.  This will print regardless of the
 * mask value and whether debugging is turned on or off, as long as some
 * logging facility has been enabled.
 *
 * returns 0 on success, -errno on failure
 */
int gossip_err(const char* format, ...)
{
	va_list ap;
	int ret = -EINVAL;

	if(!gossip_facility)
	{
		return(0);
	}

	/* rip out the variable arguments */
	va_start(ap, format);

	switch(gossip_facility)
	{
		case GOSSIP_STDERR:
			ret = gossip_err_stderr(format, ap);
			break;
		case GOSSIP_FILE:
			ret = gossip_err_file(format, ap);
			break;
		case GOSSIP_SYSLOG:
			ret = gossip_err_syslog(format, ap);
			break;
		default:
			break;
	}

	va_end(ap);

	return(ret);
}

/****************************************************************
 * Internal functions
 */

/* gossip_debug_syslog()
 * 
 * This is the standard debugging message function for the syslog logging
 * facility
 *
 * returns 0 on success, -errno on failure
 */
static int gossip_debug_syslog(const char* format, va_list ap)
{
	char buffer[GOSSIP_BUF_SIZE];
	int ret = -EINVAL;

	ret = vsnprintf(buffer, GOSSIP_BUF_SIZE, format, ap);
	if(ret < 0)
	{
		return(-errno);
	}

	syslog(internal_syslog_priority, buffer);

	return(0);
}


/* gossip_debug_file()
 * 
 * This is the standard debugging message function for the file logging
 * facility
 *
 * returns 0 on success, -errno on failure
 */
static int gossip_debug_file(const char* format, va_list ap)
{
	char buffer[GOSSIP_BUF_SIZE];
	int ret = -EINVAL;

	ret = vsnprintf(buffer, GOSSIP_BUF_SIZE, format, ap);
	if(ret < 0)
	{
		return(-errno);
	}

	ret = fprintf(internal_log_file, buffer);
	if(ret < 0)
	{
		return(-errno);
	}
	fflush(internal_log_file);

	return(0);
}

/* gossip_debug_stderr()
 * 
 * This is the standard debugging message function for the stderr
 * facility.
 *
 * returns 0 on success, -errno on failure
 */
static int gossip_debug_stderr(const char* format, va_list ap)
{

	int ret = vfprintf(stderr, format, ap);
	if (ret < 0)
	{
		return(-errno);
	}
	return(0);
}

/* gossip_err_syslog()
 * 
 * error message function for the syslog logging facility
 *
 * returns 0 on success, -errno on failure
 */
static int gossip_err_syslog(const char* format, va_list ap)
{
	/* for syslog we have the opportunity to change the priority level
	 * for errors
	 */
	int tmp_priority = internal_syslog_priority;
	internal_syslog_priority = LOG_ERR;

	gossip_debug_syslog(format, ap);

	internal_syslog_priority = tmp_priority;

	return(0);
}


/* gossip_err_file()
 * 
 * error message function for the file logging facility
 *
 * returns 0 on success, -errno on failure
 */
static int gossip_err_file(const char* format, va_list ap)
{
	/* we don't do anything special with errors here */
	return(gossip_debug_file(format, ap));
}


/* gossip_err_stderr()
 * 
 * This is the error message function for the stderr facility.
 *
 * returns 0 on success, -errno on failure
 */
static int gossip_err_stderr(const char* format, va_list ap)
{
	/* we don't do anything special for errors here */
	return(gossip_debug_stderr(format, ap));
}


/* gossip_disable_stderr()
 * 
 * The shutdown function for the stderr logging facility.
 *
 * returns 0 on success, -errno on failure
 */
static int gossip_disable_stderr(void)
{
	/* this function doesn't need to do anything... */
	return(0);
}

/* gossip_disable_file()
 * 
 * The shutdown function for the file logging facility.
 *
 * returns 0 on success, -errno on failure
 */
static int gossip_disable_file(void)
{
	fclose(internal_log_file);
	return(0);
}

/* gossip_disable_syslog()
 * 
 * The shutdown function for the syslog logging facility.
 *
 * returns 0 on success, -errno on failure
 */
static int gossip_disable_syslog(void)
{
	closelog();
	return(0);
}

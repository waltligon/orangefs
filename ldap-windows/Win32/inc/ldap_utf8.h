/* $Novell: ldap_utf8.h,v 1.15 2003/05/13 13:54:13 $ */
/*
 * Copyright 1998,1999 The OpenLDAP Foundation, Redwood City, California, USA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted only
 * as authorized by the OpenLDAP Public License.  A copy of this
 * license is available at http://www.OpenLDAP.org/license.html or
 * in file LICENSE in the top-level directory of the distribution.
 */
/* Portions
 * Copyright (c) 1990 Regents of the University of Michigan.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of Michigan at Ann Arbor. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 ******************************************************************************
 * This notice applies to changes, created by or for Novell, Inc.,
 * to preexisting works for which notices appear elsewhere in this file.
 *
 *  Novell Software Developer Kit
 *
 *  Copyright (C) 2002-2003 Novell, Inc. All Rights Reserved.
 *
 *  THIS WORK IS SUBJECT TO U.S. AND INTERNATIONAL COPYRIGHT LAWS AND TREATIES.
 *  USE AND REDISTRIBUTION OF THIS WORK IS SUBJECT TO THE LICENSE AGREEMENT
 *  ACCOMPANYING THE SOFTWARE DEVELOPER KIT (SDK) THAT CONTAINS THIS WORK.
 *  PURSUANT TO THE SDK LICENSE AGREEMENT, NOVELL HEREBY GRANTS TO DEVELOPER A
 *  ROYALTY-FREE, NON-EXCLUSIVE LICENSE TO INCLUDE NOVELL'S SAMPLE CODE IN ITS
 *  PRODUCT. NOVELL GRANTS DEVELOPER WORLDWIDE DISTRIBUTION RIGHTS TO MARKET,
 *  DISTRIBUTE, OR SELL NOVELL'S SAMPLE CODE AS A COMPONENT OF DEVELOPER'S
 *  PRODUCTS. NOVELL SHALL HAVE NO OBLIGATIONS TO DEVELOPER OR DEVELOPER'S
 *  CUSTOMERS WITH RESPECT TO THIS CODE.
 ******************************************************************************/

#ifndef _LDAP_UTF8_H
#define _LDAP_UTF8_H

#include <ldap.h>

LDAP_BEGIN_DECL

/*  
 * UTF-8 Utility Routines (in utf-8.c)
 */

#define LDAP_UCS4_INVALID (0x80000000U)

/* LDAP_MAX_UTF8_LEN is 3 or 6 depending on size of wchar_t */
#define LDAP_MAX_UTF8_LEN  sizeof(wchar_t)*3/2

/* returns the number of UTF-8 characters in the string */
LDAP_F (ber_len_t) ldap_x_utf8_chars( const char * );
/* returns the length (in bytes) indicated by the UTF-8 character */
LDAP_F (int) ldap_x_utf8_charlen( const char * );
/* returns the length (in bytes) indicated by the UTF-8 character - takes care of illegal UTF8 encodings */
LDAP_F (int) ldap_x_utf8_charlen2( const char * );
/* copies a UTF-8 character and returning number of bytes copied */
LDAP_F (int) ldap_x_utf8_copy( char *, const char * );
/* returns pointer of next UTF-8 character in string */
LDAP_F (char*) ldap_x_utf8_next( const char * );
/* returns pointer of previous UTF-8 character in string */
LDAP_F (char*) ldap_x_utf8_prev( const char * );

/* span characters not in set, return bytes spanned */
LDAP_F (ber_len_t) ldap_x_utf8_strcspn( const char* str, const char *set);
/* span characters in set, return bytes spanned */
LDAP_F (ber_len_t) ldap_x_utf8_strspn( const char* str, const char *set);
/* return first occurance of character in string */
LDAP_F (char *) ldap_x_utf8_strchr( const char* str, const char *chr);
/* return first character of set in string */
LDAP_F (char *) ldap_x_utf8_strpbrk( const char* str, const char *set);
/* reentrant tokenizer */
LDAP_F (char*) ldap_x_utf8_strtok( char* sp, const char* sep, char **last);

/* Optimizations */
#define LDAP_X_UTF8_ISASCII(p) ( * (const unsigned char *) (p) < 0x80 )

#define LDAP_X_UTF8_CHARLEN(p) ( LDAP_X_UTF8_ISASCII(p) \
	? 1 : ldap_x_utf8_charlen((p)) )

#define LDAP_X_UTF8_CHARLEN2(p) ( LDAP_X_UTF8_ISASCII(p) \
        ? 1 : ldap_x_utf8_charlen2((p)) )

#define LDAP_X_UTF8_COPY(d,s) (	LDAP_X_UTF8_ISASCII(s) \
	? (*(d) = *(s), 1) : ldap_x_utf8_copy((d),(s)) )

#define LDAP_X_UTF8_NEXT(p) (	LDAP_X_UTF8_ISASCII(p) \
	? (char *)(p)+1 : ldap_x_utf8_next((p)) )

#define LDAP_X_UTF8_INCR(p) ((p) = LDAP_X_UTF8_NEXT(p))

/* For symmetry */
#define LDAP_X_UTF8_PREV(p) (ldap_x_utf8_prev((p)))
#define LDAP_X_UTF8_DECR(p) ((p)=LDAP_X_UTF8_PREV((p)))



/*
 * UTF-8 Conversion Routines.   (in utfconv.c)
 */

/* UTF-8 character to Wide Char */
LDAP_F(int)
ldap_x_utf8_to_wc ( wchar_t *wchar, const char *utf8char );

/* UTF-8 string to Wide Char string */
LDAP_F(int)
ldap_x_utf8s_to_wcs ( wchar_t *wcstr, const char *utf8str, size_t count );

/* Wide Char to UTF-8 character */
LDAP_F(int)
ldap_x_wc_to_utf8 ( char *utf8char, wchar_t wchar, size_t count );

/* Wide Char string to UTF-8 string */
LDAP_F(int)
ldap_x_wcs_to_utf8s ( char *utf8str, const wchar_t *wcstr, size_t count );

LDAP_END_DECL

#endif /* _LDAP_UTF8_H */


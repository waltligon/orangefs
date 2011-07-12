/* $OpenLDAP: pkg/ldap/include/lber_types.nt,v 1.2.2.2 2000/06/13 17:57:14 kurt Exp $ */
/* $Novell: lber_types.win,v 1.1 2002/10/17 15:57:45 $ */
/*
 * Copyright 1998-2000 The OpenLDAP Foundation, Redwood City, California, USA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted only
 * as authorized by the OpenLDAP Public License.  A copy of this
 * license is available at http://www.OpenLDAP.org/license.html or
 * in file LICENSE in the top-level directory of the distribution.
 */

/*
 * LBER types for Win64
 */

#ifndef _LBER_TYPES_H
#define _LBER_TYPES_H

#include <winsock.h>
#include <ldap_cdefs.h>

LDAP_BEGIN_DECL

/* LBER boolean, enum, integers - 32 bits or larger*/
#define LBER_INT_T	int

/* LBER tags - 32 bits or larger */
#define LBER_TAG_T	long

/* LBER socket descriptor (WinSock SOCKET type)*/
#define LBER_SOCKET_T	SOCKET

   /* Note the Win64 compiler defines both _WIN64 & _WIN32 so put the 
    * Win64 stuff first
   */
/* LBER lengths - 32 bits or larger*/
#if defined(_WIN64)
#define LBER_LEN_T		unsigned __int64
#define LBER_SLEN_T		__int64

#else /* _Win32 */
#define LBER_LEN_T		unsigned long
#define LBER_SLEN_T		long 
#endif

/* ------------------------------------------------------------ */

/* booleans, enumerations, and integers */
typedef LBER_INT_T ber_int_t;

/* signed and unsigned versions */
typedef signed LBER_INT_T ber_sint_t;
typedef unsigned LBER_INT_T ber_uint_t;

/* tags */
typedef unsigned LBER_TAG_T ber_tag_t;

/* "socket" descriptors */
typedef LBER_SOCKET_T ber_socket_t;

/* lengths */
typedef LBER_LEN_T ber_len_t;

/* signed lengths */
typedef LBER_SLEN_T ber_slen_t;

LDAP_END_DECL

#endif /* _LBER_TYPES_H */

/* $OpenLDAP: pkg/ldap/include/ldap_features.nt,v 1.2.2.2 2000/06/13 17:57:15 kurt Exp $ */
/* $Novell: ldap_features.win,v 1.1 2002/10/17 15:57:44 $ */
/*
 * Copyright 1998-2000 The OpenLDAP Foundation, Redwood City, California, USA
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted only
 * as authorized by the OpenLDAP Public License.  A copy of this
 * license is available at http://www.OpenLDAP.org/license.html or
 * in file LICENSE in the top-level directory of the distribution.
 */

/******************************************************************************
 * This notice applies to changes, created by or for Novell, Inc.,
 * to preexisting works for which notices appear elsewhere in this file.
 *
 * Copyright (C) 1999, 2000, 2002 Novell, Inc. All Rights Reserved.
 *
 * THIS WORK IS SUBJECT TO U.S. AND INTERNATIONAL COPYRIGHT LAWS AND TREATIES.
 * USE, MODIFICATION, AND REDISTRIBUTION OF THIS WORK IS SUBJECT TO VERSION
 * 2.0.1 OF THE OPENLDAP PUBLIC LICENSE, A COPY OF WHICH IS AVAILABLE AT
 * HTTP://WWW.OPENLDAP.ORG/LICENSE.HTML OR IN THE FILE "LICENSE" IN THE
 * TOP-LEVEL DIRECTORY OF THE DISTRIBUTION. ANY USE OR EXPLOITATION OF THIS
 * WORK OTHER THAN AS AUTHORIZED IN VERSION 2.0.1 OF THE OPENLDAP PUBLIC
 * LICENSE, OR OTHER PRIOR WRITTEN CONSENT FROM NOVELL, COULD SUBJECT THE
 * PERPETRATOR TO CRIMINAL AND CIVIL LIABILITY. 
 ******************************************************************************/

/* 
 * LDAP Features
 */
#ifndef _LDAP_FEATURES_H
#define _LDAP_FEATURES_H 1

/*
** OpenLDAP reentrancy/thread-safeness should be dynamically
** checked using ldap_get_option().
**
** The -lldap implementation may or may not be:
**		LDAP_API_FEATURE_THREAD_SAFE
**
** The preprocessor flag LDAP_API_FEATURE_X_OPENLDAP_REENTRANT can
** be used to determine if -lldap is LDAP_API_FEATURE_THREAD_SAFE at
** compile time.
**
** The -lldap_r implementation is always THREAD_SAFE but
** may also be:
**		LDAP_API_FEATURE_SESSION_THREAD_SAFE
**		LDAP_API_FEATURE_OPERATION_THREAD_SAFE
**
** The preprocessor flag LDAP_API_FEATURE_X_OPENLDAP_THREAD_SAFE
** can be used to determine if -lldap_r is available at compile
** time.  You must define LDAP_THREAD_SAFE if and only if you
** link with -lldap_r.
**
** If you fail to define LDAP_THREAD_SAFE when linking with
** -lldap_r or define LDAP_THREAD_SAFE when linking with -lldap,
** provided header definations and declarations may be incorrect.
**
*/

/* is -lldap reentrant or not */
#undef LDAP_API_FEATURE_X_OPENLDAP_REENTRANT

/* is threadsafe version of -lldap (ie: -lldap_r) *available* or not */
#define LDAP_API_FEATURE_X_OPENLDAP_THREAD_SAFE 1000

/* LDAP v2 Kerberos Bind */
#undef LDAP_API_FEATURE_X_OPENLDAP_V2_KBIND

/* LDAP v2 Referrals */
#undef LDAP_API_FEATURE_X_OPENLDAP_V2_REFERRALS

/* LDAP Server Side Sort. */
#define LDAP_API_FEATURE_SERVER_SIDE_SORT 1000

/* LDAP Virtual List View. Version = 1000 + draft revision.
 * VLV requires Server Side Sort control.
 */
#define LDAP_API_FEATURE_VIRTUAL_LIST_VIEW 1000

/* UTF-8 to WideChar conversion functions */ 
#define LDAP_API_FEATURE_X_OPENLDAP_UTF8_CONVERSION 1001

/* Schema Util functions */
#define LDAP_API_FEATURE_SCHEMA_UTIL	1000

#endif /* LDAP_FEATURES */

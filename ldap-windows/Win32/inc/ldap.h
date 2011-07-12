/* $OpenLDAP: pkg/ldap/include/ldap.h,v 1.90 1999/12/18 18:50:38 kdz Exp $ */
/* $Novell: ldap.h,v 1.64 2003/05/13 13:46:48 $ */
/* $Novell: ldap.h,v 1.64 2003/05/13 13:46:48 $ */
/*
 * Copyright 1998-2000 The OpenLDAP Foundation, Redwood City, California, USA
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
 ******************************************************************************
 * Modification to OpenLDAP source by Novell, Inc.
 * Jan 13, 2000   sfs
 * Add LIBCALL to function table to specify calling convention
 * 
 * Jan 14, 2000  dks  Remove prototypes of private functions.
 *
 * Jan 20, 2000  dks  Don't advertise experimental OpenLDAP features.
 *					  LDAP_API_FEATURE_X_OPENLDAP.
 * Apr 04, 2000  sfs  Change ldap_set_rebind_func prototype
 */

#ifndef _LDAP_H
#define _LDAP_H

/* Include the appropriate platform socket headers */
#if defined (N_PLAT_NLM)
   #include <stddef.h>
   #if defined(__NOVELL_LIBC__)
      #include <novsock2.h>
   #else
      #include <ws2nlm.h>
   #endif
#elif defined (_WIN64)
   #include <winsock.h>
#elif defined (_WIN32) 
#elif defined (MODESTO)
   #include <winsock.h>
#else
   #include <sys/time.h>
   #include <sys/types.h>
   #include <sys/socket.h>
#endif

#ifdef HAVE_POLL
#include <poll.h>
#endif

/* pull in lber */
#include <lber.h>

LDAP_BEGIN_DECL

#define LDAP_VERSION1	1
#define LDAP_VERSION2	2
#define LDAP_VERSION3	3

#define LDAP_VERSION_MIN	LDAP_VERSION2
#define	LDAP_VERSION		LDAP_VERSION2
#define LDAP_VERSION_MAX	LDAP_VERSION3

/*
 * We'll use 2000+draft revision for our API version number
 * As such, the number will be above the old RFC but below 
 * whatever number does finally get assigned
 */
#define LDAP_API_VERSION	2007
#define LDAP_VENDOR_NAME	"Novell"
#define LDAP_VENDOR_VERSION	353			/* 3.5.3 July 2009 NDK */

/* OpenLDAP API Features */
/* #define LDAP_API_FEATURE_X_OPENLDAP LDAP_VENDOR_VERSION */

/* include LDAP_API_FEATURE defines */
#include <ldap_features.h>

#if defined( LDAP_API_FEATURE_X_OPENLDAP_REENTRANT ) || \
	( defined( LDAP_THREAD_SAFE ) && \
		defined( LDAP_API_FEATURE_X_OPENLDAP_THREAD_SAFE ) )
	/* -lldap may or may not be thread safe */
	/* -lldap_r, if available, is always thread safe */
#	define	LDAP_API_FEATURE_THREAD_SAFE 1
#endif
#if defined( LDAP_THREAD_SAFE ) && \
	defined( LDAP_API_FEATURE_X_OPENLDAP_THREAD_SAFE )
#define LDAP_API_FEATURE_SESSION_SAFE	1
/* #define LDAP_API_OPERATION_SESSION_SAFE	1	*/
#endif

#define LDAP_PORT		389		/* ldap:///		default LDAP port */
#define LDAPS_PORT		636		/* ldaps:///	default LDAP over TLS port */

#define LDAP_ROOT_DSE				""
#define LDAP_NO_ATTRS				"1.1"
#define LDAP_ALL_USER_ATTRIBUTES	"*"
#define LDAP_ALL_OPERATIONAL_ATTRIBUTES	"+" /* OpenLDAP extension */

/*
 * LDAP_OPTions defined by draft-ldapext-ldap-c-api-02
 * 0x0000 - 0x0fff reserved for api options
 * 0x1000 - 0x3fff reserved for api extended options
 * 0x4000 - 0x7fff reserved for private and experimental options
 */
#define LDAP_OPT_API_INFO			0x0000
#define LDAP_OPT_DESC				0x0001 /* deprecated */
#define LDAP_OPT_DEREF				0x0002
#define LDAP_OPT_SIZELIMIT			0x0003
#define LDAP_OPT_TIMELIMIT			0x0004
/* 0x05 - 0x07 not defined by current draft */
#define LDAP_OPT_REFERRALS			0x0008
#define LDAP_OPT_RESTART			0x0009
/* 0x0a - 0x10 not defined by current draft */
#define LDAP_OPT_PROTOCOL_VERSION	0x0011
#define LDAP_OPT_SERVER_CONTROLS	0x0012
#define LDAP_OPT_CLIENT_CONTROLS	0x0013
/* 0x14 not defined by current draft */
#define LDAP_OPT_API_FEATURE_INFO	0x0015

/* 0x16 - 0x2f not defined by current draft */
#define LDAP_OPT_HOST_NAME			0x0030
#define	LDAP_OPT_RESULT_CODE		0x0031	/* Updated for C API draft 5 */
#define	LDAP_OPT_ERROR_NUMBER		0x0031
#define LDAP_OPT_ERROR_STRING		0x0032
#define LDAP_OPT_MATCHED_DN			0x0033
/* 0x34 - 0x0fff not defined by current draft */


/* private and experimental options */
/* OpenLDAP specific options */
#define LDAP_OPT_DEBUG_LEVEL		0x5001	/* debug level */
#define LDAP_OPT_TIMEOUT			0x5002	/* default timeout */
#define LDAP_OPT_REFHOPLIMIT		0x5003	/* ref hop limit */
#define LDAP_OPT_NETWORK_TIMEOUT    0x5005  /* socket level timeout */
#define LDAP_OPT_URI				0x5006
#define LDAP_OPT_REFERRAL_LIST      0x5007  /* Referrals from LDAP Result msg */

/* IO functions options */
#define LDAP_OPT_IO_FUNCS        	0x7001
#define LDAP_OPT_PEER_NAME        	0x7002
#define LDAP_OPT_CURRENT_NAME        	0x7003

/*thread safe */
#define LDAP_OPT_SESSION_REFCNT 0x8001

/* CIPHER related */
#define	LDAP_OPT_TLS_CIPHER_LEVEL       0x9001

/* Values for settting the CIPHER */
#define	LDAP_TLS_CIPHER_LOW             0x01
#define	LDAP_TLS_CIPHER_MEDIUM          0x02
#define	LDAP_TLS_CIPHER_HIGH            0x03
#define	LDAP_TLS_CIPHER_EXPORT          0x04

/* on/off values */
#define LDAP_OPT_ON		((void *) 1)
#define LDAP_OPT_OFF	((void *) 0)

/* OpenLDAP TLS options */
#define LDAP_OPT_X_TLS          0x6000
#define LDAP_OPT_X_TLS_HARD     1

/*
 * ldap_get_option() and ldap_set_option() return values.
 * As later versions may return other values indicating
 * failure, current applications should only compare returned
 * value against LDAP_OPT_SUCCESS.
 */
#define LDAP_OPT_SUCCESS	0
#define	LDAP_OPT_ERROR		(-1)

#define LDAP_API_INFO_VERSION	(1)
typedef struct ldapapiinfo {
	int		ldapai_info_version;		/* version of LDAPAPIInfo (1) */
	int		ldapai_api_version;			/* revision of API supported */
	int		ldapai_protocol_version;	/* highest LDAP version supported */
	char	**ldapai_extensions;		/* names of API extensions */
	char	*ldapai_vendor_name;		/* name of supplier */
	int		ldapai_vendor_version;		/* supplier-specific version * 100 */
} LDAPAPIInfo;

#define LDAP_FEATURE_INFO_VERSION (1) /* version of api feature structure */
typedef struct ldap_apifeature_info {
	int		ldapaif_info_version; /* version of this struct (1) */
	char*	ldapaif_name;    /* matches LDAP_API_FEATURE_... less the prefix */
	int		ldapaif_version; /* matches the value LDAP_API_FEATURE_... */
} LDAPAPIFeatureInfo;

typedef struct ldapcontrol {
	char *			ldctl_oid;
	struct berval	ldctl_value;
	char			ldctl_iscritical;
} LDAPControl;

/* LDAP Controls */
	/* chase referrals controls */
#define LDAP_CONTROL_REFERRALS	"1.2.840.113666.1.4.616"
#define LDAP_CHASE_SUBORDINATE_REFERRALS	0x0020U
#define LDAP_CHASE_EXTERNAL_REFERRALS	0x0040U

#define LDAP_CONTROL_MANAGEDSAIT "2.16.840.1.113730.3.4.2"

#define LDAP_SERVER_EDIR_SEMANTICS_OID    "2.16.840.1.113719.1.513.4.5"

/* Experimental Controls */

#define LDAP_CONTROL_SORTREQUEST    "1.2.840.113556.1.4.473"
#define LDAP_CONTROL_SORTRESPONSE	"1.2.840.113556.1.4.474"
#define LDAP_CONTROL_VLVREQUEST    	"2.16.840.1.113730.3.4.9"
#define LDAP_CONTROL_VLVRESPONSE    "2.16.840.1.113730.3.4.10"
#define LDAP_CONTROL_PERSISTENTSEARCH	"2.16.840.1.113730.3.4.3"
#define LDAP_CONTROL_ENTRYCHANGE	"2.16.840.1.113730.3.4.7"

#define LDAP_CONTROL_SIMPLE_PASS    "2.16.840.1.113719.1.27.101.5"

/* Controls for NVDS */

#define LDAP_CONTROL_EFFECTPRV		"2.16.840.1.113719.1.27.101.33"

#define LDAP_CONTROL_SSTATREQUEST	"2.16.840.1.113719.1.27.101.40"
#define LDAP_CONTROL_SSTATRESPONSE	"2.16.840.1.113719.1.27.101.40"

#define LDAP_CONTROL_REFREQUEST		"2.16.840.1.113719.1.1.5150.101.1"
#define LDAP_CONTROL_REFRESPONSE	"2.16.840.1.113719.1.1.5150.101.1"


/* LDAP Unsolicited Notifications */
#define	LDAP_NOTICE_OF_DISCONNECTION	"1.3.6.1.4.1.1466.20036"
#define LDAP_NOTICE_DISCONNECT LDAP_NOTICE_OF_DISCONNECTION

/* LDAP Extended Operations */
#define LDAP_EXOP_START_TLS "1.3.6.1.4.1.1466.20037"

#define LDAP_EXOP_X_MODIFY_PASSWD "1.3.6.1.4.1.4203.1.11.1"
#define LDAP_TAG_EXOP_X_MODIFY_PASSWD_ID	((ber_tag_t) 0x80U)
#define LDAP_TAG_EXOP_X_MODIFY_PASSWD_OLD	((ber_tag_t) 0x81U)
#define LDAP_TAG_EXOP_X_MODIFY_PASSWD_NEW	((ber_tag_t) 0x82U)
#define LDAP_TAG_EXOP_X_MODIFY_PASSWD_GEN	((ber_tag_t) 0x80U)

/* LDAP Cancel Operations */

#define LDAP_CANCEL_REQUEST "1.3.6.1.1.8"

/* 
 * specific LDAP instantiations of BER types we know about
 */

/* Overview of LBER tag construction
 *
 *	Bits
 *	______
 *	8 7 | CLASS
 *	0 0 = UNIVERSAL
 *	0 1 = APPLICATION
 *	1 0 = CONTEXT-SPECIFIC
 *	1 1 = PRIVATE
 *		_____
 *		| 6 | DATA-TYPE
 *		  0 = PRIMITIVE
 *		  1 = CONSTRUCTED
 *			___________
 *			| 5 ... 1 | TAG-NUMBER
 */

/* general stuff */
#define LDAP_TAG_MESSAGE	((ber_tag_t) 0x30U)	/* constructed + 16 */
#define LDAP_TAG_MSGID		((ber_tag_t) 0x02U)	/* integer */
#define LDAP_TAG_LDAPDN		((ber_tag_t) 0x04U)	/* octect string */
#define LDAP_TAG_LDAPCRED	((ber_tag_t) 0x04U)	/* octect string */
#define LDAP_TAG_CONTROLS	((ber_tag_t) 0xa0U)	/* context specific + constructed + 0 */
#define LDAP_TAG_REFERRAL	((ber_tag_t) 0xa3U)	/* context specific + constructed + 3 */

#define LDAP_TAG_NEWSUPERIOR	((ber_tag_t) 0x80U)	/* context-specific + primitive + 0 */

#define LDAP_TAG_EXOP_REQ_OID   ((ber_tag_t) 0x80U)	/* context specific + primitive */
#define LDAP_TAG_EXOP_REQ_VALUE ((ber_tag_t) 0x81U)	/* context specific + primitive */
#define LDAP_TAG_EXOP_RES_OID   ((ber_tag_t) 0x8aU)	/* context specific + primitive */
#define LDAP_TAG_EXOP_RES_VALUE ((ber_tag_t) 0x8bU)	/* context specific + primitive */

#define LDAP_TAG_INTERMED_RES_OID   ((ber_tag_t) 0x80U) /*context specific + primitive*/
#define LDAP_TAG_INTERMED_RES_VALUE ((ber_tag_t) 0x81U) /*context specific + primitive*/
#define LDAP_TAG_SASL_RES_CREDS	((ber_tag_t) 0x87U)	/* context specific + primitive */


/* possible operations a client can invoke */
#define LDAP_REQ_BIND			((ber_tag_t) 0x60U)	/* application + constructed */
#define LDAP_REQ_UNBIND			((ber_tag_t) 0x42U)	/* application + primitive   */
#define LDAP_REQ_SEARCH			((ber_tag_t) 0x63U)	/* application + constructed */
#define LDAP_REQ_MODIFY			((ber_tag_t) 0x66U)	/* application + constructed */
#define LDAP_REQ_ADD			((ber_tag_t) 0x68U)	/* application + constructed */
#define LDAP_REQ_DELETE			((ber_tag_t) 0x4aU)	/* application + primitive   */
#define LDAP_REQ_MODDN			((ber_tag_t) 0x6cU)	/* application + constructed */
#define LDAP_REQ_MODRDN			LDAP_REQ_MODDN	
#define LDAP_REQ_RENAME			LDAP_REQ_MODDN	
#define LDAP_REQ_COMPARE		((ber_tag_t) 0x6eU)	/* application + constructed */
#define LDAP_REQ_ABANDON		((ber_tag_t) 0x50U)	/* application + primitive   */
#define LDAP_REQ_EXTENDED		((ber_tag_t) 0x77U)	/* application + constructed */

/* possible result types a server can return */
#define LDAP_RES_BIND			((ber_tag_t) 0x61U)	/* application + constructed */
#define LDAP_RES_SEARCH_ENTRY		((ber_tag_t) 0x64U)	/* application + constructed */
#define LDAP_RES_SEARCH_REFERENCE	((ber_tag_t) 0x73U)	/* V3: application + constructed */
#define LDAP_RES_SEARCH_RESULT		((ber_tag_t) 0x65U)	/* application + constructed */
#define LDAP_RES_MODIFY			((ber_tag_t) 0x67U)	/* application + constructed */
#define LDAP_RES_ADD			((ber_tag_t) 0x69U)	/* application + constructed */
#define LDAP_RES_DELETE			((ber_tag_t) 0x6bU)	/* application + constructed */
#define LDAP_RES_MODDN			((ber_tag_t) 0x6dU)	/* application + constructed */
#define LDAP_RES_MODRDN			LDAP_RES_MODDN	/* application + constructed */
#define LDAP_RES_RENAME			LDAP_RES_MODDN	/* application + constructed */
#define LDAP_RES_COMPARE		((ber_tag_t) 0x6fU)	/* application + constructed */
#define LDAP_RES_EXTENDED		((ber_tag_t) 0x78U)	/* V3: application + constructed */
#define LDAP_RES_EXTENDED_PARTIAL	((ber_tag_t) 0x79U)	/* V3+: application + constructed */
#define LDAP_RES_INTERMEDIATE       ((ber_tag_t) 0x79U)

#define LDAP_RES_ANY			(-1)
#define LDAP_RES_UNSOLICITED	(0)


/* sasl methods */
#define LDAP_SASL_SIMPLE		((char*)0)


/* authentication methods available */
#define LDAP_AUTH_NONE		((ber_tag_t) 0x00U)	/* no authentication		  */
#define LDAP_AUTH_SIMPLE	((ber_tag_t) 0x80U)	/* context specific + primitive   */
#define LDAP_AUTH_SASL		((ber_tag_t) 0xa3U)	/* context specific + primitive   */
#define LDAP_AUTH_KRBV4		((ber_tag_t) 0xffU)	/* means do both of the following */
#define LDAP_AUTH_KRBV41	((ber_tag_t) 0x81U)	/* context specific + primitive   */
#define LDAP_AUTH_KRBV42	((ber_tag_t) 0x82U)	/* context specific + primitive   */


/* filter types */
#define LDAP_FILTER_AND		((ber_tag_t) 0xa0U)	/* context specific + constructed */
#define LDAP_FILTER_OR		((ber_tag_t) 0xa1U)	/* context specific + constructed */
#define LDAP_FILTER_NOT		((ber_tag_t) 0xa2U)	/* context specific + constructed */
#define LDAP_FILTER_EQUALITY	((ber_tag_t) 0xa3U)	/* context specific + constructed */
#define LDAP_FILTER_SUBSTRINGS	((ber_tag_t) 0xa4U)	/* context specific + constructed */
#define LDAP_FILTER_GE		((ber_tag_t) 0xa5U)	/* context specific + constructed */
#define LDAP_FILTER_LE		((ber_tag_t) 0xa6U)	/* context specific + constructed */
#define LDAP_FILTER_PRESENT	((ber_tag_t) 0x87U)	/* context specific + primitive   */
#define LDAP_FILTER_APPROX	((ber_tag_t) 0xa8U)	/* context specific + constructed */
#define LDAP_FILTER_EXT		((ber_tag_t) 0xa9U)	/* context specific + constructed */

/* extended filter component types */
#define LDAP_FILTER_EXT_OID	((ber_tag_t) 0x81U)	/* context specific */
#define LDAP_FILTER_EXT_TYPE	((ber_tag_t) 0x82U)	/* context specific */
#define LDAP_FILTER_EXT_VALUE	((ber_tag_t) 0x83U)	/* context specific */
#define LDAP_FILTER_EXT_DNATTRS	((ber_tag_t) 0x84U)	/* context specific */

/* substring filter component types */
#define LDAP_SUBSTRING_INITIAL	((ber_tag_t) 0x80U)	/* context specific */
#define LDAP_SUBSTRING_ANY	((ber_tag_t) 0x81U)	/* context specific */
#define LDAP_SUBSTRING_FINAL	((ber_tag_t) 0x82U)	/* context specific */

/* search scopes */
#define LDAP_SCOPE_DEFAULT		((ber_int_t) -1)
#define LDAP_SCOPE_BASE			((ber_int_t) 0x0000)
#define LDAP_SCOPE_ONELEVEL		((ber_int_t) 0x0001)
#define LDAP_SCOPE_SUBTREE		((ber_int_t) 0x0002)
#define LDAP_SCOPE_SUBORDINATE_SUBTREE	((ber_int_t) 0x0004)

/* substring filter component types */
#define LDAP_SUBSTRING_INITIAL	((ber_tag_t) 0x80U)	/* context specific */
#define LDAP_SUBSTRING_ANY	((ber_tag_t) 0x81U)	/* context specific */
#define LDAP_SUBSTRING_FINAL	((ber_tag_t) 0x82U)	/* context specific */

/* 
 * possible error codes we can return
 */

#define LDAP_RANGE(n,x,y)	(((x) <= (n)) && ((n) <= (y)))

#define LDAP_SUCCESS					0x00
#define LDAP_OPERATIONS_ERROR			0x01
#define LDAP_PROTOCOL_ERROR				0x02
#define LDAP_TIMELIMIT_EXCEEDED			0x03
#define LDAP_SIZELIMIT_EXCEEDED			0x04
#define LDAP_COMPARE_FALSE				0x05
#define LDAP_COMPARE_TRUE				0x06
#define LDAP_AUTH_METHOD_NOT_SUPPORTED	0x07
#define LDAP_STRONG_AUTH_NOT_SUPPORTED	LDAP_AUTH_METHOD_NOT_SUPPORTED
#define LDAP_STRONG_AUTH_REQUIRED		0x08
#define LDAP_PARTIAL_RESULTS			0x09	/* not listed in v3 */

#define	LDAP_REFERRAL					0x0a /* LDAPv3 */
#define LDAP_ADMINLIMIT_EXCEEDED		0x0b /* LDAPv3 */
#define	LDAP_UNAVAILABLE_CRITICAL_EXTENSION	0x0c /* LDAPv3 */
#define LDAP_CONFIDENTIALITY_REQUIRED	0x0d /* LDAPv3 */
#define	LDAP_SASL_BIND_IN_PROGRESS		0x0e /* LDAPv3 */	

#define LDAP_ATTR_ERROR(n)	LDAP_RANGE((n),0x10,0x15) /* 16-21 */

#define LDAP_NO_SUCH_ATTRIBUTE		0x10
#define LDAP_UNDEFINED_TYPE			0x11
#define LDAP_INAPPROPRIATE_MATCHING	0x12
#define LDAP_CONSTRAINT_VIOLATION	0x13
#define LDAP_TYPE_OR_VALUE_EXISTS	0x14
#define LDAP_INVALID_SYNTAX			0x15

#define LDAP_NAME_ERROR(n)	LDAP_RANGE((n),0x20,0x24) /* 32-34,36 */

#define LDAP_NO_SUCH_OBJECT			0x20
#define LDAP_ALIAS_PROBLEM			0x21
#define LDAP_INVALID_DN_SYNTAX		0x22
#define LDAP_IS_LEAF				0x23 /* not LDAPv3 */
#define LDAP_ALIAS_DEREF_PROBLEM	0x24

#define LDAP_SECURITY_ERROR(n)	LDAP_RANGE((n),0x30,0x32) /* 48-50 */

#define LDAP_INAPPROPRIATE_AUTH		0x30
#define LDAP_INVALID_CREDENTIALS	0x31
#define LDAP_INSUFFICIENT_ACCESS	0x32

#define LDAP_SERVICE_ERROR(n)	LDAP_RANGE((n),0x33,0x36) /* 51-54 */

#define LDAP_BUSY					0x33
#define LDAP_UNAVAILABLE			0x34
#define LDAP_UNWILLING_TO_PERFORM	0x35
#define LDAP_LOOP_DETECT			0x36

#define LDAP_UPDATE_ERROR(n)	LDAP_RANGE((n),0x40,0x47) /* 64-69,71 */

#define LDAP_NAMING_VIOLATION		0x40
#define LDAP_OBJECT_CLASS_VIOLATION	0x41
#define LDAP_NOT_ALLOWED_ON_NONLEAF	0x42
#define LDAP_NOT_ALLOWED_ON_RDN		0x43
#define LDAP_ALREADY_EXISTS			0x44
#define LDAP_NO_OBJECT_CLASS_MODS	0x45
#define LDAP_RESULTS_TOO_LARGE		0x46 /* CLDAP */
#define LDAP_AFFECTS_MULTIPLE_DSAS	0x47 /* LDAPv3 */

#define LDAP_OTHER					0x50

#define LDAP_API_ERROR(n)		LDAP_RANGE((n),0x51,0x61) /* 81-97 */

#define LDAP_SERVER_DOWN		0x51
#define LDAP_LOCAL_ERROR		0x52
#define LDAP_ENCODING_ERROR		0x53
#define LDAP_DECODING_ERROR		0x54
#define LDAP_TIMEOUT			0x55
#define LDAP_AUTH_UNKNOWN		0x56
#define LDAP_FILTER_ERROR		0x57
#define LDAP_USER_CANCELLED		0x58
#define LDAP_PARAM_ERROR		0x59
#define LDAP_NO_MEMORY			0x5a

/* not technically reserved for APIs */
#define LDAP_CONNECT_ERROR				0x5b	/* draft-ietf-ldap-c-api-xx */
#define LDAP_NOT_SUPPORTED				0x5c	/* draft-ietf-ldap-c-api-xx */
#define LDAP_CONTROL_NOT_FOUND			0x5d	/* draft-ietf-ldap-c-api-xx */
#define LDAP_NO_RESULTS_RETURNED		0x5e	/* draft-ietf-ldap-c-api-xx */
#define LDAP_MORE_RESULTS_TO_RETURN		0x5f	/* draft-ietf-ldap-c-api-xx */
#define LDAP_CLIENT_LOOP				0x60	/* draft-ietf-ldap-c-api-xx */
#define LDAP_REFERRAL_LIMIT_EXCEEDED	0x61	/* draft-ietf-ldap-c-api-xx */

/* LDAP Cancel operation */
#define LDAP_CANCELED			0X76
#define LDAP_CANCEL_NO_SUCH_OPERATION	0X77
#define LDAP_CANCEL_TOO_LATE		0X78
#define LDAP_CANCEL_CANNOT_CANCEL	0X79

/*
 * This structure represents both ldap messages and ldap responses.
 * These are really the same, except in the case of search responses,
 * where a response has multiple messages.
 */

typedef struct ldapmsg LDAPMessage;

/* for modifications */
typedef union mod_vals_u {			   /* Novell: Updated for C API draft 5 */
	char			**modv_strvals;
	struct berval	**modv_bvals;
} mod_vals_u_t;

typedef struct ldapmod {
	int		mod_op;

#define LDAP_MOD_ADD		((ber_int_t) 0x0000)
#define LDAP_MOD_DELETE		((ber_int_t) 0x0001)
#define LDAP_MOD_REPLACE	((ber_int_t) 0x0002)
#define LDAP_MOD_BVALUES	((ber_int_t) 0x0080)
/* IMPORTANT: do not use code 0x1000 (or above),
 * it is used internally by the backends!
 * (see ldap/servers/slapd/slap.h)
 */

	char		*mod_type;
	mod_vals_u_t	mod_vals;
#define mod_values	mod_vals.modv_strvals
#define mod_bvalues	mod_vals.modv_bvals
} LDAPMod;

/*
 * structures for ldap getfilter routines
 */

typedef struct ldap_filt_info {
	char		*lfi_filter;
	char		*lfi_desc;
	int			lfi_scope;
	int			lfi_isexact;
	struct ldap_filt_info	*lfi_next;
} LDAPFiltInfo;

typedef struct ldap_filt_list {
    char			*lfl_tag;
    char			*lfl_pattern;
    char			*lfl_delims;
    LDAPFiltInfo	*lfl_ilist;
    struct ldap_filt_list	*lfl_next;
} LDAPFiltList;


#define LDAP_FILT_MAXSIZ	1024

typedef struct ldap_filt_desc {
	LDAPFiltList	*lfd_filtlist;
	LDAPFiltInfo	*lfd_curfip;
	LDAPFiltInfo	lfd_retfi;
	char			lfd_filter[ LDAP_FILT_MAXSIZ ];
	char			*lfd_curval;
	char			*lfd_curvalcopy;
	char			**lfd_curvalwords;
	char			*lfd_filtprefix;
	char			*lfd_filtsuffix;
} LDAPFiltDesc;


/*
 * structure representing an ldap session which can
 * encompass connections to multiple servers (in the
 * face of referrals).
 */
typedef struct ldap LDAP;

#define LDAP_DEREF_NEVER		0x00
#define LDAP_DEREF_SEARCHING	0x01
#define LDAP_DEREF_FINDING		0x02
#define LDAP_DEREF_ALWAYS		0x03

#define LDAP_NO_LIMIT			  0
#define LDAP_DEFAULT_SIZELIMIT  (-1)

/* how many messages to retrieve results for */
#define LDAP_MSG_ONE		0x00
#define LDAP_MSG_ALL		0x01
#define LDAP_MSG_RECEIVED	0x02

/*
 * structure for ldap friendly mapping routines
 */

typedef struct ldap_friendly {
	char	*lf_unfriendly;
	char	*lf_friendly;
} LDAPFriendlyMap;

/*
 * types for ldap URL handling
 */
typedef struct ldap_url_desc {
	struct ldap_url_desc *lud_next;
	char	*lud_scheme;
	char	*lud_host;
	int		lud_port;
	char	*lud_dn;
	char	**lud_attrs;
	int		lud_scope;
	char	*lud_filter;
	char	**lud_exts;
	int		lud_crit_exts;
} LDAPURLDesc;

#define LDAP_URL_SUCCESS		0x00	/* Success */
#define LDAP_URL_ERR_MEM		0x01	/* can't allocate memory space */
#define LDAP_URL_ERR_PARAM		0x02	/* parameter is bad */

#define LDAP_URL_ERR_BADSCHEME	0x03	/* URL doesn't begin with "ldap[si]://" */
#define LDAP_URL_ERR_BADENCLOSURE 0x04	/* URL is missing trailing ">" */
#define LDAP_URL_ERR_BADURL		0x05	/* URL is bad */
#define LDAP_URL_ERR_BADHOST	0x06	/* host port is bad */
#define LDAP_URL_ERR_BADATTRS	0x07	/* bad (or missing) attributes */
#define LDAP_URL_ERR_BADSCOPE	0x08	/* scope string is invalid (or missing) */
#define LDAP_URL_ERR_BADFILTER	0x09	/* bad or missing filter */
#define LDAP_URL_ERR_BADEXTS	0x0a	/* bad or missing extensions */


/*
 *  IO functions structure
 */
typedef struct ldap_io_funcs {
	ber_socket_t (LIBCALL *io_socket) ( int, int, int, LDAP * );
	int   (LIBCALL *io_prepare)  ( ber_socket_t, LDAP * );
	int   (LIBCALL *io_connect)  ( ber_socket_t, struct sockaddr *, int, LDAP * );
	int   (LIBCALL *io_read)     ( ber_socket_t, void *, ber_len_t, LDAP * );
	int   (LIBCALL *io_write)    ( ber_socket_t, void *, ber_len_t, LDAP * );
#ifdef HAVE_POLL
	int   (LIBCALL *io_select)   ( int, struct pollfd *, fd_set *, fd_set *, struct timeval *, LDAP *);
#else
	int   (LIBCALL *io_select)   ( int, fd_set *, fd_set *, fd_set *, struct timeval *, LDAP *);
#endif
	int   (LIBCALL *io_ioctl)    ( ber_socket_t, long, void *, LDAP * );
	int   (LIBCALL *io_close)    ( ber_socket_t, LDAP * );
	} LDAPIOFuncs;


/*
 * in loptions.c:
 */
LDAP_F( int )
ldap_get_option LDAP_P((
	LDAP *ld,
	int option,
	void *outvalue));

LDAP_F( int )
ldap_set_option LDAP_P((
	LDAP *ld,
	int option,
	LDAP_CONST void *invalue));

/* V3 REBIND Function Callback Prototype */
typedef int (LIBCALL LDAP_REBIND_PROC) LDAP_P((
	LDAP *ld, LDAP_CONST char *url, int request, ber_int_t msgid ));

LDAP_F( int )
ldap_set_rebind_proc LDAP_P((
	LDAP *ld,
	LDAP_REBIND_PROC *ldap_proc));

/*
 * in controls.c:
 */
LDAP_F( void )
ldap_control_free LDAP_P((
	LDAPControl *ctrl ));

LDAP_F( void )
ldap_controls_free LDAP_P((
	LDAPControl **ctrls ));

  
/*
 * in extended.c:
 */
LDAP_F( int )
ldap_extended_operation LDAP_P((
	LDAP			*ld,
	LDAP_CONST char	*reqoid,
	struct berval	*reqdata,
	LDAPControl		**serverctrls,
	LDAPControl		**clientctrls,
	int				*msgidp ));

LDAP_F( int )
ldap_extended_operation_s LDAP_P((
	LDAP			*ld,
	LDAP_CONST char	*reqoid,
	struct berval	*reqdata,
	LDAPControl		**serverctrls,
	LDAPControl		**clientctrls,
	char			**retoidp,
	struct berval	**retdatap ));

LDAP_F( int )
ldap_parse_extended_result LDAP_P((
	LDAP			*ld,
	LDAPMessage		*res,
	char			**retoidp,
	struct berval	**retdatap,
	int				freeit ));

LDAP_F( int )
ldap_parse_intermediate LDAP_P((
	LDAP			*ld,
	LDAPMessage		*res,
	char			**retoidp,
	struct berval	**retdatap,
	LDAPControl		***serverctrls,
	int				freeit ));

/*
 * in abandon.c:
 */
LDAP_F( int )
ldap_abandon_ext LDAP_P((
	LDAP			*ld,
	int				msgid,
	LDAPControl		**serverctrls,
	LDAPControl		**clientctrls ));

LDAP_F( int )
ldap_abandon LDAP_P((	/* deprecated */
	LDAP *ld,
	int msgid ));


/*
 * in add.c:
 */
LDAP_F( int )
ldap_add_ext LDAP_P((
	LDAP			*ld,
	LDAP_CONST char	*dn,
	LDAPMod			**attrs,
	LDAPControl		**serverctrls,
	LDAPControl		**clientctrls,
	int 			*msgidp ));

LDAP_F( int )
ldap_add_ext_s LDAP_P((
	LDAP			*ld,
	LDAP_CONST char	*dn,
	LDAPMod			**attrs,
	LDAPControl		**serverctrls,
	LDAPControl		**clientctrls ));

LDAP_F( int )
ldap_add LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *dn,
	LDAPMod **attrs ));

LDAP_F( int )
ldap_add_s LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *dn,
	LDAPMod **attrs ));


/*
 * in sasl.c:
 */
LDAP_F( int )
ldap_sasl_bind LDAP_P((
	LDAP			*ld,
	LDAP_CONST char	*dn,
	LDAP_CONST char	*mechanism,
	struct berval	*cred,
	LDAPControl		**serverctrls,
	LDAPControl		**clientctrls,
	int				*msgidp ));

LDAP_F( int )
ldap_sasl_bind_s LDAP_P((
	LDAP			*ld,
	LDAP_CONST char	*dn,
	LDAP_CONST char	*mechanism,
	struct berval	*cred,
	LDAPControl		**serverctrls,
	LDAPControl		**clientctrls,
	struct berval	**servercredp ));

LDAP_F( int )
ldap_parse_sasl_bind_result LDAP_P((
	LDAP			*ld,
	LDAPMessage		*res,
	struct berval	**servercredp,
	int				freeit ));

/*
 * in bind.c:
 *	(deprecated)
 */
LDAP_F( int )
ldap_bind LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *who,
	LDAP_CONST char *passwd,
	int authmethod ));

LDAP_F( int )
ldap_bind_s LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *who,
	LDAP_CONST char *cred,
	int authmethod ));

/*
 * in sbind.c:
 */
LDAP_F( int )
ldap_simple_bind LDAP_P((
	LDAP *ld,
	LDAP_CONST char *who,
	LDAP_CONST char *passwd ));

LDAP_F( int )
ldap_simple_bind_s LDAP_P((
	LDAP *ld,
	LDAP_CONST char *who,
	LDAP_CONST char *passwd ));


/*
 * in compare.c:
 */
LDAP_F( int )
ldap_compare_ext LDAP_P((
	LDAP			*ld,
	LDAP_CONST char	*dn,
	LDAP_CONST char	*attr,
	struct berval	*bvalue,
	LDAPControl		**serverctrls,
	LDAPControl		**clientctrls,
	int 			*msgidp ));

LDAP_F( int )
ldap_compare_ext_s LDAP_P((
	LDAP			*ld,
	LDAP_CONST char	*dn,
	LDAP_CONST char	*attr,
	struct berval	*bvalue,
	LDAPControl		**serverctrls,
	LDAPControl		**clientctrls ));

LDAP_F( int )
ldap_compare LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *dn,
	LDAP_CONST char *attr,
	LDAP_CONST char *value ));

LDAP_F( int )
ldap_compare_s LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *dn,
	LDAP_CONST char *attr,
	LDAP_CONST char *value ));


/*
 * in delete.c:
 */
LDAP_F( int )
ldap_delete_ext LDAP_P((
	LDAP			*ld,
	LDAP_CONST char	*dn,
	LDAPControl		**serverctrls,
	LDAPControl		**clientctrls,
	int 			*msgidp ));

LDAP_F( int )
ldap_delete_ext_s LDAP_P((
	LDAP			*ld,
	LDAP_CONST char	*dn,
	LDAPControl		**serverctrls,
	LDAPControl		**clientctrls ));

LDAP_F( int )
ldap_delete LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *dn ));

LDAP_F( int )
ldap_delete_s LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *dn ));


/*
 * in error.c:
 */
LDAP_F( int )
ldap_parse_result LDAP_P((
	LDAP			*ld,
	LDAPMessage		*res,
	int				*errcodep,
	char			**matcheddnp,
	char			**errmsgp,
	char			***referralsp,
	LDAPControl		***serverctrls,
	int				freeit ));

LDAP_F( char *)
ldap_err2string LDAP_P((
	int err ));

LDAP_F( char *)
ldap_nmas_err2string LDAP_P((
	int err ));

LDAP_F( int )
ldap_result2error LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAPMessage *r,
	int freeit ));

LDAP_F( void )
ldap_perror LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *s ));


/*
 * in lderrno.c:   Addition by Novell for compatibility
 */

LDAP_F(int)					/* deprecated */
ldap_get_lderrno( LDAP *ld, char **matchedDN, char **errmsg );

LDAP_F(int)                  /* deprecated */
ldap_set_lderrno( LDAP *ld, int errnum, char *matchedDN, char *errmsg );


/*
 * in modify.c:
 */
LDAP_F( int )
ldap_modify_ext LDAP_P((
	LDAP			*ld,
	LDAP_CONST char	*dn,
	LDAPMod			**mods,
	LDAPControl		**serverctrls,
	LDAPControl		**clientctrls,
	int 			*msgidp ));

LDAP_F( int )
ldap_modify_ext_s LDAP_P((
	LDAP			*ld,
	LDAP_CONST char	*dn,
	LDAPMod			**mods,
	LDAPControl		**serverctrls,
	LDAPControl		**clientctrls ));

LDAP_F( int )
ldap_modify LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *dn,
	LDAPMod **mods ));

LDAP_F( int )
ldap_modify_s LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *dn,
	LDAPMod **mods ));


/*
 * in modrdn.c:
 */
LDAP_F( int )
ldap_rename LDAP_P((
	LDAP *ld,
	LDAP_CONST char *dn,
	LDAP_CONST char *newrdn,
	LDAP_CONST char *newSuperior,
	int deleteoldrdn,
	LDAPControl **sctrls,
	LDAPControl **cctrls,
	int *msgidp ));

LDAP_F( int )
ldap_rename_s LDAP_P((
	LDAP *ld,
	LDAP_CONST char *dn,
	LDAP_CONST char *newrdn,
	LDAP_CONST char *newSuperior,
	int deleteoldrdn,
	LDAPControl **sctrls,
	LDAPControl **cctrls ));

LDAP_F( int )
ldap_modrdn LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *dn,
	LDAP_CONST char *newrdn ));

LDAP_F( int )
ldap_modrdn_s LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *dn,
	LDAP_CONST char *newrdn ));

LDAP_F( int )
ldap_modrdn2 LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *dn,
	LDAP_CONST char *newrdn,
	int deleteoldrdn ));

LDAP_F( int )
ldap_modrdn2_s LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *dn,
	LDAP_CONST char *newrdn,
	int deleteoldrdn));


/*
 * in open.c:
 */
LDAP_F( LDAP *)
ldap_init LDAP_P((
	LDAP_CONST char *host,
	int port ));

LDAP_F( LDAP *)
ldap_open LDAP_P((	/* deprecated */
	LDAP_CONST char *host,
	int port ));

LDAP_F( int )
ldap_destroy LDAP_P((
	LDAP *ld)); 

LDAP_F(LDAP *)
ldap_dup LDAP_P((
	LDAP *ld));

/*
 * in messages.c:
 */
LDAP_F( LDAPMessage *)
ldap_first_message LDAP_P((
	LDAP *ld,
	LDAPMessage *chain ));

LDAP_F( LDAPMessage *)
ldap_next_message LDAP_P((
	LDAP *ld,
	LDAPMessage *msg ));

LDAP_F( int )
ldap_count_messages LDAP_P((
	LDAP *ld,
	LDAPMessage *chain ));


/*
 * in references.c:
 */
LDAP_F( LDAPMessage *)
ldap_first_reference LDAP_P((
	LDAP *ld,
	LDAPMessage *chain ));

LDAP_F( LDAPMessage *)
ldap_next_reference LDAP_P((
	LDAP *ld,
	LDAPMessage *ref ));

LDAP_F( int )
ldap_count_references LDAP_P((
	LDAP *ld,
	LDAPMessage *chain ));

LDAP_F( int )
ldap_parse_reference LDAP_P((
	LDAP			*ld,
	LDAPMessage		*ref,
	char			***referralsp,
	LDAPControl		***serverctrls,
	int				freeit));


/*
 * in getentry.c:
 */
LDAP_F( LDAPMessage *)
ldap_first_entry LDAP_P((
	LDAP *ld,
	LDAPMessage *chain ));

LDAP_F( LDAPMessage *)
ldap_next_entry LDAP_P((
	LDAP *ld,
	LDAPMessage *entry ));

LDAP_F( int )
ldap_count_entries LDAP_P((
	LDAP *ld,
	LDAPMessage *chain ));

LDAP_F( int )
ldap_get_entry_controls LDAP_P((
	LDAP			*ld,
	LDAPMessage		*entry,
	LDAPControl		***serverctrls));


/*
 * in getdn.c
 */
LDAP_F( char * )
ldap_get_dn LDAP_P((
	LDAP *ld,
	LDAPMessage *entry ));

LDAP_F( char * )
ldap_dn2ufn LDAP_P((
	LDAP_CONST char *dn ));

LDAP_F( char ** )
ldap_explode_dn LDAP_P((
	LDAP_CONST char *dn,
	int notypes ));

LDAP_F( char ** )
ldap_explode_rdn LDAP_P((
	LDAP_CONST char *rdn,
	int notypes ));

/*
 * in getattr.c
 */
LDAP_F( char *)
ldap_first_attribute LDAP_P((									 
	LDAP *ld,
	LDAPMessage *entry,
	BerElement **ber ));

LDAP_F( char *)
ldap_next_attribute LDAP_P((
	LDAP *ld,
	LDAPMessage *entry,
	BerElement *ber ));


/*
 * in free.c
 */
LDAP_F( void )
ldap_memfree LDAP_P((
	void* p ));


/*
 * in getvalues.c
 */
LDAP_F( char **)
ldap_get_values LDAP_P((
	LDAP *ld,
	LDAPMessage *entry,
	LDAP_CONST char *target ));

LDAP_F( struct berval **)
ldap_get_values_len LDAP_P((
	LDAP *ld,
	LDAPMessage *entry,
	LDAP_CONST char *target ));

LDAP_F( int )
ldap_count_values LDAP_P((
	char **vals ));

LDAP_F( int )
ldap_count_values_len LDAP_P((
	struct berval **vals ));

LDAP_F( void )
ldap_value_free LDAP_P((
	char **vals ));

LDAP_F( void )
ldap_value_free_len LDAP_P((
	struct berval **vals ));

/*
 * in result.c:
 */
LDAP_F( int )
ldap_result LDAP_P((
	LDAP *ld,
	int msgid,
	int all,
	struct timeval *timeout,
	LDAPMessage **result ));

LDAP_F( int )
ldap_msgtype LDAP_P((
	LDAPMessage *lm ));

LDAP_F( int )
ldap_msgid   LDAP_P((
	LDAPMessage *lm ));

LDAP_F( int )
ldap_msgfree LDAP_P((
	LDAPMessage *lm ));


/*
 * in search.c:
 */
LDAP_F( int )
ldap_search_ext LDAP_P((
	LDAP			*ld,
	LDAP_CONST char	*base,
	int				scope,
	LDAP_CONST char	*filter,
	char			**attrs,
	int				attrsonly,
	LDAPControl		**serverctrls,
	LDAPControl		**clientctrls,
	struct timeval	*timeout,
	int				sizelimit,
	int				*msgidp ));

LDAP_F( int )
ldap_search_ext_s LDAP_P((
	LDAP			*ld,
	LDAP_CONST char	*base,
	int				scope,
	LDAP_CONST char	*filter,
	char			**attrs,
	int				attrsonly,
	LDAPControl		**serverctrls,
	LDAPControl		**clientctrls,
	struct timeval	*timeout,
	int				sizelimit,
	LDAPMessage		**res ));

LDAP_F( int )
ldap_search LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *base,
	int scope,
	LDAP_CONST char *filter,
	char **attrs,
	int attrsonly ));

LDAP_F( int )
ldap_search_s LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *base,
	int scope,
	LDAP_CONST char *filter,
	char **attrs,
	int attrsonly,
	LDAPMessage **res ));

LDAP_F( int )
ldap_search_st LDAP_P((	/* deprecated */
	LDAP *ld,
	LDAP_CONST char *base,
	int scope,
	LDAP_CONST char *filter,
    char **attrs,
	int attrsonly,
	struct timeval *timeout,
	LDAPMessage **res ));


/*
 * in unbind.c
 */
LDAP_F( int )
ldap_unbind LDAP_P((
	LDAP *ld ));

LDAP_F( int )
ldap_unbind_s LDAP_P((
	LDAP *ld ));

LDAP_F( int )
ldap_unbind_ext LDAP_P((
	LDAP			*ld,
	LDAPControl		**serverctrls,
	LDAPControl		**clientctrls));

LDAP_F( int )
ldap_unbind_ext_s LDAP_P((
	LDAP			*ld,
	LDAPControl		**serverctrls,
	LDAPControl		**clientctrls));

/*
 * in sort.c
 */
LDAP_F( int )
ldap_sort_entries LDAP_P(( LDAP *ld,
	LDAPMessage **chain,
	LDAP_CONST char *attr,
	int (*cmp) (LDAP_CONST char *, LDAP_CONST char *) ));

LDAP_F( int )
ldap_multisort_entries LDAP_P(( LDAP *ld,
	LDAPMessage **chain,
	LDAP_CONST char **attrs,
	int (*cmp) (LDAP_CONST char *, LDAP_CONST char *) ));

LDAP_F( int )
ldap_sort_values LDAP_P((
	LDAP *ld,
	char **vals,
	int (*cmp) (LDAP_CONST void *, LDAP_CONST void *) ));

LDAP_F( int )
ldap_sort_strcasecmp LDAP_P((
	LDAP_CONST void *a,
	LDAP_CONST void *b ));


/*
 * in url.c
 *
 * need _ext variants
 */
LDAP_F( int )
ldap_is_ldap_url LDAP_P((
	LDAP_CONST char *url ));

LDAP_F( int )
ldap_is_ldaps_url LDAP_P((
	LDAP_CONST char *url ));

LDAP_F( int )
ldap_url_parse LDAP_P((
	LDAP_CONST char *url,
	LDAPURLDesc **ludpp ));

LDAP_F(int)
ldap_url_parse_ext LDAP_P(( 
    LDAP_CONST char *url_in, 
    LDAPURLDesc **ludpp ));

LDAP_F( char * )
ldap_url_desc2str LDAP_P((
	LDAPURLDesc *ludp ));

LDAP_F( void )
ldap_free_urldesc LDAP_P((
	LDAPURLDesc *ludp ));

LDAP_F( int )
ldap_url_search LDAP_P((
	LDAP *ld,
	LDAP_CONST char *url,
	int attrsonly ));

LDAP_F( int )
ldap_url_search_s LDAP_P((
	LDAP *ld,
	LDAP_CONST char *url,
	int attrsonly,
	LDAPMessage **res ));

LDAP_F( int )
ldap_url_search_st LDAP_P((
	LDAP *ld,
	LDAP_CONST char *url,
	int attrsonly,
	struct timeval *timeout,
	LDAPMessage **res ));

/*
 * in digestmd5.c
 */

typedef struct digestMD5ctx LDAP_DIGEST_MD5_CONTEXT;

LDAP_F(int)
ldap_bind_digest_md5_start LDAP_P(( 
    LDAP                      *ld,
    LDAP_DIGEST_MD5_CONTEXT   **pMD5Context));

LDAP_F(int)
ldap_get_digest_md5_realms LDAP_P((
	LDAP_DIGEST_MD5_CONTEXT		*MD5context,
	char                        ***realms));


/* defines for the abortFlag parameter */
#define LDAP_DIGEST_MD5_FINISH  1
#define LDAP_DIGEST_MD5_ABORT   2

LDAP_F( int )
ldap_bind_digest_md5_finish LDAP_P(( 
	LDAP_DIGEST_MD5_CONTEXT	  **pMD5context,
	char 					  *authID,
	char   					  *password,
    int                       passwordLen,
	int                       realmIndex,
	int                       abortFlag));

/*
 * in nmas.c
 */

LDAP_F(int)
ldap_bind_nmas_s(
	LDAP				*ld,
	LDAP_CONST char  	*dn,
	LDAP_CONST char 	*password,
	LDAP_CONST char 	*reqSequence,
	LDAP_CONST char 	*reqClearance);

LDAP_F(int)
ldap_bind_nmas_ex_s(
	void			*guiHandle,
	LDAP			*ld,
	LDAP_CONST char  	*dn,
	LDAP_CONST char 	*password,
	LDAP_CONST char 	*reqSequence,
	LDAP_CONST char 	*reqClearance);

LDAP_F( int )
ldap_nmas_get_errcode( void );
/* 
 * in sortctrl.c  
 */
/*
 * structure for a sort-key 
 */
typedef struct ldapsortkey {
	char *  attributeType;
	char *  orderingRule;
	int     reverseOrder;
} LDAPSortKey;

LDAP_F( int )
ldap_create_sort_keylist LDAP_P((
	LDAPSortKey ***sortKeyList,
	char        *keyString ));

LDAP_F( void )
ldap_free_sort_keylist LDAP_P((
	LDAPSortKey **sortkeylist ));



LDAP_F( int )
ldap_create_sort_control LDAP_P(( 	
     LDAP *ld, 
     LDAPSortKey **keyList,
     int ctl_iscritical,
     LDAPControl **ctrlp));

LDAP_F( int )
ldap_parse_sort_control LDAP_P((  
      LDAP           *ld, 
      LDAPControl    **ctrlp,  
      unsigned long  *result,
      char           **attribute));

/*
 * in psearchctrl.c
 */
#define LDAP_CHANGETYPE_ADD		1
#define LDAP_CHANGETYPE_DELETE		2
#define LDAP_CHANGETYPE_MODIFY		4
#define LDAP_CHANGETYPE_MODDN		8
#define LDAP_CHANGETYPE_ANY		(1|2|4|8)
LDAP_F( int )
ldap_create_persistentsearch_control LDAP_P((
    LDAP        *ld, 
    int         changeTypes, 
    int         changesOnly, 
    int         returnEchgCtls, 
    char        isCritical, 
    LDAPControl **ctrlp ));

LDAP_F( int )
ldap_parse_entrychange_control LDAP_P(( 
    LDAP        *ld, 
    LDAPControl **ctrls, 
    int         *changeType, 
    char        **prevDN, 
    int         *hasChangeNum, 
    long        *changeNum ));

/* 
 * in vlvctrl.c  
 */

/*
 * structure for virtul list.
 */
typedef struct ldapvlvinfo {
	int             ldvlv_version;
    unsigned long   ldvlv_before_count;      
    unsigned long   ldvlv_after_count;                     
    unsigned long   ldvlv_offset;              
    unsigned long   ldvlv_count;
    struct berval  *ldvlv_attrvalue;
    struct berval  *ldvlv_context;
    void           *ldvlv_extradata;
} LDAPVLVInfo;

LDAP_F( int ) 
ldap_create_vlv_control LDAP_P((
	LDAP *ld, 
	LDAPVLVInfo *ldvlistp,
	LDAPControl **ctrlp ));

LDAP_F( int )
ldap_parse_vlv_control LDAP_P(( 
	LDAP          *ld, 
	LDAPControl   **ctrls,
	unsigned long *target_posp, 
	unsigned long *list_countp, 
	struct berval **contextp,
	int           *errcodep ));


/*  These structures and functions are defined in 
    Schema_util.h and implemented in Schema_util.c */

/* structures */
typedef struct ldap_schema_element LDAPSchemaElement;

typedef struct ldap_schema LDAPSchema;

/* Holds field names and values of an attribute definition. External */
typedef struct ldap_schema_mod {
	int op;  			/* same type define as LDAPMod: 
						LDAP_MOD_ADD, LDAP_MOD_REPLACE, LDAP_MOD_DELETE */
	char *fieldName;	/* name of the field to update or add*/
	char **values;		/* Values of the field.  */

} LDAPSchemaMod;


/* macros */

#define LDAP_SCHEMA_ATTRIBUTE_TYPE      0
#define LDAP_SCHEMA_OBJECT_CLASS        1
#define LDAP_SCHEMA_SYNTAX              2
#define LDAP_SCHEMA_MATCHING_RULE       3
#define LDAP_SCHEMA_MATCHING_RULE_USE   4
#define LDAP_SCHEMA_NAME_FORM           5
#define LDAP_SCHEMA_DIT_CONTENT_RULE    6
#define LDAP_SCHEMA_DIT_STRUCTURE_RULE  7

/****************************************************************/
/*   Table of fields for SchemaElements (repeats are commented) */
/*                                                              */
/*      NAME                              Value                 */
/*--------------------------------------------------------------*/
/*     All types, except DITStructureRule does not have an OID  */
    #define     LDAP_SCHEMA_OID             "OID"  
    #define     LDAP_SCHEMA_DESCRIPTION     "DESC"
    /*     All but LDAPSchemaSyntax  */               
    #define     LDAP_SCHEMA_NAMES           "NAME"
  /*#define    LDAP_SCHEMA_VALUE                                */
    #define     LDAP_SCHEMA_OBSOLETE        "OBSOLETE"
    
    /*     LDAPSchemaAttributeType   */
    #define     LDAP_SCHEMA_SUPERIOR        "SUP"
    #define     LDAP_SCHEMA_EQUALITY        "EQUALITY"
    #define     LDAP_SCHEMA_ORDERING        "ORDERING"
    #define     LDAP_SCHEMA_SUBSTRING       "SUBSTR"
    #define     LDAP_SCHEMA_SYNTAX_OID      "SYNTAX"
    #define     LDAP_SCHEMA_SINGLE_VALUED   "SINGLE-VALUE"
    #define     LDAP_SCHEMA_COLLECTIVE      "COLLECTIVE"
    #define     LDAP_SCHEMA_NO_USER_MOD     "NO-USER-MODIFICATION"            
    #define     LDAP_SCHEMA_USAGE           "USAGE"
    
    /*     LDAPSchemaObjectClass                                */
    #define     LDAP_SCHEMA_SUPERIORS       "SUP"
    #define     LDAP_SCHEMA_MUST_ATTRIBUTES "MUST"
    #define     LDAP_SCHEMA_MAY_ATTRIBUTES  "MAY"
    
    #define     LDAP_SCHEMA_TYPE_ABSTRACT   "ABSTRACT"
    #define     LDAP_SCHEMA_TYPE_STRUCTURAL "STRUCTURAL"
    #define     LDAP_SCHEMA_TYPE_AUXILIARY  "AUXILIARY"
    
    /*     LDAPSchemaMatchingRule                               */    
    #define     LDAP_SCHEMA_SYNTAX_OID      "SYNTAX"
    
    /*     LDAPSchemaMatchingRuleUse                            */    
    #define     LDAP_SCHEMA_APPLIES         "APPLIES"
    
    /*     LDAPSchemaNameForm                                   */
    #define     LDAP_SCHEMA_NAME_FORM_OBJECT   "OC"
  /*#define     LDAP_SCHEMA_MUST_ATTRIBUTES "MUST"      required
    #define     LDAP_SCHEMA_MAY_ATTRIBUTES  "MAY"               */
    
    /*     LDAPSchemaSyntax                                     */
  /*#define     LDAP_SCHEMA_OID             "OID"               */
  /*#define     LDAP_SCHEMA_DESCRIPTION     "DESC"              */
    
    /*     LDAPSchemaDITContentRule                             */
    #define     LDAP_SCHEMA_AUX_CLASSES     "AUX"
  /*#define     LDAP_SCHEMA_MUST_ATTRIBUTES "MUST"
    #define     LDAP_SCHEMA_MAY_ATTRIBUTES  "MAY"               */
    #define     LDAP_SCHEMA_NOT_ATTRIBUTES  "NOT"
    
    /*     LDAPSchemaDITStructureRule : Does not contain OID    */
    #define     LDAP_SCHEMA_RULE_ID         "RULEID" 
    #define     LDAP_SCHEMA_NAME_FORM_OID   "FORM"
  /*#define     LDAP_SCHEMA_SUPERIORS       "SUP"               */


   
/* for use with the field LDAP_SCHEMA_USAGE */
#define LDAP_SCHEMA_USER_APP         "userApplications"
#define LDAP_SCHEMA_DIRECTORY_OP     "directoryOperation"
#define LDAP_SCHEMA_DISTRIBUTED_OP   "distributedOperation"
#define LDAP_SCHEMA_DSA_OP           "dSAOperation"



/****************************************************************
Functions to init and free the local copy of Directory schema:
*/

	/* Fetches directory schema and stores it in a local read-only 
        copy. */
    LDAP_F( int )
    ldap_schema_fetch LDAP_P(( 
            struct ldap *ld, 
            struct ldap_schema **schema,
            const char* subschemaSubentryDN ));

	/* Frees memory allocated to it from ldap_schema_fetch */
    LDAP_F( int )
    ldap_schema_free LDAP_P(( 
            struct ldap_schema *schema));



/****************************************************************
Locating individual schema elements:
*/

	/* Retrieves the schema element according to name and type */
    LDAP_F( int )
    ldap_schema_get_by_name  LDAP_P((
            struct ldap_schema* schema, 
            char* name, 
            int elementType, 
            struct ldap_schema_element **element));

	/* 	Retrieves the schema element according to an index and type */
    LDAP_F( int )
    ldap_schema_get_by_index LDAP_P((
            struct ldap_schema* schema, 
            int index, 
            int elementType, 
            struct ldap_schema_element **element));

	/* 	Returns the number of elements of a type.  This is to be used 
        in conjunction with get_by_index */
    LDAP_F( int )
    ldap_schema_get_count LDAP_P((
            struct ldap_schema* schema, 
            int elementType));



/*****************************************************************
Extracting information from schema elements: 
*/

    /*  Retrieves a list of the fields defined for this schema element */
    LDAP_F( int )
    ldap_schema_get_field_names LDAP_P(( 
	    LDAPSchemaElement *schema, 
	    char ***names ));

    /*  Retrieves a list of values for the specified field in this schema 
	    element */
    LDAP_F( int )
    ldap_schema_get_field_values LDAP_P(( 
	    LDAPSchemaElement *schema, 
	    char *name, 
	    char ***values ));

    /* Use ldap_value_free for names and values */



/*****************************************************************
Changing schema definitions 
*/

    /* 	Deletes the schema element definition identifed by the string
	    name_oid from the LDAPSchema structure.  If a Name is given 
        then the type is used to identify which element type the 
        element is to be deleted from. */
    LDAP_F( int )
    ldap_schema_delete LDAP_P((
	    LDAPSchema *schema, 
	    int type,
        char *name_oid));

    /*	Modifies the fields of a schema element.  Each mod structure in 
        the fieldsToChange array will have a flag to specify a delete, 
	    replace or add.  The schema element is identified by name_oid.
	    The change is reflected in the LDAPSchema structure */
    LDAP_F( int )
    ldap_schema_modify LDAP_P((
	    LDAPSchema *schema, 
	    char* name_oid,
	    int type, 
	    struct ldap_schema_mod *fieldsToChange[]));

    /*	Adds a new schema element definition to the LDAPSchema structure */
    LDAP_F( int )
    ldap_schema_add LDAP_P((
	    LDAPSchema *schema, 
	    int type, 
	    struct ldap_schema_mod *fields[]));

    /*  Saves any changes made in the LDAPSchema structure from the 
        functions ldap_schema_add, ldap_schema_modify, and 
        ldap_schema_delete. If the DN of the subschemaSubentry to use is 
        NULL, the first Subentry advertized in the Root DSE will be used. 
        (LDAPv3 must be set to read the root DSE.) */
        
    LDAP_F( int )
    ldap_schema_save LDAP_P((
        LDAP *ld, 
        LDAPSchema *schema, 
        const char *subschemaSubentryDN ));

/* 
 * in cancel.c  
 */

/*
 * functions for cancel operation.
 */
LDAP_F(int)
ldap_cancel_ext(
	LDAP            *ld,
	int             cancelid,
	LDAPControl     **sctrls,
	LDAPControl     **cctrls,
	int             *msgidp );

LDAP_F(int)
ldap_cancel_ext_s(
	LDAP            *ld,
	int             cancelid,
	LDAPControl     **sctrls,
	LDAPControl     **cctrls );

/* 
 * in geteffectivecntrl.c  
 */

/*
 * functions for eprvcntrl.c privilege control.
 */

typedef struct ldapgetprvinfo {
	char           *attribute; 
	char           *classname;    
} LDAPGetprvInfo;             
 

LDAP_F( int )                                                 
ldap_create_geteffective_control LDAP_P((
	LDAP		*ld,                 
	LDAPGetprvInfo	**getprvinfo,
	int		efPrvvalue,                 
	int		isCritical,                 
	LDAPControl	**ctrlp ));

/*
 * functions for sstatctrl.c control.
 */

typedef struct ldapsstatctrl {
	int	statwith_ent;
	int	statwith_ref;
	int	waitdone_ms;
	int	ret_exam;
	int	ret_pass;
	int	ret_done;
	int	ret_ava;
} LDAPSstatCtrl;
 
LDAP_F( int )
ldap_create_sstatus_control LDAP_P((
	LDAP		*ld,
	LDAPSstatCtrl	*sstatctrl,
	int		isCritical,
	LDAPControl	**ctrlp ));

LDAP_F( int )
ldap_parse_sstatus_control LDAP_P((
	LDAP		*ld,
	LDAPControl	**ctrls,
	int		*numEax,
	int		*numPass,
	int		*evaDone,
	int		*numAva ));

/*
 * functions for referencectrl.c control.
 */

/* reference type */
#define LDAP_REF_SUPERIOR			((ber_int_t) 0x0000)
#define LDAP_REF_SUBORDINATE			((ber_int_t) 0x0001)
#define LDAP_REF_CROSS				((ber_int_t) 0x0002)
#define LDAP_REF_NON_SPECIFIC_SUBORDINATE	((ber_int_t) 0x0003)
#define LDAP_REF_SUPLIER			((ber_int_t) 0x0004)
#define LDAP_REF_MASTER				((ber_int_t) 0x0005)
#define LDAP_REF_IMMEDIATESUPERIOR		((ber_int_t) 0x0006)
#define LDAP_REF_SELF				((ber_int_t) 0x0007)


LDAP_F( int )
ldap_create_reference_control LDAP_P((
	LDAP		*ld,
	int		isCritical,
	LDAPControl	**ctrlp ));

LDAP_F( int )
ldap_parse_reference_control LDAP_P((
	LDAP		*ld,
	LDAPControl	**ctrls,
	char		*locRef,
	int		refType,
	char		*remainingName,
	int		scope,
	char		**searchedSubtrees,
	char		*failedName));

LDAP_END_DECL
#endif /* _LDAP_H */

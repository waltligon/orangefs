/* $Novell: ldap_ssl.h,v 1.32 2003/05/13 13:52:32 $ 
 *****************************************************************************
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
 *****************************************************************************/
#if !defined(LDAP_SSL_H)
#define LDAP_SSL_H

#include <ldap.h>


#ifdef __cplusplus
extern "C" {
#endif

#define LDAPSSL_SUCCESS          0
#define LDAPSSL_ERROR           -1

#define LDAPSSL_VERIFY_NONE   0x00
#define LDAPSSL_VERIFY_SERVER 0x01

#define LDAPSSL_FILETYPE_B64     1
#define LDAPSSL_FILETYPE_DER     2
#define LDAPSSL_BUFFTYPE_B64     3
#define LDAPSSL_BUFFTYPE_DER     4
#define LDAPSSL_FILETYPE_P12     5
#define LDAPSSL_BUFFTYPE_P12     6

#define LDAPSSL_CERT_FILETYPE_B64     LDAPSSL_FILETYPE_B64
#define LDAPSSL_CERT_FILETYPE_DER     LDAPSSL_FILETYPE_DER
#define LDAPSSL_CERT_BUFFTYPE_B64     LDAPSSL_BUFFTYPE_B64
#define LDAPSSL_CERT_BUFFTYPE_DER     LDAPSSL_BUFFTYPE_DER

#define LDAPSSL_CERT_ATTR_ISSUER            1
#define LDAPSSL_CERT_ATTR_SUBJECT           2
#define LDAPSSL_CERT_ATTR_VALIDITY_PERIOD   3

#define LDAPSSL_CERT_GET_STATUS           100

/*
 * Certificate Status
 */
/* unable to get issuer certificate */
#define UNABLE_TO_GET_ISSUER_CERT           2
/* unable to decode issuer public key */
#define UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY  6
/* certificate signature failure */
#define CERT_SIGNATURE_FAILURE              7
/* certificate is not yet valid */
#define CERT_NOT_YET_VALID                  9	
/* CRL is not yet valid */
#define CERT_HAS_EXPIRED                   10
/* format error in certificate's notBefore field */
#define ERROR_IN_CERT_NOT_BEFORE_FIELD     13
/* format error in certificate's notAfter field */
#define ERROR_IN_CERT_NOT_AFTER_FIELD      14
/* self signed certificate */
#define DEPTH_ZERO_SELF_SIGNED_CERT        18
/* self signed certificate in certificate chain */
#define SELF_SIGNED_CERT_IN_CHAIN          19
/* unable to get local issuer certificate */
#define UNABLE_TO_GET_ISSUER_CERT_LOCALLY  20
/* unable to verify the first certificate */
#define UNABLE_TO_VERIFY_LEAF_SIGNATURE    21
/* invalid CA certificate */
#define INVALID_CA                         24
/* path length constraint exceeded */
#define PATH_LENGTH_EXCEEDED               25
/* unsupported certificate purpose */
#define INVALID_PURPOSE                    26
/* certificate not trusted */
#define CERT_UNTRUSTED                     27
/* certificate rejected */
#define CERT_REJECTED                      28


#define LDAPSSL_CERT_UTC_TIME 1
#define LDAPSSL_CERT_GEN_TIME 2

#define LDAPSSL_CERT_ACCEPT 0
#define LDAPSSL_CERT_REJECT -1

typedef struct _LDAPSSL_Private_Key
{
    unsigned long    length;
    void            *data;

} LDAPSSL_Private_Key, *pLDAPSSL_Private_Key;

typedef struct _LDAPSSL_Cert
{
    unsigned long    length;
    void            *data;

} LDAPSSL_Cert, *pLDAPSSL_Cert;

typedef struct _LDAPSSL_Cert_Validity_Period
{
   char  notBeforeTime[40];
   int   notBeforeType;
   char  notAfterTime[40];
   int   notAfterType;

} LDAPSSL_Cert_Validity_Period, *pLDAPSSL_Cert_Validity_Period;


/* APIs */
LDAP_F(int) ldapssl_client_init
(
   const char *certFile,
   void *reserved
);

LDAP_F(LDAP *) ldapssl_init
(
   const char *defhost,
   int defport,
   int defsecure
);

LDAP_F(int) ldapssl_install_routines
(
   LDAP *ld
);

LDAP_F(int) ldapssl_client_deinit
(
   void
);

LDAP_F(int) ldapssl_add_trusted_cert
(
   void *cert,
   int   type
);

LDAP_F(int) ldapssl_set_verify_mode
(
   int   mode
);

LDAP_F(int) ldapssl_get_verify_mode
(
   int   *mode
);

LDAP_F(int) ldapssl_set_verify_callback
(
   int (LIBCALL *certVerifyFunc)(void *)
);

LDAP_F(int) ldapssl_get_cert_attribute
(
   void          *certHandle,
   int           attrID,
   void          *value,
   int           *length
);

LDAP_F(int) ldapssl_get_cert
(
   void          *certHandle,
   int            type,
   LDAPSSL_Cert  *cert
);

LDAP_F(int) ldapssl_set_client_private_key
(
   void *key,
   int   type,
   void *password
);

LDAP_F(int) ldapssl_set_client_cert
(
   void *cert,
   int   type,
   void *password
);

LDAP_F(int) ldapssl_start_tls
(
   LDAP *ld
);

LDAP_F(int) ldapssl_stop_tls
(
   LDAP *ld
);

#ifdef __cplusplus
}
#endif

#endif /* !defined(LDAP_SSL_H) */

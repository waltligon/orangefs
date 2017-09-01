/*                                                              
 * Copyright (C) 2012 Clemson University and Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 *
 * Certificate declarations                                                                
 *
*/

#ifndef _PINT_CERT_H_
#define _PINT_CERT_H_

#include <openssl/x509.h>

int PINT_init_trust_store(void);

int PINT_add_trusted_certificate(X509 *cert);

void PINT_cleanup_trust_store(void);

int PINT_verify_certificate(X509 *cert);


/* int PINT_verify_cert_trust(X509 *cert); */

#endif

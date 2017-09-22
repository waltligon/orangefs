/*
 * (C) 2013 Clemson University and Omnibond Systems LLC
 *
 * Certificate cache functions
 *
 * See COPYING in top-level directory.
 */

#include "pvfs2-config.h"

#ifdef ENABLE_CERTCACHE

#include <malloc.h>
#include <string.h>

#include <openssl/x509.h>
#include <openssl/asn1.h>

#include "certcache.h"
#include "pvfs2-debug.h"
#include "server-config.h"
#include "security-util.h"
#include "cert-util.h"
#include "pint-util.h"
#include "pvfs2-internal.h"
#include "murmur3.h"
#include "gossip.h"
#include "llist.h"

/* error-checking macros */
#define CHECK_RET(__err)    if ((__err)) return (__err)

#define CHECK_NULL_RET_NULL(__val)    if ((__val) == NULL) return NULL
                                           
/* global variables */
seccache_t * certcache = NULL;

/* certificate cache methods */
static void PINT_certcache_set_expired(seccache_entry_t *entry, 
                                       PVFS_time timeout);
static uint16_t PINT_certcache_get_index(void *data, 
                                         uint64_t hash_limit);
static int PINT_certcache_compare(void *data, 
                                  void *entry);
static void PINT_certcache_cleanup(void *entry);
static void PINT_certcache_debug(const char *prefix, 
                                 void *data);

static seccache_methods_t certcache_methods = {
    PINT_seccache_expired_default,
    PINT_certcache_set_expired,
    PINT_certcache_get_index,
    PINT_certcache_compare,
    PINT_certcache_cleanup,
    PINT_certcache_debug
};

/* certificate cache helper functions */
static void get_cert_subject(const PVFS_certificate *cert,
                             char *subject);
static ASN1_UTCTIME *get_expiration(const PVFS_certificate *cert);
static certcache_data_t *PINT_certcache_new_data(const PVFS_certificate *cert,
                                                 PVFS_uid uid,
                                                 uint32_t num_groups,
                                                 PVFS_gid *group_array);


/* END of internal-only certificate cache functions */

/* get_cert_subject
 * 
 * Get subject of certificate
 */
static void get_cert_subject(const PVFS_certificate *cert,
                             char *subject)
{
    X509 *xcert = NULL;
    X509_NAME *xsubject;

    subject[0] = '\0';

    if (PINT_cert_to_X509(cert, &xcert) != 0)
    {
        return;
    }

    xsubject = X509_get_subject_name(xcert);
    if (xsubject == NULL)
    {
        X509_free(xcert);
        return;
    }

    X509_NAME_oneline(xsubject, subject, CERTCACHE_SUBJECT_SIZE);
    subject[CERTCACHE_SUBJECT_SIZE-1] = '\0';

    X509_free(xcert);
}

/* get_expiration 
 * 
 * Return expiration time of entry -- return 0 on error
 * or if cert is within the security timeout of expiration
 */
static ASN1_UTCTIME *get_expiration(const PVFS_certificate *cert)
{
    X509 *xcert = NULL;
    ASN1_UTCTIME *certexp;    

    if (PINT_cert_to_X509(cert, &xcert) != 0)
    {
        return 0;
    }

    certexp = M_ASN1_UTCTIME_dup(X509_get_notAfter(xcert));

    X509_free(xcert);

    return certexp;
}

/** PINT_certcache_new_data
 *  Creates a certificate cache data object.
 */
static certcache_data_t *PINT_certcache_new_data(
    const PVFS_certificate * cert,
    PVFS_uid uid,
    uint32_t num_groups,
    PVFS_gid * group_array)
{
    certcache_data_t *data = NULL;
    char subject[CERTCACHE_SUBJECT_SIZE];

    CHECK_NULL_RET_NULL(cert);

    data = (certcache_data_t *) malloc(sizeof(certcache_data_t));
    CHECK_NULL_RET_NULL(data);

    /* copy subject */
    get_cert_subject(cert, subject);

    strncpy(data->subject, subject, sizeof(subject));

    /* copy other fields */
    data->uid = uid;
    data->num_groups = num_groups;
    if (num_groups > 0)
    {
        /* allocate group array -- free in cleanup */
        data->group_array = (PVFS_gid *) malloc(num_groups * sizeof(PVFS_gid));
        if (data->group_array == NULL)
        {
            free(data);
            return NULL;
        }
        memcpy(data->group_array, group_array, num_groups * sizeof(PVFS_gid));
    }
    else
    {
        data->num_groups = 0;
    }

    data->expiration = get_expiration(cert);

    return data;
}

/* PINT_certcache_set_expired 
 * Set expiration of entry based on certificate expiration stored in 
 * certcache data
 */
static void PINT_certcache_set_expired(seccache_entry_t *entry,
                                       PVFS_time timeout)
{    
    certcache_data_t *data = (certcache_data_t *) entry->data;

    /* set expiration to now + supplied timeout */
    entry->expiration = time(NULL) + timeout;

    /* expire entry if cert expiration passed or within timeout */
    if (ASN1_UTCTIME_cmp_time_t(data->expiration, entry->expiration) == -1)
    {
        entry->expiration = 0;
    }
}

/*****************************************************************************/
/** PINT_certcache_get_index
 * Determine the hash table index based on certificate subject.
 * Returns index of hash table.
 */
static uint16_t PINT_certcache_get_index(void * data,
                                         uint64_t hash_limit)
{    
    uint32_t seed = 42;
    uint64_t hash1[2] = { 0, 0};
    uint64_t hash2[2] = { 0, 0};
    uint16_t index = 0;
    char *subject = ((certcache_data_t *) data)->subject;

    /* Hash certificate subject */
    MurmurHash3_x64_128((const void *) subject,
        strlen(subject), seed, hash2);
    hash1[0] += hash2[0];
    hash1[1] += hash2[1];

    index = (uint16_t) (hash1[0] % hash_limit);

    return index;
}

/** PINT_certcache_compare
 * Compare certificate subjects
 */
static int PINT_certcache_compare(void * data,
                                  void * entry)
{
    certcache_data_t *certdata = (certcache_data_t *) data;
    seccache_entry_t *pentry = (seccache_entry_t *) entry;

    /* ignore chain end marker */
    if (pentry->data == NULL)
    {
        return 1;
    }

    /* compare subjects */
    return strcmp(certdata->subject, 
                  ((certcache_data_t *) pentry->data)->subject);
}

/** PINT_certcache_cleanup_data
 * Frees certcache_data_t struct.
 */
static void PINT_certcache_cleanup_data(certcache_data_t *certdata)
{
    if (certdata->expiration != NULL)
    {
        M_ASN1_UTCTIME_free(certdata->expiration);
    }
    if (certdata->group_array != NULL)
    {
        free(certdata->group_array);
        certdata->group_array = NULL;
    }

    free(certdata);        
}

/** PINT_certcache_cleanup()
 * Free cache entry method
 */
static void PINT_certcache_cleanup(void *entry)
{
    if (entry != NULL)
    {
        if (((seccache_entry_t *) entry)->data != NULL)
        {
            PINT_certcache_cleanup_data((certcache_data_t *) 
                                        ((seccache_entry_t *) entry)->data);
            ((seccache_entry_t *)entry)->data = NULL;
        }
        free(entry);
        entry = NULL;
    }
}

/* PINT_certcache_debug_certificate
 *
 * Outputs the subject of a certificate.
 * prefix should typically be "caching" or "removing".
 */
static void PINT_certcache_debug(const char *prefix,
                                 void *data)
{
    certcache_data_t *certdata = (certcache_data_t *) data;

    gossip_debug(GOSSIP_SECCACHE_DEBUG, "%s certificate:\n", prefix);
    gossip_debug(GOSSIP_SECCACHE_DEBUG, "\tsubject: %s\n", certdata->subject);
}

/** Initializes the certificate cache.
 * Returns 0 on success.
 * Returns negative PVFS_error on failure.
 */
int PINT_certcache_init(void)
{
    struct server_configuration_s *config = PINT_get_server_config();

    gossip_debug(GOSSIP_SECURITY_DEBUG, "Initializing certificate cache...\n");

    certcache = PINT_seccache_new("Certificate", &certcache_methods, 0);
    if (certcache == NULL)
    {
        return -PVFS_ENOMEM;
    }

    /* Set timeout */
    PINT_seccache_set(certcache, SECCACHE_TIMEOUT, config->certcache_timeout);

    return 0;
}

/** Releases resources used by the certificate cache.
 * Returns 0 on success.
 * Returns negative PVFS_error on failure.
 */
int PINT_certcache_finalize(void)
{
    gossip_debug(GOSSIP_SECURITY_DEBUG, "Finalizing certificate cache...\n");

    PINT_seccache_cleanup(certcache);

    return 0;
}

/** Lookup
 * Returns pointer to certificate cache entry on success.
 * Returns NULL on failure.
 */
seccache_entry_t * PINT_certcache_lookup(PVFS_certificate * cert)
{
    certcache_data_t *data;
    seccache_entry_t *entry;
    
    data = PINT_certcache_new_data(cert, 0, 0, NULL);
    CHECK_NULL_RET_NULL(data);

    entry = PINT_seccache_lookup(certcache, data);

    PINT_certcache_cleanup_data(data);

    return entry;
}

/** PINT_certcache_insert
 * Inserts 'cert' into the certificate cache.
 * Returns 0 on success; otherwise returns -PVFS_error.
 */
int PINT_certcache_insert(const PVFS_certificate * cert, 
                          PVFS_uid uid,
                          uint32_t num_groups,
                          PVFS_gid * group_array)
{
    certcache_data_t *data = NULL; 
    int ret;

    data = PINT_certcache_new_data(cert, uid, num_groups, group_array);
    if (data == NULL)
    {
        return -PVFS_ENOMEM;
    }

    ret = PINT_seccache_insert(certcache, data, sizeof(certcache_data_t));

    if (ret != 0)
    {
        PVFS_perror_gossip("PINT_certcache_insert: insert failed", ret);
    }

    return ret;
}

#endif /* ENABLE_CERTCACHE */

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
#include "server-config.h"
#include "security-util.h"
#include "cert-util.h"
#include "pint-util.h"
#include "pvfs2-internal.h"
#include "murmur3.h"
#include "gossip.h"
#include "pvfs2-debug.h"

/* error-checking macros */
#define CHECK_RET(__err)    if ((__err)) return (__err)

#define CHECK_NULL_RET_NULL(__val)    if ((__val) == NULL) return NULL
                                           
/* global variables */
seccache_t * certcache = NULL;

/* certificate cache methods */
static seccache_entry_t *PINT_certcache_new_entry(void * data);
static int PINT_certcache_expired(void * entry1,
                                  void * entry2);
static void PINT_certcache_set_expired(void * entry, PVFS_time timeout);
static uint16_t PINT_certcache_get_index(void * data, uint64_t hash_limit);
static int PINT_certcache_compare(void * data, 
                                  void * entry);
static void PINT_certcache_cleanup_entry(void * entry);
static void PINT_certcache_debug(const char * prefix, 
                                 void * entry);

/* certificate cache helper functions */
static void get_cert_subject(const PVFS_certificate * cert,
                             char * subject);
static ASN1_UTCTIME *get_expiration(const PVFS_certificate *cert);
static certcache_data_t *PINT_certcache_new_data(
    const PVFS_certificate * cert,
    PVFS_uid uid,
    uint32_t num_groups,
    PVFS_gid *group_array);


static seccache_methods_t certcache_methods = {
    PINT_certcache_new_entry,
    PINT_certcache_expired,
    PINT_certcache_set_expired,
    PINT_certcache_get_index,
    PINT_certcache_compare,
    PINT_certcache_cleanup_entry,
    PINT_certcache_debug
};
/* END of internal-only certificate cache functions */

/* get_cert_subject
 * 
 * Get subject of certificate
 */
static void get_cert_subject(const PVFS_certificate * cert,
                             char * subject)
{
    X509 *xcert = NULL;
    X509_NAME *xsubject;
    
    if (PINT_cert_to_X509(cert, &xcert) != 0)
    {
        return;
    }

    xsubject = X509_get_subject_name(xcert);
    if (xsubject == NULL)
    {
        return;
    }

    X509_NAME_oneline(xsubject, subject, CERTCACHE_SUBJECT_SIZE);
    subject[CERTCACHE_SUBJECT_SIZE-1] = '\0';
}

/* get_expiration 
 * 
 * Return expiration time of entry -- return 0 on error
 * or if cert is within the security timeout of expiration
 */
static ASN1_UTCTIME *get_expiration(const PVFS_certificate * cert)
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

/** Creates new entry and stores data */
static seccache_entry_t * PINT_certcache_new_entry(void * data)
{
    seccache_entry_t *entry = NULL;

    CHECK_NULL_RET_NULL(data);

    /* allocate entry */
    entry = (seccache_entry_t *) malloc(sizeof(seccache_entry_t));
    CHECK_NULL_RET_NULL(entry);

    /* assign certcache data */
    entry->data = (certcache_data_t *) data;    

    entry->data_size = sizeof(certcache_data_t);

    /* note: expiration set when inserted into cache */
    entry->expiration = 0xFFFFFFFF;

    return entry;
}

/** PINT_certcache_expired
 * Compares certcache entry's timeout to that of a 'dummy' entry with the
 * timeout set to the current time.
 * Returns 0 if entry 'e2' has expired; otherwise, returns 1.
 */
static int PINT_certcache_expired(void * entry1,
                                  void * entry2)
{
    if (((seccache_entry_t *) entry1)->expiration >=
        ((seccache_entry_t *) entry2)->expiration)
    {
        /* entry has expired */
        return 0;
    }
    return 1;
}

/* PINT_certcache_set_expired 
 * Set expiration of entry based on certificate expiration stored in 
 * certcache data
 */
static void PINT_certcache_set_expired(void * entry,
                                       PVFS_time timeout)
{
    seccache_entry_t *pentry = (seccache_entry_t *) entry;
    certcache_data_t *data = (certcache_data_t *) pentry->data;

    /* set expiration to now + supplied timeout */
    pentry->expiration = time(NULL) + timeout;

    /* expire entry if cert expiration passed or within timeout */
    if (ASN1_UTCTIME_cmp_time_t(data->expiration, pentry->expiration) == -1)
    {
        pentry->expiration = 0;
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
    uint32_t seed = 42; //Seed Murmur3
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

/** PINT_certcache_cleanup_entry()
 *  Frees allocated members of certcache_data_t and then frees
 *  the entry.
 */
static void PINT_certcache_cleanup_entry(void * entry)
{
    certcache_data_t *data = NULL;

    if (entry != NULL)
    {
        data = (certcache_data_t *) ((seccache_entry_t *) entry)->data;
        if (data != NULL)
        {
            if (data->expiration != NULL)
            {
                M_ASN1_UTCTIME_free(data->expiration);
            }
            if (data->group_array != NULL)
            {
                free(data->group_array);
                data->group_array = NULL;
            }
            free(entry);
        }
    }
}

/* PINT_certcache_debug_certificate
 *
 * Outputs the subject of a certificate.
 * prefix should typically be "caching" or "removing".
 */
static void PINT_certcache_debug(const char * prefix,
                                 void * data)
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
    struct server_configuration_s *config;

    config = PINT_get_server_config();

    gossip_debug(GOSSIP_SECURITY_DEBUG, "Initializing certificate cache...\n");

    /* TODO: locking */

    certcache = PINT_seccache_new("Certificate", 
                                  &certcache_methods,
                                  CERTCACHE_HASH_LIMIT);
    if (certcache == NULL)
    {
        return -PVFS_ENOMEM;
    }

    /* Set properties */
    PINT_seccache_set(certcache, SECCACHE_SIZE_LIMIT, CERTCACHE_SIZE_LIMIT);
    PINT_seccache_set(certcache, SECCACHE_TIMEOUT, config->security_timeout);
    PINT_seccache_set(certcache, SECCACHE_ENTRY_LIMIT, CERTCACHE_ENTRY_LIMIT);

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
    certcache_data_t *data = NULL; 
    /* seccache_entry_t *entry = NULL; */
    
    data = PINT_certcache_new_data(cert, 0, 0, NULL);
    CHECK_NULL_RET_NULL(data);

   /* entry = PINT_certcache_new_entry(data);
    CHECK_NULL_RET_NULL(entry); */

    return PINT_seccache_lookup(certcache, data);
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

    data = PINT_certcache_new_data(cert, uid, num_groups, group_array);
    if (data == NULL)
    {
        return -PVFS_ENOMEM;
    }

    return PINT_seccache_insert(certcache, data, sizeof(certcache_data_t));
}

/* Remove */
int PINT_certcache_remove(seccache_entry_t * entry)
{
    return PINT_seccache_remove(certcache, entry);
}

#endif /* ENABLE_CERTCACHE */

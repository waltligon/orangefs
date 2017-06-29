/*                                                              
 * Copyright (C) 2012 Clemson University and Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 *
 * LDAP identity-mapping functions                                                                
 *
*/

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#ifndef WIN32
#include <unistd.h>
#endif

#include <ldap.h>

#include <openssl/x509.h>
#include <openssl/err.h>

#include "pint-ldap-map.h"

#include "gossip.h"
#include "gen-locks.h"
#include "server-config.h"
#include "pint-security.h"
#include "cert-util.h"

/* global LDAP connection handle */
static LDAP *ldap = NULL;

/* LDAP handle mutex */
static gen_mutex_t ldap_mutex = GEN_MUTEX_INITIALIZER;

static void PINT_ldap_error(const char *msg,
                            int ret)
{

    /* log an LDAP error to gossip */
    gossip_err("LDAP error: %s (%d): %s\n", msg, ret, 
               ldap_err2string(ret));

}

/* load LDAP password from file */
static char *get_ldap_password(const char *path)
{
    FILE *f;
    char *buf;
    struct stat statbuf;

    /* check permissions */
    if (stat(path, &statbuf) != 0)
    {
        gossip_err("Could not stat LDAP password file %s: %s\n", path,
                   strerror(errno));
        return NULL;
    }
    /* check if group or other modes are set */
    if (statbuf.st_mode & (S_IRWXG|S_IRWXO))
    {
        gossip_err("Warning: LDAP password file %s has Group or Other "
                   "permissions enabled\n", path);
    }

    f = fopen(path, "r");
    if (f == NULL)
    {
        gossip_err("Error loading LDAP password file %s: %s\n", path,
                   strerror(errno));
        return NULL;
    }

    buf = (char *) malloc(128);
    if (buf == NULL)
    {
        return NULL;
    }

    fgets(buf, 128, f);
    if (strlen(buf) > 0 && buf[strlen(buf)-1] == '\n')
    {
        buf[strlen(buf)-1] = '\0';
    }

    fclose(f);

    return buf;
}

/* connect to configured LDAP server */
int PINT_ldap_initialize(void)
{
    char *passwd = NULL;
    int version, ret;
    struct server_configuration_s *config = PINT_get_server_config();

    /* lock the mutex */
    gen_mutex_lock(&ldap_mutex);
    
    if (config->ldap_hosts == NULL)
    {
        gossip_err("No LDAP hosts specified\n");
        return -PVFS_EINVAL;
    }

    /* connect to a server in the URI list */
    ret = ldap_initialize(&ldap, config->ldap_hosts);
    if (ret == LDAP_SUCCESS)
    {
        /* set the version */
        version = LDAP_VERSION3;
        ldap_set_option(ldap, LDAP_OPT_PROTOCOL_VERSION, &version);

        if (config->ldap_bind_password)
        {
            /* load password from file */
            if (strncmp(config->ldap_bind_password, "file:", 4) == 0)
            {
                passwd = get_ldap_password(config->ldap_bind_password+5);
                if (passwd == NULL)
                {
                    return -PVFS_EINVAL;
                }
            }
            else
            {
                passwd = strdup(config->ldap_bind_password);
                if (passwd == NULL)
                {
                    return -PVFS_ENOMEM;
                }
            }
        }

        gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: connecting to LDAP using "
                     "server list \"%s\", user \"%s\"\n",
                     __func__, config->ldap_hosts, 
                     (config->ldap_bind_dn != NULL) ? config->ldap_bind_dn :
                         "anonymous");

        /* log in to the server
           note: dn and password may be NULL for anonymous bind */
        ret = ldap_simple_bind_s(ldap,
                                 config->ldap_bind_dn, 
                                 passwd);

        if (ret != LDAP_SUCCESS)
        {
            PINT_ldap_error("ldap_simple_bind_s", ret);
        }
    }
    else 
    {
        PINT_ldap_error("ldap_initialize", ret);
    }

    if (passwd)
    {
        free(passwd);
    }

    gen_mutex_unlock(&ldap_mutex);

    return ret;
}

/* return TRUE if string is numeric */
static int check_number(char *s)
{
    char *p = s;

    if (s == NULL || *s == '\0')
        return 0;

    while (*p)
    {
        if (!isdigit(*p++))
            return 0;
    }

    return 1;
}

/* Cert subject is a DN like:
      /C=US/ST=SC/O=Acme Inc/OU=Engineering/CN=bsmith
   This function parses out the CN
*/
static int parse_subject_cn(const char *subject, char *cn, int size)
{
    char *pmatch, *ps, *pcn;
    int count = 0;

    if (subject == NULL || cn == NULL || size == 0)
    {
        return -PVFS_EINVAL;
    }

    cn[0] = '\0';

    /* find CN term in subject */
    pmatch = strstr(subject, "/CN=");
    if (!pmatch)
    {
        pmatch = strstr(subject, "/cn=");
        if (!pmatch)
        {
            return -PVFS_EINVAL;
        }
    }

    /* copy CN, up to / */
    ps = pmatch + 4;
    pcn = cn;
    while (*ps && *ps != '/' && count < size-1)
    {
        *pcn++ = *ps++;
        count++;
    }

    cn[count] = '\0';

    return 0;
}

/* convert a certificate subject DN to an LDAP DN */
static int convert_dn(const char *subject, char *ldap_dn, int size)
{
    const char *ps, **segments;
    char ch, *pdn;
    int segcount = 0, seg; 

    if (subject == NULL || ldap_dn == NULL || size == 0 ||
        strlen(subject)+1 > size )
    {
        return -PVFS_EINVAL;
    }

    /* count number of segments in subject */
    ps = subject;
    while ((ch = *ps++))
    {
        if (ch == '/')
        {
            segcount++;
        }
    }

    /* allocate segment stack */
    segments = (const char **) malloc(sizeof(char *) * segcount);
    if (segments == NULL)
    {
        return -PVFS_ENOMEM;
    }

    /* set up pointers to segments */
    seg = 0;
    ps = subject;
    while ((ch = *ps++))
    {
        if (ch == '/') 
        {
            segments[seg++] = ps;
        }
    }

    /* build LDAP dn by reversing segments */
    seg = segcount;
    pdn = ldap_dn;
    while (seg) 
    {
        ps = segments[seg-1];
        /* append segment */
        while (*ps && *ps != '/')
        {
            *pdn++ = *ps++;
        }
        /* append comma */
        if (seg-1 > 0)
        {
            *pdn++ = ',';
        }
        seg--;
    }

    /* null-terminate */
    *pdn = '\0';

    free(segments);

    return 0;
}

/* use info from credential certificate to retrieve uid/groups */
int PINT_ldap_map_credential(PVFS_credential *cred,
                             PVFS_uid *uid,
                             uint32_t *num_groups,
                             PVFS_gid *group_array)
{
    struct server_configuration_s *config = PINT_get_server_config();
    X509 *xcert = NULL;
    X509_NAME *xsubject;
    char subject[512], *base = NULL, name[256],
         filter[512], *attrs[3], *attr_name, **values, *dn = NULL;
    int ret, scope = LDAP_SCOPE_SUBTREE, free_flag = 0, retries;
    struct timeval timeout = { 15, 0 };
    LDAPMessage *res, *entry;
    BerElement *ber;

    /* read subject from cert */
    ret = PINT_cert_to_X509(&cred->certificate, &xcert);
    PINT_SECURITY_CHECK_RET(ret, "could not convert internal cert\n");

    xsubject = X509_get_subject_name(xcert);
    PINT_SECURITY_CHECK_NULL(xsubject, PINT_ldap_map_user_exit,
                             "could not retrieve cert subject\n");

    X509_NAME_oneline(xsubject, subject, sizeof(subject));
    subject[sizeof(subject) - 1] = '\0';

    if (subject[0] == '\0')
    {
        
        ret = -PVFS_ESECURITY;
        PINT_SECURITY_CHECK(ret, PINT_ldap_map_user_exit, 
                            "no certificate subject\n");
    }

    gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: using subject %s\n", __func__,
                 subject);

    if (config->ldap_search_mode == PVFS2_LDAP_SEARCH_CN)
    {
        /* parse the CN */
        ret = parse_subject_cn(subject, name, sizeof(name));
        PINT_SECURITY_CHECK(ret, PINT_ldap_map_user_exit,
                            "cannot parse CN from certificate %s\n",
                            subject);
        
        /* set LDAP search parameters */
        scope = config->ldap_search_scope;

        /* may be null */
        base = config->ldap_search_root;

        /* construct the filter in the form 
           (&(objectClass={search-class})({naming-attr}={userid})) */

        snprintf(filter, sizeof(filter), "(&(objectClass=%s)(%s=%s))",
                 config->ldap_search_class,
                 config->ldap_search_attr,
                 name);
        filter[sizeof(filter) - 1] = '\0';

    }
    else /* config->ldap_search_mode == PVFS2_LDAP_SEARCH_DN */
    {
        /* allocate the base */
        base = (char *) malloc(strlen(subject) + 1);
        if (base == NULL){
            PINT_SECURITY_CHECK(-PVFS_ENOMEM, PINT_ldap_map_user_exit,
                                "out of memory\n");
        }
        free_flag = 1;

        /* convert the DN */
        ret = convert_dn(subject, base, strlen(subject) + 1);
        PINT_SECURITY_CHECK(ret, PINT_ldap_map_user_exit,
                            "cannot convert certificate DN: %d\n", ret);        

        scope = LDAP_SCOPE_BASE;

        strcpy(filter, "objectClass=*");
    }

    attrs[0] = config->ldap_uid_attr;
    attrs[1] = config->ldap_gid_attr;
    attrs[2] = NULL;

    retries = PVFS2_LDAP_RETRIES;

PINT_ldap_map_user_retry:

    if (config->ldap_search_timeout > 0)
    {
        timeout.tv_sec = config->ldap_search_timeout;
    }

    *uid = PVFS_UID_MAX;
    group_array[0] = PVFS_GID_MAX;

    gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: "
                 "ldap_search_ext_s(ldap, \"%s\", %d, "
                 "\"%s\", {\"%s\", \"%s\"}, 0, NULL, NULL, "
                 "%lu, 0, ...)\n",
                 __func__,
                 base,
                 scope,
                 filter,
                 attrs[0],
                 attrs[1],
                 timeout.tv_sec);

    /* search LDAP with the specified values */
    ret = ldap_search_ext_s(ldap, base, scope, filter, attrs, 0,
                            NULL, NULL, &timeout, 0, &res);

    if (ret == LDAP_SUCCESS)
    {
        if (res != NULL)
        {
            int count = ldap_count_entries(ldap, res);
            if (count > 1)
            {
                gossip_err("%s: LDAP warning: multiple entries for user %s\n",
                           __func__, name);
            }

            /* note: we only check the first entry */
            entry = ldap_first_entry(ldap, res);
            if (entry != NULL)
            {
                dn = ldap_get_dn(ldap, entry);
                gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: found LDAP user %s\n",
                             __func__, dn);

                attr_name = ldap_first_attribute(ldap, entry, &ber);
                while (attr_name != NULL)
                {
                    values = ldap_get_values(ldap, entry, attr_name);
                    if (values != NULL) 
                    {
                        if (!strcasecmp(attr_name, config->ldap_uid_attr)) 
                        {
                            if (check_number(values[0]))
                            {
                                *uid = atoi(values[0]);
                            }
                            else
                            {                                
                                gossip_err("LDAP error: user %s: attribute %s:"
                                           " not a number (%s)\n", dn,
                                             attr_name, values[0]);
                                ret = -PVFS_EINVAL;
                                ldap_memfree(attr_name);
                                break;
                            }
                        }
                        else if (!strcasecmp(attr_name, config->ldap_gid_attr))
                        {
                            if (check_number(values[0]))
                            {
                                *num_groups = 1;
                                group_array[0] = atoi(values[0]);
                            }
                            else
                            {
                                gossip_err("LDAP error: user %s: attribute %s:"
                                           " not a number (%s)\n", dn,
                                             attr_name, values[0]);
                                ret = -PVFS_EINVAL;
                                ldap_memfree(attr_name);
                                break;
                            }
                        }
                                    
                        ldap_value_free(values);
                    }
                    else
                    {
                        gossip_err("LDAP error: user %s: no values for "
                                   "LDAP attribute %s\n", dn, attr_name);
                        ldap_memfree(attr_name);
                        ret = -PVFS_EINVAL;
                        break;
                    }

                    ldap_memfree(attr_name);

                    attr_name = ldap_next_attribute(ldap, entry, ber);
                } /* while */

                ber_free(ber, 0);
            }
            else
            {
                ldap_get_option(ldap, LDAP_OPT_RESULT_CODE, &ret);
                gossip_err("LDAP error: user %s: no LDAP entries (%d)\n", name, ret);
                ret = -PVFS_ESECURITY;
            }

            ldap_msgfree(res);
        }
        else
        {
            gossip_err("LDAP error: user %s: no LDAP results\n", name);
            ret = -PVFS_ENOENT;
        }
    }
    else
    {
        PINT_ldap_error("ldap_search_ext_s", ret);

        /* Reconnect to LDAP and try again */
        if (--retries) 
        {
            gossip_err("LDAP search error: retrying...\n");

            PINT_ldap_finalize();

            PINT_ldap_initialize();

            goto PINT_ldap_map_user_retry;
        }

        ret = -PVFS_ESECURITY;
    }

PINT_ldap_map_user_exit:

    if (ret == 0 && dn != NULL && 
        (*uid == PVFS_UID_MAX || group_array[0] == PVFS_GID_MAX))
    {
        gossip_err("LDAP error: user %s: no value for %s or %s\n",
                   dn, config->ldap_uid_attr, config->ldap_gid_attr);
        ret = -PVFS_EACCES;
    }

    if (dn != NULL)
    {
        ldap_memfree(dn);
    }

    if (free_flag)
    {
        free(base);
    }

    if (xcert != NULL)
    {
        X509_free(xcert);
    }

    if (ret != 0)
    {
        gossip_debug(GOSSIP_SECURITY_DEBUG, 
                     "%s: error code %d: returning -PVFS_EACCES\n",
                     __func__, ret);
        ret = -PVFS_EACCES;
    }

    return ret;
}

int PINT_ldap_authenticate(const char *userid,
                           const char *password)
{
    struct server_configuration_s *config = PINT_get_server_config();
    LDAP *ldap2 = NULL;
    char *base = NULL, filter[512], *dn = NULL;
    int ldapret, ret = -PVFS_EINVAL, scope = LDAP_SCOPE_SUBTREE, version;
    struct timeval timeout = { 15, 0 };
    LDAPMessage *res, *entry;

    if (userid == NULL || strlen(userid) == 0 ||
        password == NULL)
    {
        gossip_err("%s: userid and/or password is NULL or blank\n", __func__);
        return -PVFS_EINVAL;
    }

    /* set LDAP search parameters */
    scope = config->ldap_search_scope;

    /* root container of search -- may be null */
    base = config->ldap_search_root;

    /* construct the filter in the form 
     (&(objectClass={search-class})({naming-attr}={userid})) */
    snprintf(filter, sizeof(filter), "(&(objectClass=%s)(%s=%s))",
             config->ldap_search_class,
             config->ldap_search_attr,
             userid);
    filter[sizeof(filter) - 1] = '\0';

    gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: "
                 "ldap_search_ext_s(ldap, \"%s\", %d, "
                 "\"%s\", NULL, 0, NULL, NULL, "
                 "%lu, 0, ...)\n",
                 __func__,
                 base,
                 scope,
                 filter,
                 timeout.tv_sec);

    /* search LDAP with the specified values */
    ldapret = ldap_search_ext_s(ldap, base, scope, filter, NULL, 0,
                            NULL, NULL, &timeout, 0, &res);

    gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: ldap_search_ext returned %d\n",
                 __func__, ldapret);

    if (ldapret == LDAP_SUCCESS)
    {
        if (res != NULL)
        {
            int count = ldap_count_entries(ldap, res);
            if (count > 1)
            {
                gossip_err("%s: LDAP warning: multiple entries for user %s\n",
                           __func__, userid);
            }

            /* note: we only check the first entry */
            entry = ldap_first_entry(ldap, res);
            if (entry != NULL)
            {
                dn = ldap_get_dn(ldap, entry);
                gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: found LDAP user %s\n",
                             __func__, dn);
            }
            else
            {
                /* no entries... return -PVFS_ENOENT */
                gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: no users found for "
                             "username %s\n", __func__, userid);
                ret = -PVFS_ENOENT;
            }
            ldap_msgfree(res);
        }
        else
        {
            gossip_err("%s: LDAP result buffer is null\n", __func__);
            ret = -PVFS_ESECURITY;
        }
    }
    else
    {
        PINT_ldap_error("LDAP_authenticate failed", ldapret);
        ret = -PVFS_ESECURITY;
    }

    /* login with dn / password */
    if (dn != NULL)
    {
        ldapret = ldap_initialize(&ldap2, config->ldap_hosts);
        if (ldapret == LDAP_SUCCESS)
        {
            /* set the version */
            version = LDAP_VERSION3;
            ldap_set_option(ldap2, LDAP_OPT_PROTOCOL_VERSION, &version);

            gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: authenticating to LDAP "
                         "using server list \"%s\", user \"%s\"\n",
                         __func__, config->ldap_hosts, dn);

            ldapret = ldap_simple_bind_s(ldap2,
                                     dn,
                                     password);

            if (ldapret == 0)
            {
                gossip_debug(GOSSIP_SECURITY_DEBUG, "%s: password ok\n",
                             __func__);

                ldap_unbind_ext_s(ldap2, NULL, NULL);
                ret = 0;                
            }
            else
            {
                PINT_ldap_error("ldap_simple_bind_s", ldapret);
                ret = -PVFS_EACCES;
            }
        }
        else
        {
            PINT_ldap_error("ldap_initialize", ldapret);
            ret = -PVFS_ESECURITY;
        }

        ldap_memfree(dn);
    }

    return ret;
}

/* close LDAP connection */
void PINT_ldap_finalize(void)
{
    gen_mutex_lock(&ldap_mutex);

    /* disconnect from the LDAP server */
    if (ldap != NULL)
    {
        ldap_unbind_ext_s(ldap, NULL, NULL);
    }

    ldap = NULL;

    gen_mutex_unlock(&ldap_mutex);
}


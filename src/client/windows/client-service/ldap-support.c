/* 
 * (C) 2010-2022 Omnibond Systems, LLC
 *
 * See COPYING in top-level directory.
 */
   
/* LDAP functions - OrangeFS credential (UID/GID) can be stored
 * in an LDAP directory. The system will search for the username
 * and locate attributes containing information. The details of the
 * search can be configured in the configuration file.
 */

#include <Windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <ldap.h>
#include <ldap_ssl.h>

#include "cred.h"
#include "ldap-support.h"

/* 15-second search timeout */
struct timeval timeout = {15, 0};

extern PORANGEFS_OPTIONS goptions;

/* initialize LDAP SSL */
int PVFS_ldap_init()
{
    int ret;

    ret = ldapssl_client_init(NULL, NULL);

    if (ret == 0)
        ret = ldapssl_set_verify_mode(LDAPSSL_VERIFY_NONE);

    return ret;
}

/* cleanup LDAP SSL */
void PVFS_ldap_cleanup()
{
    ldapssl_client_deinit();
}

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

int get_ldap_credential(char *user_name,
                        PVFS_credential *credential)
{
    LDAP *ld;
    int version, ret = -1, bind_ret = 0;
    char *bind_dn, *password, filter[384],
         *attrs[3], *attr_name, **values,
         error_msg[256];
    PVFS_uid uid;
    PVFS_gid gid;
    LDAPMessage *results, *entry;
    BerElement *ptr;

    client_debug("   get_ldap_credential: enter\n");

    if (user_name == NULL || strlen(user_name) == 0 || 
        credential == NULL)
    {
        return -PVFS_EINVAL;
    }

    /* if system user return root credential */
    if (!stricmp(user_name, "SYSTEM"))
    {
        return get_system_credential(credential);
    }

    /* connect to LDAP - this will not be encrypted if
       secure is not set */    
    ld = ldapssl_init(goptions->ldap.host, goptions->ldap.port, 
                      goptions->ldap.secure);
    if (ld == NULL)
    {
        _snprintf(error_msg, sizeof(error_msg), "User %s: could not initialize "
            "LDAP", user_name);
        report_error(error_msg, -PVFS_ECONNREFUSED);
        goto get_ldap_credential_exit;
    }

    /* set the version */
    version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

    /* connect to the LDAP host */
    if (strlen(goptions->ldap.bind_dn) > 0)
    {
        bind_dn = goptions->ldap.bind_dn;
        password = goptions->ldap.bind_password;
    }
    else 
    {
        /* anonymous bind */
        bind_dn = password = NULL;
    }

    bind_ret = ldap_simple_bind_s(ld, bind_dn, password);
    if (bind_ret != 0)
    {
        _snprintf(error_msg, sizeof(error_msg), "User %s: could not bind to "
            "LDAP server: %s (%d)", user_name, ldap_err2string(bind_ret), bind_ret);
        report_error(error_msg, 0);
        goto get_ldap_credential_exit;
    }
    
    /* construct the filter in the form 
       (&(objectClass={search-class})({naming-attr}={user_name})) 
       */
    _snprintf(filter, 384, "(&(objectClass=%s)(%s=%s))", 
              goptions->ldap.search_class,
              goptions->ldap.naming_attr,
              user_name);

    /* set to read uid and gid attrs */
    attrs[0] = (char *) malloc(32);
    attrs[1] = (char *) malloc(32);
    strncpy(attrs[0], goptions->ldap.uid_attr, 32);
    strncpy(attrs[1], goptions->ldap.gid_attr, 32);
    attrs[2] = NULL;

    client_debug("   get_ldap_credential: search root: %s\n",
        goptions->ldap.search_root);
    client_debug("   get_ldap_credential: search scope: %d\n", 
        goptions->ldap.search_scope);
    client_debug("   get_ldap_credential: filter: %s\n", filter);
    client_debug("   get_ldap_credential: attrs: %s/%s\n", 
        goptions->ldap.uid_attr, goptions->ldap.gid_attr);
    ret = ldap_search_st(ld, goptions->ldap.search_root, goptions->ldap.search_scope,
              filter, (char **) attrs, 0, &timeout, &results);

    /* retrieve uid/gid values from results */
    if (ret == 0)
    {
        uid = gid = -1;

        if (results != NULL)
        {
            /* note: we only check the first entry */
            entry = ldap_first_entry(ld, results);
            if (entry != NULL)
            {
                attr_name = ldap_first_attribute(ld, entry, &ptr);
                while (attr_name != NULL)
                {
                    values = ldap_get_values(ld, entry, attr_name);
                    if (values != NULL) 
                    {
                        if (check_number(values[0])) 
                        {
                            if (!stricmp(attr_name, goptions->ldap.uid_attr))
                                uid = atoi(values[0]);
                            else if (!stricmp(attr_name, goptions->ldap.gid_attr))
                                gid = atoi(values[0]);
                        }
                        else
                        {
                            _snprintf(error_msg, sizeof(error_msg), "User %s: "
                                "LDAP attribute %s: not a number (%s)", user_name,
                                attr_name, values[0]);
                            report_error(error_msg, 0);
                            ret = -1;
                        }

                        ldap_value_free(values);
                    }
                    else
                    {
                        _snprintf(error_msg, sizeof(error_msg), "User %s: no "
                            "values for LDAP attribute %s", user_name, attr_name);
                        report_error(error_msg, 0);
                        ret = -1;
                    }
                   
                    ldap_memfree(attr_name);

                    attr_name = ldap_next_attribute(ld, entry, ptr);
                }
                ber_free(ptr, 0);
            }
            else
            {
                ldap_get_option(ld, LDAP_OPT_RESULT_CODE, &ret);
                _snprintf(error_msg, sizeof(error_msg), "User %s: no LDAP "
                    "entries", user_name);
                report_error(error_msg, 0);
                ret = -1;
            }

            ldap_msgfree(results);
        }
        else
        {
            _snprintf(error_msg, sizeof(error_msg), "User %s: no LDAP "
                "results", user_name);
            report_error(error_msg, 0);
            ret = -1;
        }
    }
    else 
    {
        _snprintf(error_msg, sizeof(error_msg), "User %s: LDAP search error: "
            "%s (%d)", user_name, ldap_err2string(ret), ret);
        report_error(error_msg, 0);
    }

    free(attrs[0]);
    free(attrs[1]);

    if (ret == 0 && uid != -1 && gid != -1)
    {
        ret = init_credential(uid, &gid, 1, NULL, NULL, credential);
        if (ret != 0)
        {
            _snprintf(error_msg, sizeof(error_msg), "User %s: credential "
                "error: %d", user_name, ret);
        }
    }
    else if (ret == 0)
    {
        _snprintf(error_msg, sizeof(error_msg), "User %s: LDAP credential "
            "not found", user_name);
        report_error(error_msg, 0);
        ret = -1;
    }

get_ldap_credential_exit:

    if (bind_ret != 0)
        ret = bind_ret;

    if (ld != NULL)
        ldap_unbind_s(ld);

    client_debug("   get_ldap_credential: exit\n");

    return ret;
}
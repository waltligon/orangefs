/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */


#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#ifndef WIN32
#include <unistd.h>
#include <strings.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <ctype.h>
#include <sys/stat.h>
#ifdef WIN32
#include <io.h>

#include "wincommon.h"
#endif

#include "pvfs2-internal.h"

#include "src/common/dotconf/dotconf.h"
#include "server-config.h"
#include "pvfs2.h"
#include "sid.h"
#include "job.h"
#include "trove.h"
#include "gossip.h"
#include "extent-utils.h"
#include "mkspace.h"
#include "pint-distribution.h"
#include "pvfs2-server.h"

#ifdef HAVE_OPENSSL
#include <openssl/evp.h>
#endif

static const char * replace_old_keystring(const char * oldkey);

static DOTCONF_CB(get_logstamp);
static DOTCONF_CB(get_storage_path);
static DOTCONF_CB(get_data_path);
static DOTCONF_CB(get_meta_path);
static DOTCONF_CB(get_config_path);
static DOTCONF_CB(enter_defaults_context);
static DOTCONF_CB(exit_defaults_context);
static DOTCONF_CB(enter_security_context);
static DOTCONF_CB(exit_security_context);
static DOTCONF_CB(enter_server_context);
static DOTCONF_CB(exit_server_context);
static DOTCONF_CB(enter_serverdef_context);
static DOTCONF_CB(exit_serverdef_context);
static DOTCONF_CB(enter_filesystem_context);
static DOTCONF_CB(exit_filesystem_context);
static DOTCONF_CB(enter_rootsrvs_context);
static DOTCONF_CB(exit_rootsrvs_context);
static DOTCONF_CB(enter_storage_hints_context);
static DOTCONF_CB(exit_storage_hints_context);
static DOTCONF_CB(enter_export_options_context);
static DOTCONF_CB(exit_export_options_context);
static DOTCONF_CB(enter_server_options_context);
static DOTCONF_CB(exit_server_options_context);
static DOTCONF_CB(enter_distribution_context);
static DOTCONF_CB(exit_distribution_context);
static DOTCONF_CB(get_unexp_req);
static DOTCONF_CB(get_tcp_buffer_send);
static DOTCONF_CB(get_tcp_buffer_receive);
static DOTCONF_CB(get_tcp_bind_specific);
static DOTCONF_CB(get_perf_update_interval);
static DOTCONF_CB(get_rootsrv);
static DOTCONF_CB(get_root_handle);
static DOTCONF_CB(get_root_dirdata_handle);
static DOTCONF_CB(get_name);
static DOTCONF_CB(get_logfile);
static DOTCONF_CB(get_logtype);
static DOTCONF_CB(get_event_logging_list);
static DOTCONF_CB(get_event_tracing);
static DOTCONF_CB(get_filesystem_replication);
static DOTCONF_CB(get_filesystem_collid);
static DOTCONF_CB(get_alias);
static DOTCONF_CB(get_sid);
static DOTCONF_CB(get_address_list);
static DOTCONF_CB(get_type_list);
static DOTCONF_CB(get_attribute_list);
static DOTCONF_CB(check_this_server);
#ifdef USE_TRUSTED
static DOTCONF_CB(get_trusted_portlist);
static DOTCONF_CB(get_trusted_network);
#endif
static DOTCONF_CB(get_bmi_module_list);
static DOTCONF_CB(get_flow_module_list);

static DOTCONF_CB(get_root_squash);
static DOTCONF_CB(get_root_squash_exceptions);
static DOTCONF_CB(get_read_only);
static DOTCONF_CB(get_all_squash);
static DOTCONF_CB(get_anon_gid);
static DOTCONF_CB(get_anon_uid);

static DOTCONF_CB(get_handle_recycle_timeout_seconds);
static DOTCONF_CB(get_flow_buffer_size_bytes);
static DOTCONF_CB(get_flow_buffers_per_flow);
static DOTCONF_CB(get_attr_cache_keywords_list);
static DOTCONF_CB(get_attr_cache_size);
static DOTCONF_CB(get_attr_cache_max_num_elems);
static DOTCONF_CB(get_trove_sync_meta);
static DOTCONF_CB(get_trove_sync_data);
static DOTCONF_CB(get_file_stuffing);
static DOTCONF_CB(get_db_cache_size_bytes);
static DOTCONF_CB(get_trove_max_concurrent_io);
static DOTCONF_CB(get_db_cache_type);
static DOTCONF_CB(get_param);
static DOTCONF_CB(get_value);
static DOTCONF_CB(get_default_num_dfiles);
static DOTCONF_CB(get_default_dfile_replication);
static DOTCONF_CB(get_immediate_completion);
static DOTCONF_CB(get_server_job_bmi_timeout);
static DOTCONF_CB(get_server_job_flow_timeout);
static DOTCONF_CB(get_client_job_bmi_timeout);
static DOTCONF_CB(get_client_job_flow_timeout);
static DOTCONF_CB(get_client_retry_limit);
static DOTCONF_CB(get_client_retry_delay);
static DOTCONF_CB(get_secret_key);
static DOTCONF_CB(get_coalescing_high_watermark);
static DOTCONF_CB(get_coalescing_low_watermark);
static DOTCONF_CB(get_trove_method);
static DOTCONF_CB(get_small_file_size);
static DOTCONF_CB(directio_thread_num);
static DOTCONF_CB(directio_ops_per_queue);
static DOTCONF_CB(directio_timeout);

static DOTCONF_CB(get_key_store);
static DOTCONF_CB(get_server_key);
static DOTCONF_CB(get_credential_timeout);
static DOTCONF_CB(get_capability_timeout);
static DOTCONF_CB(get_credcache_timeout);
static DOTCONF_CB(get_capcache_timeout);
static DOTCONF_CB(get_certcache_timeout);
static DOTCONF_CB(get_ca_file);
static DOTCONF_CB(get_user_cert_dn);
static DOTCONF_CB(get_user_cert_exp);
static DOTCONF_CB(enter_ldap_context);
static DOTCONF_CB(exit_ldap_context);
static DOTCONF_CB(get_ldap_hosts);
static DOTCONF_CB(get_ldap_bind_dn);
static DOTCONF_CB(get_ldap_bind_password);
static DOTCONF_CB(get_ldap_search_mode);
static DOTCONF_CB(get_ldap_search_root);
static DOTCONF_CB(get_ldap_search_class);
static DOTCONF_CB(get_ldap_search_attr);
static DOTCONF_CB(get_ldap_search_scope);
static DOTCONF_CB(get_ldap_uid_attr);
static DOTCONF_CB(get_ldap_gid_attr);
static DOTCONF_CB(get_ldap_search_timeout);

static DOTCONF_CB(tree_width);
static DOTCONF_CB(tree_threshold);
static DOTCONF_CB(distr_dir_servers_initial);
static DOTCONF_CB(distr_dir_servers_max);
static DOTCONF_CB(distr_dir_split_size);

static FUNC_ERRORHANDLER(errorhandler);
const char *contextchecker(command_t *cmd, unsigned long mask);

/* internal helper functions */
static int is_valid_alias(PINT_llist * host_aliases, char *str);
static int is_valid_host(host_alias_t *host);
static void free_host_alias(void *ptr);
static void free_filesystem(void *ptr);
static void copy_filesystem(struct filesystem_configuration_s *dest_fs,
                            struct filesystem_configuration_s *src_fs);
static int cache_config_files(struct server_configuration_s *config_s,
                              char *global_config_filename);
static int is_populated_filesystem_configuration(
                    struct filesystem_configuration_s *fs);
static int is_valid_filesystem_configuration(
                    struct server_configuration_s *config_s,
                    struct filesystem_configuration_s *fs);
/* obsolete at least for me - seems useful */
#if 0
static host_alias_t *find_host_alias_ptr_by_alias(
                                     struct server_configuration_s *config_s,
                                     char *alias,
                                     int *index);
#endif
static int compare_aliases(void* vkey,
                           void* valias2);

/* PVFS2 servers are deployed using configuration files that provide information
 * about the file systems, storage locations and endpoints that each server
 * manages.  For every pvfs2 deployment, there should be a global config file
 * (fs.conf) shared across all of the pvfs2 servers. When the servers
 * are started up, a command line parameter (server-alias) indicates what options
 * are relevant and applicable for a particular server.
 * This parameter will be used by the server to parse relevant options.
 * Configuration options in the global config files have the following format:
 * 
 * <pre>
 * OptionName OptionValue
 * </pre>
 *
 * An option cannot span more than one line, and only one option can
 * be specified on each line.  The <i>OptionValue</i> should 
 * be formatted based on the option's type:
 *
 * <ul>
 * <li>Integer - must be an integer value
 * <li>String - must be a string without breaks (newlines)
 * <li>List - a set of strings separated by commas
 * </ul>
 *
 * Options are grouped together using contexts, and usually
 * indented within a context for clarity.  A context is started
 * with a context start tag, and ended with a context end tag:
 * 
 * <pre>
 * &lt;ContextName&gt;
 *     Option1Name Option1Value
 *     Option2Name Option2Value
 * &lt;/ContextName&gt;
 * </pre>
 *
 * Options are required to be defined within a specified context 
 * or set of contexts. 
 * Sub-contexts can also be specified, and must be defined within
 * their specified parent context.  For example, the <i>Range</i> option is
 * specified in either the <i>DataHandleRanges</i> or <i>MetaHandleRanges</i> 
 * contexts.  Both of
 * those contexts are specified to be defined in the <i>FileSystem</i> context.
 * Details of the required context an option or sub-context must be defined in
 * are given in the <a href="#OptionDetails">Option Details</a> section. 
 *
 * Options and contexts that appear in the top-level (not defined within
 * another context) are considered to be defined in a special <i>Global</i>
 * context.  Many options are only specified to appear within
 * the <a name="Default">Default</a> context, 
 * which is a context that allows a default value to be specified for certain
 * options.
 *
 * The options detailed below each specify their type, the context
 * where they appear, a default value, and description.  The default
 * value is used if the option is not specified in any of the config files.
 * Options without default values are required to be defined in the
 * config file.
 */
static const configoption_t options[] =
{
    /* Options specified within the Defaults context are used as 
     * default values over all the OrangeFS server specific config files.
     */
    {"<Defaults>",ARG_NONE, enter_defaults_context,NULL,CTX_GLOBAL,NULL},

    /* Specifies the end-tag for the Defaults context.
     */
    {"</Defaults>",ARG_NONE, exit_defaults_context,NULL,CTX_DEFAULTS,NULL},

    /* Options specified within the Security context are used to configure
     * settings related to key- or certificate-based security options.
     * These options are ignored if security mode is not compiled in.
     */
    {"<Security>", ARG_NONE, enter_security_context, NULL,
        CTX_DEFAULTS|CTX_SERVER_OPTIONS, NULL},

    /* Specifies the end-tag for the Security context.
     */
    {"</Security>", ARG_NONE, exit_security_context, NULL, CTX_SECURITY, NULL},

#ifdef USE_TRUSTED
    /* Specifies the range of ports in the form of a range of 2 integers
     * from which the connections are going to be accepted and serviced.
     * The format of the TrustedPorts option is:
     *
     * <c>TrustedPorts{</c><i>StartPort</i><c>}-{</c><i>EndPort</i><c>}</c>
     *
     * As an example:
     *
     * <c>TrustedPorts 0-65535</c>
     */
    {"TrustedPorts",ARG_STR, get_trusted_portlist,NULL,
        CTX_SECURITY,NULL},
    
    /* Specifies the IP network and netmask in the form of 2 BMI addresses from
     * which the connections are going to be accepted and serviced.
     * The format of the TrustedNetwork option is:
     *
     * <c>TrustedNetwork {</c><i>bmi-network-address@bmi-network-mask</i><c>}-{</c><i>EndPort</i><c>}</c>
     *
     * As an example:
     *
     * <c>TrustedNetwork tcp://192.168.4.0@24</c>
     */
    {"TrustedNetwork",ARG_LIST, get_trusted_network,NULL,
        CTX_SECURITY,NULL},
#endif

    /* A path to a keystore file, which stores server and client 
     * public keys for key-based security. 
     * Note: May be in the Defaults section for backwards-compatibility.
     * For newly-generated configuration files it should appear in the
     * Security section.
     */
    {"KeyStore", ARG_STR, get_key_store, NULL, 
        CTX_DEFAULTS|CTX_SERVER_OPTIONS|CTX_SECURITY, NULL},

    /* Path to the server private key file, in PEM format. Must 
     * correspond to CA certificate in certificate mode.
     * Note: May be in the Defaults section for backwards-compatibility.
     * For newly-generated configuration files it should appear in the
     * Security section.
     */
    {"ServerKey", ARG_STR, get_server_key, NULL,
        CTX_DEFAULTS|CTX_SERVER_OPTIONS|CTX_SECURITY, NULL},

    /* Credential timeout in seconds */
    {"CredentialTimeoutSecs", ARG_INT, get_credential_timeout, NULL,
        CTX_SECURITY, "3600"},

    /* Capability timeout in seconds */
    {"CapabilityTimeoutSecs", ARG_INT, get_capability_timeout, NULL,
        CTX_SECURITY, "600"},

    /* Credential cache timeout in seconds */
    {"CredentialCacheTimeoutSecs", ARG_INT, get_credcache_timeout, NULL,
        CTX_SECURITY, "3600"},

    /* Capability cache timeout in seconds */
    {"CapabilityCacheTimeoutSecs", ARG_INT, get_capcache_timeout, NULL,
        CTX_SECURITY, "600"},

    /* Certificate cache timeout in seconds */
    {"CertificateCacheTimeoutSecs", ARG_INT, get_certcache_timeout, NULL,
        CTX_SECURITY, "3600"},

    /* Path to CA certificate file in PEM format.
     * Note: May be in the Defaults section for backwards-compatibility.
     * For newly-generated configuration files it should appear in the
     * Security section.
     */
    {"CAFile", ARG_STR, get_ca_file, NULL,
        CTX_DEFAULTS|CTX_SERVER_OPTIONS|CTX_SECURITY, NULL},

    /* DN used for root of generated user certificate subject DN
     * Note: May be in the Defaults section for backwards-compatibility.
     * For newly-generated configuration files it should appear in the
     * Security section.
     */
    {"UserCertDN", ARG_STR, get_user_cert_dn, NULL,
        CTX_DEFAULTS|CTX_SERVER_OPTIONS|CTX_SECURITY, 
        "\"C=US, O=OrangeFS\""},

    /* Expiration of generated user certificate in days 
     * Note: May be in the Defaults section for backwards-compatibility.
     * For newly-generated configuration files it should appear in the
     * Security section.
     */
    {"UserCertExp", ARG_INT, get_user_cert_exp, NULL,
        CTX_DEFAULTS|CTX_SERVER_OPTIONS|CTX_SECURITY, "365"},

    /* Open tag for LDAP options, used in certificate mode. 
     */
    {"<LDAP>", ARG_NONE, enter_ldap_context, NULL, 
       CTX_SECURITY, NULL},

    /* Close tag for LDAP options */
    {"</LDAP>", ARG_NONE, exit_ldap_context, NULL,
        CTX_LDAP, NULL},

    /* List of LDAP hosts in URI format, e.g. <c>ldaps://ldap.acme.com:999</c> */
    {"Hosts", ARG_STR, get_ldap_hosts, NULL,
        CTX_LDAP, "ldaps://localhost"},

    /* DN of LDAP user to use when binding to LDAP directory.
     */
    {"BindDN", ARG_STR, get_ldap_bind_dn, NULL,
        CTX_LDAP, NULL},

    /* Password of LDAP user to use when binding to LDAP directory. 
     * May also be in form <c>file:{</c><i>path</i><c>}</c> 
     * which will load password from restricted file. 
     */
    {"BindPassword", ARG_STR, get_ldap_bind_password, NULL,
        CTX_LDAP, NULL},

    /* May be <c>CN</c> or <c>DN</c>. Controls how the certificate subject DN is
     * used to search LDAP for a user. 
     */
    {"SearchMode", ARG_STR, get_ldap_search_mode, NULL,
        CTX_LDAP, "CN"},

    /* DN of top-level LDAP search container. Only used in <c>CN</c> mode.     
     */
    {"SearchRoot", ARG_STR, get_ldap_search_root, NULL,
        CTX_LDAP, NULL},

    /* Object class of user objects to search in LDAP.
     */
    {"SearchClass", ARG_STR, get_ldap_search_class, NULL,
        CTX_LDAP, "inetOrgPerson"},

    /* Attribute name to match certificate CN. Only used in <c>CN</c> mode. 
     */
    {"SearchAttr", ARG_STR, get_ldap_search_attr, NULL,
        CTX_LDAP, "CN"},

    /* May be <c>onelevel</c> to search only SearchRoot container,
     * or <c>subtree</c> to
     * search SearchRoot container and all child containers.
     */
    {"SearchScope", ARG_STR, get_ldap_search_scope, NULL,
        CTX_LDAP, "subtree"},

    /* Attribute name in which UID value is stored.
     */
    {"UIDAttr", ARG_STR, get_ldap_uid_attr, NULL,
        CTX_LDAP, "uidNumber"},

    /* Attribute name in which GID value is stored. 
     */
    {"GIDAttr", ARG_STR, get_ldap_gid_attr, NULL,
        CTX_LDAP, "gidNumber"},

    /* LDAP server timeout for searches (in seconds) 
     */
    {"SearchTimeoutSecs", ARG_INT, get_ldap_search_timeout, NULL,
        CTX_LDAP, "15"},

    /* This groups the Server definition options including alias, SID,
     * address, type, and attributes.
     *
     * This context should be defined before any FileSystem contexts
     * are defined, as options in the FileSystem context usually need to
     * reference the aliases defined in this context.
     *
     * Note: beginning with V3 Aliases specify a SID rather than a
     * BMI_addr. SIDs are UUIDs (36 character string representing
     * a 16 byte array plus 4 separators).  SID to BMI_addr mapping
     * is handled by the SID cache, and typically loaded from a distinct
     * text file and then updated online.
     */
    {"<ServerDefines>", ARG_NONE, enter_server_context, NULL, CTX_GLOBAL, NULL},

    /* Specifies the end-tag for the ServerDefines context.
     */
    {"</ServerDefines>", ARG_NONE, exit_server_context, NULL, CTX_SERVER, NULL},

    /* THis groups defines for a single server
     */
    {"<ServerDef>", ARG_NONE, enter_serverdef_context, NULL, CTX_SERVER, NULL},

    /* Specifies the end-tag for the ServerDef context.
     */
    {"</ServerDef>", ARG_NONE, exit_serverdef_context, NULL, CTX_SERVERDEF, NULL},


    /* Specifies an alias in the form of a non-whitespace string that
     * can be used to reference a BMI server address (a HostID).  This
     * allows us to reference individual servers by an alias instead of their
     * full HostID.  The format of the Alias option is:
     *
     *     Alias {Alias}
     *
     * As an example:
     *
     *     Alias mynode1 
     */
    {"Alias", ARG_STR, get_alias, NULL, CTX_SERVERDEF, NULL},

    /* Specifies the SID of a server, which is in a standard UUID format
     *
     *     SID (SID}
     *
     * As an example:
     *
     *     SID 12345678-90ab-cdef-1234-567890abcdef
     */
    {"SID", ARG_STR, get_sid, NULL, CTX_SERVERDEF, NULL},

    /* Specifies one or more addresses for a server in BMI URI form
     *
     *     Address {bmi address} ...
     *
     * As an example:
     *
     *     Address tcp://hostname1.clustername1.domainname:12345
     */
    {"Address", ARG_LIST, get_address_list, NULL, CTX_SERVERDEF, NULL},

    /* Sprecifies one or more server types for a server
     *
     *     Type {type-word} ...
     *
     * As an example:
     *
     *     Type META DATA DIRDATA
     */
    {"Type", ARG_LIST, get_type_list, NULL, CTX_SERVERDEF, NULL},

    /* Specfies one or more user attributes for a server
     *
     *     Attribute {type-word} ...
     *
     * As an example:
     *
     *     Attribute RACK=3 TIER=1
     */
    {"Attribute", ARG_LIST, get_attribute_list, NULL, CTX_SERVERDEF, NULL},

    /* This groups the Server specific options.
     *
     * The ServerOptions context should be defined after the Alias mappings 
     * have been defined. The reason is that the ServerOptions context is
     * defined in terms of the aliases defined in that context.
     *
     * Default options applicable to all servers can be overridden on
     * a per-server basis in the ServerOptions context.
     * To illustrate:
     * Suppose the Option name is X, its default value is Y,
     * and one wishes to override the option for a server to Y'.
     *
     * <p style="margin-bottom: 0pt;"><c>&lt;Defaults&gt;</c></p>
     * <p class="normal_indent_1" style="margin-bottom: 0pt;"><c>..</c></p>
     * <p class="normal_indent_2" style="margin-bottom: 0pt;"><c>X Y</c></p>
     * <p class="normal_indent_1" style="margin-bottom: 0pt;"><c>..</c></p>
     * <p style="margin-bottom: 0pt;"><c>&lt;/Defaults&gt;</c></p>
     *
     * <p style="margin-bottom: 0pt;"><c>&lt;ServerOptions&gt;</c></p>
     * <p class="normal_indent_1" style="margin-bottom: 0pt;"><c>Server {</c><i>server alias</i><c>}</c></p>
     * <p class="normal_indent_1" style="margin-bottom: 0pt;"><c>..</c></p>
     * <p class="normal_indent_1" style="margin-bottom: 0pt;"><c>X Y'</c></p>
     * <p class="normal_indent_1" style="margin-bottom: 0pt;"><c>..</c></p>
     * <p style="margin-bottom: 0pt;"><c>&lt;/ServerOptions&gt;</c></p>
     *
     * The ServerOptions context REQUIRES the Server option specify
     * the server alias, which sets the remaining options specified
     * in the context for that server.
    */
    {"<ServerOptions>", ARG_NONE, enter_server_options_context, NULL,
            CTX_SERVERDEF, NULL},

    /* Specifies the end-tag of the ServerOptions context.
     */
    {"</ServerOptions>", ARG_NONE, exit_server_options_context, NULL,
            CTX_SERVER_OPTIONS, NULL},

    /* Defines the server alias for the server specific options that
     * are to be set within the ServerOptions context.
     */
    {"Server", ARG_STR, check_this_server, NULL, CTX_SERVER_OPTIONS, NULL},

    /* This groups options specific to a file system.  An OrangeFS server may manage
     * more than one file system, so a config file may have more than
     * one FileSystem context, each defining the parameters of a different
     * file system.
     */
    {"<FileSystem>", ARG_NONE, enter_filesystem_context, NULL,
            CTX_GLOBAL, NULL},

    /* Specifies the end-tag of a FileSystem context.
     */
    {"</FileSystem>", ARG_NONE, exit_filesystem_context, NULL,
            CTX_FILESYSTEM, NULL},

     /* Specifies the beginning of an ExportOptions context.
      * This groups options specific to a file system and related to the behavior
      * of how it gets exported to various clients. Most of these options
      * will affect things like uid translation.
      */
     {"<ExportOptions>", ARG_NONE, enter_export_options_context, NULL,
             CTX_FILESYSTEM, NULL},
 
     /* Specifies the end-tag of the ExportOptions context.
      */
     {"</ExportOptions>", ARG_NONE, exit_export_options_context, NULL,
             CTX_EXPORT, NULL},
 
    /* This groups
     * options specific to a file system and related to the behavior of the
     * storage system.  Mostly these options are passed directly to the
     * TROVE storage module which may or may not support them.  The
     * DBPF module (currently the only TROVE module available) supports
     * all of them.
     */
    {"<StorageHints>",ARG_NONE, enter_storage_hints_context, NULL,
            CTX_FILESYSTEM, NULL},

    /* Specifies the end-tag of the StorageHints context.
     */
    {"</StorageHints>",ARG_NONE, exit_storage_hints_context, NULL,
            CTX_STORAGEHINTS, NULL},

    /* Provides a context for defining the file system's default
     * distribution to use and the parameters to be set for that distribution.
     *
     * Valid options within the Distribution context are <c>Name</c>, <c>Param</c>, and <c>Value</c>.
     *
     * This context is an optional context within the FileSystem context.  If
     * not specified, the file system defaults to the simple-stripe distribution.
     */
    {"<Distribution>",ARG_NONE, enter_distribution_context, NULL,
            CTX_FILESYSTEM, NULL},

    /* Specifies the end-tag for the Distribution context.
     */
    {"</Distribution>", ARG_NONE, exit_distribution_context, NULL,
            CTX_DISTRIBUTION, NULL},

    /* Opens a context for describing the root metadata
     */
    {"<RootServers>", ARG_NONE, enter_rootsrvs_context, NULL,
            CTX_FILESYSTEM, NULL},

    /* Closes a context for describing the root metadata
     */
    {"</RootServers>", ARG_NONE, exit_rootsrvs_context, NULL,
            CTX_ROOTSERVERS, NULL},

    /* Specifies the SID of a server that holds a copy of the
     * filesystem root object.  The format is:
     * RootServer {Alias}
     */
    {"RootServer", ARG_STR, get_rootsrv, NULL, CTX_ROOTSERVERS, NULL},

    /* Specifies the handle value for the root of the file system.  This
     * is a required option in the FileSystem context.  The format is:
     *
     * <c>RootHandle {</c><i>OID</i><c>}</c>
     *
     * Where <c>{</c><i>OID</i><c>}</c> is a UUID: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
     *
     * In general its best to let the pvfs-genconfig script specify a
     * RootHandle value for the file system.
     */
    {"RootHandle", ARG_STR, get_root_handle, NULL,
            CTX_ROOTSERVERS, NULL},

    /* Specifies the handle value for the root of the Filesystem.  This
     * is a required option in the Filesystem context.  The format is:
     *
     * RootHandle {OID}
     *
     * Where {OID} is a UUID: XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX
     *
     * In general its best to let the pvfs-genconfig script specify a
     * RootHandle value for the filesystem.
     */
    {"RootDirdataHandle", ARG_STR, get_root_dirdata_handle, NULL,
            CTX_ROOTSERVERS, NULL},

    /* This option specifies the name of the particular filesystem or
     * distribution that its defined in.  It is a required option in
     * FileSystem and Distribution contexts.
     */
    {"Name", ARG_STR, get_name, NULL,
            CTX_FILESYSTEM | CTX_DISTRIBUTION, NULL},

    /* An OrangeFS server may manage more than one file system, and so a
     * unique identifier is used to represent each one.  
     * This option specifies such an ID (sometimes called a 'collection
     * id') for the file system it is defined in.  
     *
     * The ID value can be any positive integer, no greater than
     * 2147483647 (INT32_MAX).  It is a required option in the FileSystem
     * context.
     */
    {"ID", ARG_INT, get_filesystem_collid, NULL,
            CTX_FILESYSTEM, NULL},

    /* All metadata in a file system is replicated to the same degree.
     * This defines the number of copies of each metadata object in the
     * file system.  This is optional, and defauts to 1 (no
     * replication).
     */
    {"MetaReplicationFactor", ARG_INT, get_filesystem_replication, NULL,
            CTX_FILESYSTEM, "1"},

    /* maximum number of AIO operations that Trove will allow to run
     * concurrently 
     */
    {"TroveMaxConcurrentIO", ARG_INT, get_trove_max_concurrent_io, NULL,
            CTX_DEFAULTS | CTX_SERVER_OPTIONS, "16"},

    /* The gossip interface in OrangeFS allows users to specify different
     * levels of logging for the OrangeFS server.  The output of these
     * different log levels is written to a file, which is specified in
     * this option.  The value of the option must be the path pointing to a 
     * file with valid write permissions.  The LogFile option can be
     * specified for all the OrangeFS servers in the Defaults context or for
     * a particular server in the Global context.
     */
    {"LogFile", ARG_STR, get_logfile, NULL,
            CTX_DEFAULTS | CTX_SERVER_OPTIONS, "/tmp/pvfs2-server.log"},

    /* The LogType option can be used to control the destination of log 
     * messages from OrangeFS server.  The default value is <c>file</c>, which causes
     * all log messages to be written to the file specified by the LogFile
     * parameter.  Another option is <c>syslog</c>, which causes all log messages
     * to be written to syslog.
     */
    {"LogType", ARG_STR, get_logtype, NULL,
            CTX_DEFAULTS | CTX_SERVER_OPTIONS, "file"},

    /* The gossip interface in OrangeFS allows users to specify different
     * levels of logging for the OrangeFS server.  This option sets that level for
     * either all servers (by being defined in the Defaults context) or for
     * a particular server by defining it in the Global context.  Possible
     * values for event logging are:
     *
     * __EVENTLOGGING__
     *
     * The value of the EventLogging option can be a comma-separated list
     * of the above values.  Individual values can also be negated with
     * a '-'.  Examples of possible values are:
     *
     * <c>EventLogging flow,msgpair,io</c>
     * 
     * <c>EventLogging -storage</c>
     * 
     * <c>EventLogging -flow,-flowproto</c>
     */
    {"EventLogging", ARG_LIST, get_event_logging_list, NULL,
            CTX_DEFAULTS | CTX_SERVER_OPTIONS, "none,"},

    {"EnableTracing",ARG_STR, get_event_tracing, NULL,
            CTX_DEFAULTS| CTX_SERVER_OPTIONS, "no"},

    /* At startup each OrangeFS server allocates space for a set number
     * of incoming requests to prevent the allocation delay at the beginning
     * of each unexpected request.  This parameter specifies the number
     * of requests for which to allocate space.
     *
     * A default value is set in the Defaults context which will be be used 
     * for all servers. 
     * However, the default value can also be overwritten by setting a separate value
     * in the ServerOptions context.
     */
     {"UnexpectedRequests", ARG_INT, get_unexp_req, NULL,
             CTX_DEFAULTS | CTX_SERVER_OPTIONS, "50"},

    /* DEPRECATED. Use <c>DataStorageSpace</c> and <c>MetadataStorageSpace</c> 
     *       instead.
     */
    {"StorageSpace", ARG_STR, get_storage_path, NULL,
            CTX_DEFAULTS | CTX_SERVER_OPTIONS, NULL},

    /* Specifies the local path for the OrangeFS server to use as storage space 
     * for data files. This option specifies the default path for all servers 
     * and will appear in the Defaults context.
     *
     * NOTE: This can be overridden in the ServerOptions context on a per-server
     * basis.
     * Example:
     *
     * <c>DataStorageSpace /opt/orangefs/storage/data</c>
     */
    {"DataStorageSpace", ARG_STR, get_data_path, NULL,
            CTX_DEFAULTS | CTX_SERVER_OPTIONS, NULL},

    /* Specifies the local path for the OrangeFS server to use as storage space 
     * for metadata files. This option specifies the default path for all 
     * servers and will appear in the Defaults context.
     *
     * NOTE: This can be overridden in the ServerOptions context on a per-server
     * basis.
     * Example:
     *
     * <c>MetadataStorageSpace /opt/orangefs/storage/meta</c>
     */
    {"MetadataStorageSpace", ARG_STR, get_meta_path, NULL,
            CTX_DEFAULTS | CTX_SERVER_OPTIONS, NULL},

    /* The SID cache stores attributes and addresses for known servers
     * indexed by SID.  This database is memory only, but is saved
     * periodically to local storage and loaded when the server starts
     * so we don't have to hunt for lots of servers at startup.
     */
    {"ConfigStorageSpace", ARG_STR, get_config_path, NULL,
            CTX_DEFAULTS | CTX_SERVER_OPTIONS, "/tmp/pvfs-config.storage"},

     /* Current implementations of TCP on most systems use a window
      * size that is too small for almost all uses of OrangeFS.  
      * We recommend administators
      * consider tuning the Linux kernel maximum send and
      * receive buffer sizes via the /proc settings.  The
      * <a href="http://www.psc.edu/networking/projects/tcptune/#Linux">
      * PSC tcp tuning section for linux</a> has good information
      * on how to do this.  
      *
      * The <i>TCPBufferSend</i> and
      * <i>TCPBufferReceive</i> options allow setting the tcp window
      * sizes for the OrangeFS clients and servers, if using the
      * system wide settings is unacceptable.  The values should be
      * large enough to hold the full bandwidth delay product (BDP)
      * of the network.  Note that setting these values disables
      * tcp autotuning.  See the
      * <a href="http://www.psc.edu/networking/projects/tcptune/#options">
      * PSC networking options</a> for details.
      */
     {"TCPBufferSend", ARG_INT, get_tcp_buffer_send, NULL,
             CTX_DEFAULTS, "0"},

     /* See the <a href="#TCPBufferSend">TCPBufferSend</a> option.
      */
      {"TCPBufferReceive", ARG_INT, get_tcp_buffer_receive, NULL,
             CTX_DEFAULTS, "0"},

     /* If enabled, specifies that the server should bind its port only on
      * the specified address (rather than INADDR_ANY).
      */
     {"TCPBindSpecific", ARG_STR, get_tcp_bind_specific, NULL,
            CTX_DEFAULTS | CTX_SERVER_OPTIONS, "no"},

     /* Specifies the timeout value in seconds for BMI jobs on the server.
      */
     {"ServerJobBMITimeoutSecs", ARG_INT, get_server_job_bmi_timeout, NULL,
             CTX_DEFAULTS | CTX_SERVER_OPTIONS, "300"},
     
     /* Specifies the timeout value in seconds for TROVE jobs on the server.
      */
     {"ServerJobFlowTimeoutSecs", ARG_INT, get_server_job_flow_timeout, NULL,
             CTX_DEFAULTS | CTX_SERVER_OPTIONS, "300"},
     
     /* Specifies the timeout value in seconds for BMI jobs on the client.
      */
     {"ClientJobBMITimeoutSecs", ARG_INT, get_client_job_bmi_timeout, NULL,
             CTX_DEFAULTS, "300"},

     /* Specifies the timeout value in seconds for FLOW jobs on the client.
      */
     {"ClientJobFlowTimeoutSecs", ARG_INT, get_client_job_flow_timeout, NULL,
             CTX_DEFAULTS, "300"},

     /* Specifies the number of retry attempts for operations (when possible).
      */
     {"ClientRetryLimit", ARG_INT, get_client_retry_limit, NULL,
             CTX_DEFAULTS, "5"},

     /* Specifies the delay in milliseconds to wait between retries.
      */
     {"ClientRetryDelayMilliSecs", ARG_INT, get_client_retry_delay, NULL,
             CTX_DEFAULTS, "2000"},

    /* Specifies if file stuffing should be enabled or not. File stuffing
     * allows the data for a small file to be stored on the same server
     * as the metadata.
     */
    {"FileStuffing",ARG_STR, get_file_stuffing, NULL, 
        CTX_FILESYSTEM,"yes"},

     /* This specifies the frequency (in milliseconds) 
      * that performance monitor should be updated
      * when the OrangeFS server is running in admin mode.
      *
      * Can be set in either Default or ServerOptions contexts.
      */
    {"PerfUpdateInterval",ARG_INT, get_perf_update_interval,NULL,
        CTX_DEFAULTS,"1000"},

    /* List the BMI modules to load when the server is started.  At present,
     * only tcp, infiniband, and myrinet are valid BMI modules.  
     * The format of the list is a comma separated list of one of:
     *
     * <c>bmi_tcp</c>
     * <p><c>bmi_ib</c></p>
     * <p><c>bmi_gm</c></p>
     *
     * For example:
     *
     * <c>BMIModules bmi_tcp,bmi_ib</c>
     *
     * Note that only the bmi modules compiled into OrangeFS should be
     * specified in this list.  The BMIModules option can be specified
     * in either the Defaults or ServerOptions contexts.
     */
    {"BMIModules",ARG_LIST, get_bmi_module_list,NULL, CTX_DEFAULTS,NULL},

    /* List the flow modules to load when the server is started.  The modules
     * available for loading currently are:
     *
     * <c>flowproto_multiqueue</c> - A flow module that handles all the possible flows,
     * <c>bmi->trove</c>, <c>trove->bmi</c>, <c>mem->bmi</c>, <c>bmi->mem</c>.  At present, this is the
     * default and only available flow for production use.
     *
     * <c>flowproto_bmi_cache</c> - A flow module that enables the use of the NCAC
     * (network-centric adaptive cache) in the OrangeFS server.  Since the NCAC
     * is currently disable and unsupported, this module exists as a proof
     * of concept only.
     *
     * <c>flowproto_dump_offsets</c> - Used for debugging, this module allows the
     * developer to see what/when flows are being posted, without making
     * any actual BMI or TROVE requests.  This should only be used if you
     * know what you're doing.
     *
     */
    {"FlowModules",ARG_LIST, get_flow_module_list,NULL,
        CTX_DEFAULTS,"flowproto_multiqueue,"},

    /* Specifies the format of the date/timestamp that events will have
     * in the event log.  Possible values are:
     *
     * <c>usec</c>: [%H:%M:%S.%U]
     *
     * <c>datetime</c>: [%m/%d/%Y %H:%M:%S]
     *
     * <c>thread</c>: [%H:%M:%S.%U (%lu)]
     *
     * <c>none</c>
     *
     * The format of the option is one of the above values.  For example,
     *
     * <c>LogStamp datetime</c>
     */
    {"LogStamp",ARG_STR, get_logstamp,NULL,
        CTX_DEFAULTS|CTX_SERVER_OPTIONS,"usec"},

    /* buffer size to use for bulk data transfers */
    {"FlowBufferSizeBytes", ARG_INT,
         get_flow_buffer_size_bytes, NULL, CTX_FILESYSTEM,"262144"},

    /* number of buffers to use for bulk data transfers */
    {"FlowBuffersPerFlow", ARG_INT,
         get_flow_buffers_per_flow, NULL, CTX_FILESYSTEM,"8"},

    /* RootSquash option specifies whether the exported file system needs to
    *  squash accesses by root. This is an optional parameter that needs 
    *  to be specified as part of the ExportOptions
     * context and is a list of BMI URL specification of client addresses 
     * for which RootSquash has to be enforced. 
     *
     * <c>RootSquash tcp://192.168.2.0@24 tcp://10.0.0.* tcp://192.168.* ...</c>
     */
    {"RootSquash", ARG_LIST, get_root_squash, NULL,
        CTX_EXPORT, ""},
  
    /* RootSquashExceptions option specifies exceoptions to the RootSquash 
     * list. This is an optional parameter that needs to be specified as 
     * part of the ExportOptions context and is a list of BMI URL 
     * specification of client addresses for which RootSquash
     * has to be enforced. 
     *
     * <c>RootSquash tcp://192.168.2.0@24 tcp://10.0.0.* tcp://192.168.* ...</c>
     */
    {"RootSquashExceptions", ARG_LIST, get_root_squash_exceptions, NULL,
        CTX_EXPORT, ""},

    /* ReadOnly option specifies whether the exported file system needs to
    *  disallow write accesses from clients or anything that modifies the 
    *  state of the file system.
     * This is an optional parameter that needs to be specified as part of 
     * the ExportOptions context and is a list of BMI URL specification of 
     * client addresses for which ReadOnly has to be enforced.
     * An example: 
     *
     * <c>ReadOnly tcp://192.168.2.0@24 tcp://10.0.0.* tcp://192.168.* ...</c>
     */
    {"ReadOnly", ARG_LIST,  get_read_only,    NULL,
        CTX_EXPORT, ""},

    /* AllSquash option specifies whether the exported file system needs to 
    *  squash all accesses to the file system to a specified uid/gid.
     * This is an optional parameter that needs to be specified as part of 
     * the ExportOptions context and is a list of BMI URL specification of client 
     * addresses for which AllSquash has to be enforced.
     * An example:
     *
     * <c>AllSquash tcp://192.168.2.0@24 tcp://10.0.0.* tcp://192.168.* ...</c>
     */
    {"AllSquash", ARG_LIST, get_all_squash,   NULL,
        CTX_EXPORT, ""},

    /* AnonUID tells the servers to translate the requesting client's uid
     * to the specified one whenever AllSquash is specified.
     * If this is not specified and AllSquash is specified then the uid used 
     * will be that of nobody.
     * An example:
     *
     * AnonUID 3454
     */
    {"AnonUID",  ARG_STR,  get_anon_uid,     NULL,
        CTX_EXPORT, "65534"},

    /* AnonGID tells the servers to translate the requesting client's gid
     * to the specified one whenever AllSquash is specified.
     * If this is not specified and AllSquash is specified then the gid used 
     * will be that of nobody.
     * An example:
     *
     * AnonGID 3454
     */
    {"AnonGID",  ARG_STR,  get_anon_gid,     NULL,
        CTX_EXPORT, "65534"},

    /* The TROVE storage layer has a management component that deals with
     * allocating handle values for new metafiles and datafiles.  The underlying
     * trove module can be given a hint to tell it how long to wait before
     * reusing handle values that have become freed up (only deleting files will
     * free up a handle).  The HandleRecycleTimeoutSecs option specifies
     * the number of seconds to wait for each file system.  This is an
     * optional parameter that can be specified in the StorageHints context.
     */
    {"HandleRecycleTimeoutSecs", ARG_INT,
         get_handle_recycle_timeout_seconds, NULL, 
         CTX_STORAGEHINTS,"360"},
    
    /* The TROVE layer (server side storage layer) 
     * has an attribute caching component that 
     * caches stored attributes.  This is used to improve the performance of
     * metadata accesses.  The AttrCacheKeywords option is a list of the
     * object types that should get cached in the attribute cache.  
     * The possible values for this option are:
     *
     * <c>dh</c> - (datafile handles) This will cache the array of datafile handles for
     *      each logical file in this file system
     * 
     * <c>md</c> - (metafile distribution) This will cache (for each logical file)
     *      the file distribution information used to create/manage
     *      the datafiles.  
     *
     * <c>de</c> - (directory entries) This will cache the handles of 
     *      the directory entries in this file system
     *
     * <c>st</c> - (symlink target) This will cache the target path 
     *      for the symbolic links in this file system
     *
     * The format of this option is a comma-separated list of one or more
     * of the above values.  For example:
     *
     * <c>AttrCacheKeywords dh,md,de,st</c>
     */
    {"AttrCacheKeywords",ARG_LIST, get_attr_cache_keywords_list,NULL,
        CTX_STORAGEHINTS, 
        DATAFILE_HANDLES_KEYSTR","METAFILE_DIST_KEYSTR","
        DIRECTORY_ENTRY_KEYSTR","SYMLINK_TARGET_KEYSTR},
    
    /* The attribute cache in the TROVE layer mentioned in the documentation
     * for the AttrCacheKeywords option is managed as a hashtable.  The
     * AttrCacheSize adjusts the number of buckets that this hashtable contains.
     * This value can be adjusted for better performance.  A good hashtable
     * size should always be a prime number.
     */
    {"AttrCacheSize",ARG_INT, get_attr_cache_size, NULL,
        CTX_STORAGEHINTS,"511"},

    /* This option specifies the max cache size of the attribute cache 
     * in the TROVE layer mentioned in the documentation
     * for the AttrCacheKeywords option.  This value can be adjusted for
     * better performance.
     */
    {"AttrCacheMaxNumElems",ARG_INT,get_attr_cache_max_num_elems,NULL,
        CTX_STORAGEHINTS,"1024"},
    
    /* The TroveSyncMeta option allows users to turn off metadata
     * synchronization with every metadata write.  This can greatly improve
     * performance.  In general, this value should probably be set to yes;
     * otherwise, metadata transaction could be lost in the event of server
     * failover.
     */
    {"TroveSyncMeta",ARG_STR, get_trove_sync_meta, NULL, 
        CTX_STORAGEHINTS,"yes"},

    /* The TroveSyncData option allows users to turn off datafile
     * synchronization with every write operation.  This can greatly improve
     * performance, but may cause lost data in the event of server failover.
     */
    {"TroveSyncData",ARG_STR, get_trove_sync_data, NULL, 
        CTX_STORAGEHINTS,"yes"},

    /* The DBCacheSizeBytes option allows users to set the size of the
     * shared memory buffer pool (i.e., cache) for Berkeley DB. The size is
     * specified in bytes.
     * See BDB documentation for <c>set_cachesize()</c> for more info.
     */
    {"DBCacheSizeBytes", ARG_INT, get_db_cache_size_bytes, NULL,
        CTX_STORAGEHINTS,"0"},

    /* cache type for berkeley db environment.  <c>sys</c> and <c>mmap</c> are valid
     * values for this option
     */
    {"DBCacheType", ARG_STR, get_db_cache_type, NULL,
        CTX_STORAGEHINTS, "sys"},

    /* This option specifies a parameter name to be passed to the 
     * distribution to be used.  This option should be immediately
     * followed by a Value option.
     */
    {"Param", ARG_STR, get_param, NULL, 
        CTX_DISTRIBUTION, NULL},
    
    /* This option specifies the value of the parameter whose name
     * was specified in the Param option.
     */
    {"Value", ARG_INT, get_value, NULL, 
        CTX_DISTRIBUTION, NULL},
    
    /* This option specifies the default number of datafiles to use
     * when a new file is created.  The value is passed to the distribution
     * and it determines whether to use that value or not.
     */
    {"DefaultNumDFiles", ARG_INT, get_default_num_dfiles, NULL,
        CTX_FILESYSTEM, "0"},

    /* This option specifies the default number of datafiles to use
     * when a new file is created.  The value is passed to the distribution
     * and it determines whether to use that value or not.
     */
    {"DefaultDFileReplication", ARG_INT, get_default_dfile_replication, NULL,
        CTX_FILESYSTEM, "0"},

    {"ImmediateCompletion", ARG_STR, get_immediate_completion, NULL,
        CTX_STORAGEHINTS, "no"},

    {"CoalescingHighWatermark", ARG_STR, get_coalescing_high_watermark, NULL,
        CTX_STORAGEHINTS, "8"},

    {"CoalescingLowWatermark", ARG_INT, get_coalescing_low_watermark, NULL,
        CTX_STORAGEHINTS, "1"},

    /* This option specifies the method used for trove.  The method specifies
     * how both metadata and data are stored and managed by the OrangeFS servers.
     * Currently the
     * alt-aio method is the default.  Possible methods are:
     *
     * <c>alt-aio</c>  This uses a thread-based implementation of Asynchronous IO.
     *
     * <c>directio</c>  This uses a direct I/O implementation to perform I/O
     * operations to datafiles.  This method may give significant performance
     * improvement if OrangeFS servers are running over shared storage, especially
     * for large I/O accesses.  For local storage, including RAID setups,
     * the alt-aio method is recommended.
     *
     * <c>null-aio</c>  This method is an implementation 
     * that does no disk I/O at all
     * and is only useful for development or debugging purposes.  It can
     * be used to test the performance of the network without doing I/O to disk.
     * <c>dbpf</c>  Uses the system's Linux AIO implementation.  No longer
     * recommended in production environments.
     *
     * Note that this option can be specified in either the <a href="#Defaults">
     * Defaults</a> context of fs.conf, or in a file system specific 
     * <a href="#StorageHints">StorageHints</a>
     * context, but the semantics of TroveMethod in the 
     * <a href="#Defaults">Defaults</a>
     * context is different from other options.  The TroveMethod in the
     * <a href="#Defaults">Defaults</a> context only specifies which 
     * method is used at
     * server initialization.  It does not specify the default TroveMethod
     * for all the file systems the server supports.  To set the TroveMethod
     * for a file system, the TroveMethod must be placed in the 
     * <a href="#StorageHints">StorageHints</a> context for that file system.
     */
    {"TroveMethod", ARG_STR, get_trove_method, NULL, 
        CTX_DEFAULTS|CTX_STORAGEHINTS, "alt-aio"},

    /* Specifies the file system's key for use in HMAC-based digests of
     * client operations.
     */
    {"SecretKey",ARG_STR, get_secret_key,NULL,CTX_FILESYSTEM,NULL},

    /* Specifies the size of the small file transition point */
    {"SmallFileSize", ARG_INT, get_small_file_size, NULL, CTX_FILESYSTEM, NULL},

    /* Specifies the number of threads that should be started to service
     * Direct I/O operations.
     */
    {"DirectIOThreadNum", ARG_INT, directio_thread_num, NULL,
        CTX_STORAGEHINTS, "30"},

    /* Specifies the number of operations to service at once in Direct I/O mode.
     */
    {"DirectIOOpsPerQueue", ARG_INT, directio_ops_per_queue, NULL,
        CTX_STORAGEHINTS, "10"},

    /* Specifies the timeout in Direct I/O to wait before checking the next queue. */
    {"DirectIOTimeout", ARG_INT, directio_timeout, NULL,
        CTX_STORAGEHINTS, "1000"},

    /* Specifies the number of partitions to use for tree communication. */
    {"TreeWidth", ARG_INT, tree_width, NULL,
        CTX_FILESYSTEM, "2"},

    /* Specifies the minimum number of servers to contact 
     * before tree communication kicks in.
     */
    {"TreeThreshold", ARG_INT, tree_threshold, NULL,
        CTX_FILESYSTEM, "2"},

    /* Specifies the default for initial number of servers to hold directory entries. */
    {"DistrDirServersInitial", ARG_INT, distr_dir_servers_initial, NULL,
        CTX_FILESYSTEM, "1"},

    /* Specifies the default for maximum number of servers to hold directory entries. */
    {"DistrDirServersMax", ARG_INT, distr_dir_servers_max, NULL,
        CTX_FILESYSTEM, "4"},

    /* Specifies the default for number of directory entries on a server before splitting. */
    {"DistrDirSplitSize", ARG_INT, distr_dir_split_size, NULL,
        CTX_FILESYSTEM, "10000"},

    LAST_OPTION
};

/*
 * Function: PINT_parse_config
 *
 * Params:   struct server_configuration_s*,
 *           global_config_filename - common config file for all servers
 *                                    and clients
 *           server_alias_name      - alias (if any) provided for this server
 *                                    client side can provide to check
 *                                    for a local server
 *           server_flag            - true if running on a server
 *
 * Returns:  0 on success; 1 on failure
 *
 */
int PINT_parse_config(struct server_configuration_s *config_obj,
                      char *global_config_filename,
                      char *server_alias_name,
                      int server_flag)
{
    struct server_configuration_s *config_s;
    configfile_t *configfile = (configfile_t *)0;

    if (!config_obj)
    {
        gossip_err("Invalid server_configuration_s object\n");
        return 1;
    }

    /* static global assignment */
    config_s = config_obj;
    memset(config_s, 0, sizeof(struct server_configuration_s));

    if ((server_flag & PARSE_CONFIG_SERVER) && !server_alias_name)
    {
        gossip_err("Server alias not provided for server config\n");
        return 1;
    }
    if (server_alias_name)
    {
        config_s->server_alias = strdup(server_alias_name);
    }
    else
    {
        config_s->server_alias = NULL;
    }

    /* set some global defaults for optional parameters */
    config_s->logstamp_type = GOSSIP_LOGSTAMP_DEFAULT;
    config_s->server_job_bmi_timeout = PVFS2_SERVER_JOB_BMI_TIMEOUT_DEFAULT;
    config_s->server_job_flow_timeout = PVFS2_SERVER_JOB_FLOW_TIMEOUT_DEFAULT;
    config_s->client_job_bmi_timeout = PVFS2_CLIENT_JOB_BMI_TIMEOUT_DEFAULT;
    config_s->client_job_flow_timeout = PVFS2_CLIENT_JOB_FLOW_TIMEOUT_DEFAULT;
    config_s->client_retry_limit = PVFS2_CLIENT_RETRY_LIMIT_DEFAULT;
    config_s->client_retry_delay_ms = PVFS2_CLIENT_RETRY_DELAY_MS_DEFAULT;
    config_s->trove_max_concurrent_io = 16;

    if (cache_config_files(config_s, global_config_filename))
    {
        return 1;
    }
    assert(config_s->fs_config_buflen && config_s->fs_config_buf);

    /* read in the fs.conf defaults config file */
    config_s->configuration_context = CTX_GLOBAL;
    configfile = PINT_dotconf_create(config_s->fs_config_filename,
                                     NULL,
                                     0,
                                     options,
                                     (void *)config_s, 
                                     CASE_INSENSITIVE);
    if (!configfile)
    {
        gossip_err("Error opening config file %s\n",
                   config_s->fs_config_filename);
        return 1;
    }
    config_s->private_data = configfile;
    configfile->errorhandler = (dotconf_errorhandler_t)errorhandler;
    configfile->contextchecker = (dotconf_contextchecker_t)contextchecker;
    
    if(PINT_dotconf_command_loop(configfile) == 0)
    {
        /* NOTE: dotconf error handler will log message */
        return 1;
    }
    PINT_dotconf_cleanup(configfile);

    /* only the first config file must pass all of the remaining checks
     * others are either partial configs or updates to dynamic parts of
     * the config.  The issues addressed in these checks should not
     * change after the initial load
     */
    if (!(server_flag & PARSE_CONFIG_INIT))
    {
        return 0;
    }

/* V3 replaced this with code during parse - exit-serverdef */
#if 0
    /* set up reference to local server for client */
    /* V3 probably more dlicate here since server nneds to find his
     * address for lots of things - would be nice if we didn't need a
     * commandline argument.  Should run if we have not already found
     * the ME record, try to locate, and if found, set ME attribute and
     * make sure we will write an updated config.
     */
    if (server_alias_name) 
    {
        struct host_alias_s *halias;
        /* V3 host index is probably obsolete */
        halias = find_host_alias_ptr_by_alias(config_s,
                                              server_alias_name,
                                              &config_s->host_index);
        if (!halias || !halias->bmi_address) 
        {
            if (server_flag & PARSE_CONFIG_SERVER)
            {
                gossip_err("Configuration file error. "
                           "No host ID specified for alias %s.\n",
                           server_alias_name);
                return 1;
            }
        }
        else
        {
            /* save alias bmi_address */
            config_s->host_id = strdup(halias->bmi_address);
            /* save the host_sid */
            config_s->host_sid = halias->host_sid;
        }
    }
#endif

    /* check for errors */
    if ((server_flag & PARSE_CONFIG_SERVER) && !config_s->data_path)
    {
        gossip_err("Configuration file error. "
                   "No data storage path specified for alias %s.\n",
                   server_alias_name);
        return 1;
    }

    if ((server_flag & PARSE_CONFIG_SERVER) && !config_s->meta_path)
    {
        gossip_err("Configuration file error. "
                   "No metadata storage path specified for alias %s.\n",
                   server_alias_name);
        return 1;
    }

    if ((server_flag & PARSE_CONFIG_SERVER) && !config_s->config_path)
    {
        gossip_err("Configuration file error. "
                   "No configuration storage path specified for alias %s.\n",
                   server_alias_name);
        return 1;
    }

    if (!config_s->bmi_modules)
    {
        gossip_err("Configuration file error. "
                   "No BMI modules specified.\n");
        return 1;
    }

    /* We set to the default flow module since there's only one.
    */
    if (!config_s->flow_modules)
    {
        gossip_err("Configuration file error. No flow module specified\n");
        return 1;
    }
    
    /* Users don't need to learn about this unless they want to
    */
    if (!config_s->perf_update_interval)
    {
        gossip_err("Configuration file error.  "
                   "No PerfUpdateInterval specified.\n");
        return 1;
    }

#ifdef ENABLE_SECURITY_KEY
    if ((server_flag & PARSE_CONFIG_SERVER) && !config_s->keystore_path)
    {
        gossip_err("Configuration file error. No keystore path specified.\n");
        return 1;
    }
#endif

#if defined(ENABLE_SECURITY_KEY) || defined(ENABLE_SECURITY_CERT)
    if ((server_flag & PARSE_CONFIG_SERVER) && !config_s->serverkey_path)
    {
        gossip_err("Configuration file error. No server key path "
                   "specified.\n");
        return 1;
    }
#endif

#ifdef ENABLE_SECURITY_CERT
    if (server_flag && !config_s->ca_file)
    {
        gossip_err("Configuration file error. No CA certificate path "
                   "specified.\n");
        return 1;
    }
#endif

    return 0;
}

const char *contextchecker(command_t *cmd, unsigned long mask)
{
    struct server_configuration_s *config_s = cmd->context;

    if(!(mask & config_s->configuration_context))
    {
        return "Option can't be defined in that context";
    }
    return NULL;
}
    
FUNC_ERRORHANDLER(errorhandler)
{
    gossip_err("Error: %s line %ld: %s", configfile->filename,
               configfile->line, msg);
    return(1);
}

DOTCONF_CB(get_logstamp)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    if(config_s->configuration_context == CTX_SERVER_OPTIONS &&
       config_s->my_server_options == 0)
    {
        return NULL;
    }
    if(!strcmp(cmd->data.str, "none"))
    {
        config_s->logstamp_type = GOSSIP_LOGSTAMP_NONE;
    }
    else if(!strcmp(cmd->data.str, "usec"))
    {
        config_s->logstamp_type = GOSSIP_LOGSTAMP_USEC;
    }
    else if(!strcmp(cmd->data.str, "datetime"))
    {
        config_s->logstamp_type = GOSSIP_LOGSTAMP_DATETIME;
    }
    else if(!strcmp(cmd->data.str, "thread"))
    {
        config_s->logstamp_type = GOSSIP_LOGSTAMP_THREAD;
    }
    else
    {
        return("LogStamp tag (if specified) must have one of the following values: none, usec, or datetime.\n");
    }

    return NULL;
}


DOTCONF_CB(get_storage_path)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    if(config_s->configuration_context == CTX_SERVER_OPTIONS &&
       config_s->my_server_options == 0)
    {
        return NULL;
    }

    if( config_s->data_path )
    {
        free(config_s->data_path);
    }

    if( config_s->meta_path )
    {
        free(config_s->meta_path);
    }

    config_s->data_path = (cmd->data.str ? strdup(cmd->data.str) : NULL);
    config_s->meta_path = (cmd->data.str ? strdup(cmd->data.str) : NULL);
    return NULL;
}

DOTCONF_CB(get_data_path)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    if(config_s->configuration_context == CTX_SERVER_OPTIONS &&
       config_s->my_server_options == 0)
    {
        return NULL;
    }
    if (config_s->data_path)
    {
        free(config_s->data_path);
    }

    config_s->data_path = (cmd->data.str ? strdup(cmd->data.str) : NULL);
    return NULL;
}

DOTCONF_CB(get_meta_path)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    if(config_s->configuration_context == CTX_SERVER_OPTIONS &&
       config_s->my_server_options == 0)
    {
        return NULL;
    }
    if (config_s->meta_path)
    {
        free(config_s->meta_path);
    }

    config_s->meta_path = (cmd->data.str ? strdup(cmd->data.str) : NULL);
    return NULL;
}

DOTCONF_CB(enter_defaults_context)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    const char *error = NULL;
    config_s->configuration_context = CTX_DEFAULTS;

    /* We initialize security defaults here also, so they are set even
       if the <Security> context is omitted. */
    if ((error = PINT_dotconf_set_defaults(cmd->configfile, CTX_SECURITY)))
    {
        return error;
    }
    return PINT_dotconf_set_defaults(cmd->configfile, CTX_DEFAULTS);
}

DOTCONF_CB(exit_defaults_context)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_GLOBAL;
    return NULL;
}

DOTCONF_CB(enter_security_context)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    config_s->prev_context = config_s->configuration_context;
    config_s->configuration_context = CTX_SECURITY;

    /* note: defaults set in enter_defaults_context */
    return NULL;
}

DOTCONF_CB(exit_security_context)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = config_s->prev_context;
    return NULL;
}

DOTCONF_CB(enter_server_context)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_SERVER;
    return NULL;
}

DOTCONF_CB(exit_server_context)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_GLOBAL;
    return NULL;
}

DOTCONF_CB(enter_serverdef_context)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    /* begin defining a new server */
    if (config_s->new_host)
    {
        /* There is a host from a previous decl hanging around */
        /* gossip_error("left over host being removed"); */
        free_host_alias(config_s->new_host);
    }
    config_s->new_host = (host_alias_t *)malloc(sizeof(host_alias_t));
    config_s->configuration_context = CTX_SERVERDEF;
    return NULL;
}

DOTCONF_CB(exit_serverdef_context)
{
    int ret;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_SERVER;
    /* end defining a new server */
    if (!config_s->new_host)
    {
        return "Error: missing host definition\n";
    }
    if (!is_valid_host(config_s->new_host))
    {
        free_host_alias(config_s->new_host);
        config_s->new_host = NULL;
        return "Error: incomplete host definition\n";
    }

    /* convert string SID to binary */
    ret = PVFS_SID_str2bin(config_s->new_host->host_sid_text,
                           &config_s->new_host->host_sid);
    if (ret != 0)
    {
        return "Error: server SID is not a propoer UUID\n";
    }

    /* add main record to SID cache */
    ret = SID_add(&config_s->new_host->host_sid,
                  0, /* internal BMI ID */
                  config_s->new_host->bmi_address,
                  config_s->new_host->attributes);
    if (ret != 0)
    {
        return "Error: server not added to SID cache\n";
    }

    /* see if this record refers to this server, and save config
     * info if it does
     */
    if (!strcmp(config_s->server_alias, config_s->new_host->host_alias))
    {
        /* This is our address - save the information */
        if (config_s->host_id != NULL)
        {
            /* something already set as this host */
            config_s->new_host = NULL;
            return "Duplicate address found for this server\n";
        }
        config_s->host_sid = config_s->new_host->host_sid;
        config_s->host_id = strdup(config_s->new_host->bmi_address);
        config_s->new_host->server_type |= SID_SERVER_ME;
    }

    /* add type records to SID cache */
    ret = SID_update_type(&config_s->new_host->host_sid,
                          config_s->new_host->server_type);
    if (ret != 0)
    {
        return "Error: server type records not added to SID cache\n";
    }

    /* add to config list of aliases */
    /* create list if this is first one */
    if (!config_s->host_aliases)
    {
        config_s->host_aliases = PINT_llist_new();
    }

    PINT_llist_add_to_tail(config_s->host_aliases,
                           (void *)config_s->new_host);

    config_s->new_host = NULL;
    return NULL;
}

DOTCONF_CB(enter_filesystem_context)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    struct filesystem_configuration_s *fs_conf = NULL;

    if (config_s->host_aliases == NULL)
    {
        return("Error in context.  FileSystem tag cannot "
                   "be declared before an Aliases tag.\n");
    }

    fs_conf = (struct filesystem_configuration_s *)
            malloc(sizeof(struct filesystem_configuration_s));
    assert(fs_conf); /* TODO: replace this with error handleing */
    memset(fs_conf,0,sizeof(struct filesystem_configuration_s));

    /* fill any fs defaults here */
    fs_conf->flowproto = FLOWPROTO_DEFAULT;
    fs_conf->encoding = PVFS2_ENCODING_DEFAULT;
    fs_conf->trove_sync_meta = TROVE_SYNC;
    fs_conf->trove_sync_data = TROVE_SYNC;
    fs_conf->fp_buffer_size = -1;
    fs_conf->fp_buffers_per_flow = -1;
    fs_conf->file_stuffing = 1;

    if (!config_s->file_systems)
    {
        config_s->file_systems = PINT_llist_new();
    }
    PINT_llist_add_to_head(config_s->file_systems,(void *)fs_conf);
    assert(PINT_llist_head(config_s->file_systems) == (void *)fs_conf);
    config_s->configuration_context = CTX_FILESYSTEM;

    return PINT_dotconf_set_defaults(cmd->configfile, CTX_FILESYSTEM);
}

DOTCONF_CB(exit_filesystem_context)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    /*
      make sure last fs config object is valid
      (i.e. has all required values filled in)
    */
    if (!is_populated_filesystem_configuration(fs_conf))
    {
        gossip_err("Error: File system configuration is invalid!\n");
        return("Possible Error in context.  Cannot have /FileSystem "
                   "tag before all file system attributes are declared.\n");
    }

    config_s->configuration_context = CTX_GLOBAL;
    return NULL;
}

DOTCONF_CB(enter_rootsrvs_context)
{
    struct server_configuration_s *config_s = 
                    (struct server_configuration_s *)cmd->context;
    static int first_time = 1;

    /* V3 this is NOT right */
    if (!first_time)
    {
        /* can only specify root servers once, this is an error */
        gossip_err("Error, invalid filesystem configuration, only one "
                   "RootServers section can be specified");
        return("Config file format error - only one RootServers context "
               "can be specified\n");
    }

    first_time = 0;
    config_s->configuration_context = CTX_ROOTSERVERS;
    return NULL;
}

DOTCONF_CB(exit_rootsrvs_context)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    struct filesystem_configuration_s *fs_conf =
            (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);
    struct host_alias_s *cur_root_server;
    PINT_llist *cur = NULL;
    int i;

    fs_conf->root_sid_array = (PVFS_SID *)malloc(fs_conf->root_sid_count *
                                                       sizeof(PVFS_SID));
    if(!fs_conf->root_sid_array)
    {
        gossip_err("Unable to malloc memory for root_sid_array\n");
        return("Error, ran out of memory for root_sid_array\n");
    }

    /* run through the root_servers list, look up address in the
     * SIDcache and build the root_sid_array
     */
    cur = fs_conf->root_servers;
    for (i = 0; i < fs_conf->root_sid_count; i++)
    {
        if(!cur)
        {
            gossip_err("Error, ran out of root servers too soon\n");
            return("Error, ran out of root servers too soon\n");
        }
        cur_root_server = (struct host_alias_s *)PINT_llist_head(cur);
        if (!cur_root_server)
        {
            return("Error, no server record for this root server\n");
        }
        if (PVFS_SID_EQ(&cur_root_server->host_sid, &PVFS_SID_NULL))
        {
            /* we dont have a binary SID - convert from text */
            /* should have done this previously and not need this */
            if (!cur_root_server->host_sid_text)
            {
                return "Error, host_alias has no SID";
            }
            /* convert SID into bin */
            PVFS_SID_str2bin(cur_root_server->host_sid_text,
                             &fs_conf->root_sid_array[i]);
        }
        else
        {
            /* we have a binary, just copy it */
            fs_conf->root_sid_array[i] = cur_root_server->host_sid;
        }
        
        cur = PINT_llist_next(cur);
    }

    config_s->configuration_context = CTX_FILESYSTEM;
    return NULL;
}

DOTCONF_CB(enter_storage_hints_context)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_STORAGEHINTS;

    return PINT_dotconf_set_defaults(cmd->configfile, CTX_STORAGEHINTS);
}

DOTCONF_CB(exit_storage_hints_context)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_FILESYSTEM;
    return NULL;
}

DOTCONF_CB(enter_export_options_context)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_EXPORT;

    return PINT_dotconf_set_defaults(cmd->configfile, CTX_EXPORT);
}

DOTCONF_CB(exit_export_options_context)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_FILESYSTEM;
    return NULL;
}

DOTCONF_CB(enter_server_options_context)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_SERVER_OPTIONS;

    return PINT_dotconf_set_defaults(cmd->configfile, CTX_SERVER_OPTIONS);
}

DOTCONF_CB(exit_server_options_context)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_GLOBAL;
    config_s->my_server_options = 0;
    return NULL;
}

DOTCONF_CB(check_this_server)
{
    struct server_configuration_s *config_s =
            (struct server_configuration_s *)cmd->context;

    if(is_valid_alias(config_s->host_aliases, cmd->data.str))
    {
        /* if the Server option specifies our alias, enable setting
         * of server specific options
         */
        if(config_s->server_alias &&
           strcmp(config_s->server_alias, cmd->data.str) == 0)
        {
            config_s->my_server_options = 1;
        }
    }
    else
    {
        return "Unrecognized alias specified.\n";
    }
    return NULL;
}

DOTCONF_CB(enter_distribution_context)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_DISTRIBUTION;
    return NULL;
}

DOTCONF_CB(exit_distribution_context)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    config_s->configuration_context = CTX_FILESYSTEM;
    return NULL;
}

DOTCONF_CB(get_unexp_req)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    if(config_s->configuration_context == CTX_SERVER_OPTIONS &&
       config_s->my_server_options == 0)
    {
        return NULL;
    }
    config_s->initial_unexpected_requests = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_tcp_buffer_receive)
{
    struct server_configuration_s *config_s =
            (struct server_configuration_s *)cmd->context;
    config_s->tcp_buffer_size_receive = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_tcp_buffer_send)
{
    struct server_configuration_s *config_s =
            (struct server_configuration_s *)cmd->context;
    config_s->tcp_buffer_size_send = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_tcp_bind_specific)
{
    struct server_configuration_s *config_s =
            (struct server_configuration_s *)cmd->context;

    if(config_s->configuration_context == CTX_SERVER_OPTIONS &&
       config_s->my_server_options == 0)
    {
        return NULL;
    }
    if(strcasecmp(cmd->data.str, "yes") == 0)
    {
        config_s->tcp_bind_specific = 1;
    }
    else if(strcasecmp(cmd->data.str, "no") == 0)
    {
        config_s->tcp_bind_specific = 0;
    }
    else
    {
        return("TCPBindSpecific value must be 'yes' or 'no'.\n");
    }

    return NULL;
}

DOTCONF_CB(get_server_job_bmi_timeout)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    if(config_s->configuration_context == CTX_SERVER_OPTIONS &&
       config_s->my_server_options == 0)
    {
        return NULL;
    }
    config_s->server_job_bmi_timeout = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_server_job_flow_timeout)
{
    struct server_configuration_s *config_s = 
             (struct server_configuration_s *)cmd->context;
    if(config_s->configuration_context == CTX_SERVER_OPTIONS &&
       config_s->my_server_options == 0)
    {
        return NULL;
    }
    config_s->server_job_flow_timeout = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_client_job_bmi_timeout)
{
    struct server_configuration_s *config_s = 
             (struct server_configuration_s *)cmd->context;
    config_s->client_job_bmi_timeout = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_client_job_flow_timeout)
{
    struct server_configuration_s *config_s = 
             (struct server_configuration_s *)cmd->context;
    config_s->client_job_flow_timeout = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_client_retry_limit)
{
    struct server_configuration_s *config_s = 
             (struct server_configuration_s *)cmd->context;
    config_s->client_retry_limit = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_client_retry_delay)
{
    struct server_configuration_s *config_s = 
             (struct server_configuration_s *)cmd->context;
    config_s->client_retry_delay_ms = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_perf_update_interval)
{
    struct server_configuration_s *config_s = 
             (struct server_configuration_s *)cmd->context;
    config_s->perf_update_interval = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_logfile)
{
    struct server_configuration_s *config_s = 
             (struct server_configuration_s *)cmd->context;
    /* free whatever was added in set_defaults phase */
    if(config_s->configuration_context == CTX_SERVER_OPTIONS &&
       config_s->my_server_options == 0)
    {
        return NULL;
    }
    if (config_s->logfile)
    {
        free(config_s->logfile);
    }
    config_s->logfile = (cmd->data.str ? strdup(cmd->data.str) : NULL);
    return NULL;
}

DOTCONF_CB(get_logtype)
{
    struct server_configuration_s *config_s = 
             (struct server_configuration_s *)cmd->context;
    /* free whatever was added in set_defaults phase */
    if(config_s->configuration_context == CTX_SERVER_OPTIONS &&
       config_s->my_server_options == 0)
    {
        return NULL;
    }
    if (config_s->logtype)
    {
        free(config_s->logtype);
    }
    config_s->logtype = (cmd->data.str ? strdup(cmd->data.str) : NULL);
    return NULL;
}


DOTCONF_CB(get_event_logging_list)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    int i = 0, len = 0;
    char buf[512] = {0};
    char *ptr = buf;

    if(config_s->configuration_context == CTX_SERVER_OPTIONS &&
       config_s->my_server_options == 0)
    {
        return NULL;
    }
    if (config_s->event_logging != NULL) 
    {
        free(config_s->event_logging);
        config_s->event_logging = NULL;
    }
    for(i = 0; i < cmd->arg_count; i++)
    {
        strncat(ptr, cmd->data.list[i], 512 - len);
        len += strlen(cmd->data.list[i]);
    }
    config_s->event_logging = strdup(buf);
    return NULL;
}

DOTCONF_CB(get_config_path)
{
    struct server_configuration_s *config_s = 
             (struct server_configuration_s *)cmd->context;
    /* free whatever was added in set_defaults phase */
    if(config_s->configuration_context == CTX_SERVER_OPTIONS &&
       config_s->my_server_options == 0)
    {
        return NULL;
    }
    if (config_s->config_path)
    {
        free(config_s->config_path);
    }
    config_s->config_path = (cmd->data.str ? strdup(cmd->data.str) : NULL);
    return NULL;
}

DOTCONF_CB(get_event_tracing)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    if(config_s->configuration_context == CTX_SERVER_OPTIONS &&
       config_s->my_server_options == 0)
    {
        return NULL;
    }
    if(!strcmp(cmd->data.str, "yes"))
    {
        config_s->enable_events = 1;
    }
    else
    {
        config_s->enable_events = 0;
    }
    return NULL;
}

DOTCONF_CB(get_flow_module_list)
{
    int i = 0, len = 0;
    char buf[512] = {0};
    char *ptr = buf;

    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    if (config_s->flow_modules != NULL)
    {
        free(config_s->flow_modules);
        config_s->flow_modules = NULL;
    }
    for(i = 0; i < cmd->arg_count; i++)
    {
        strncat(ptr, cmd->data.list[i], 512 - len);
        len += strlen(cmd->data.list[i]);
    }
    config_s->flow_modules = strdup(buf);
    return NULL;
}

static void free_list_of_strings(int list_count, char ***new_list)
{
    int i;

    if (new_list && *new_list != NULL)
    {
        for (i = 0; i < list_count; i++)
        {
            free((*new_list)[i]);
            (*new_list)[i] = NULL;
        }
        free(*new_list);
        *new_list = NULL;
    }
    return;
}

/*
 * Given a parsed_list containing list_count number of strings,
 * create a new array of pointers which holds a duplicate list
 * of all the strings present in the original parsed list.
 * Used as a helper function by all the routines/keywords that require a list
 * of strings as their options/arguments.
 */
static int get_list_of_strings(int list_count,
                               char **parsed_list,
                               char ***new_list)
{
    int i;

    *new_list = (char **) calloc(list_count, sizeof(char *));
    if (*new_list == NULL)
    {
        errno = ENOMEM;
        return -1;
    }
    for (i = 0; i < list_count; i++)
    {
        (*new_list)[i] = strdup(parsed_list[i]);
        if ((*new_list)[i] == NULL)
        {
            break;
        }
    }
    if (i != list_count)
    {
        int j;
        for (j = 0; j < i; j++)
        {
            free((*new_list)[j]);
            (*new_list)[j] = NULL;
        }
        free(*new_list);
        *new_list = NULL;
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

/*
 * Given a set/list of BMI addresses suffixed with @ and a netmask,
 * this function will populate a netmasks array that contains
 * the integer subsqeuent to the @ symbol.
 * i.e given tcp://192.168.2.0@24 we will have tcp://192.168.2.0,
 * 24 as the netmask
 */
static int setup_netmasks(int count, char **bmi_address, int *netmasks)
{
    int i;
    for (i = 0; i < count; i++)
    {
        /* 
         * if we find a @ then we parse the 
         * chars at the end of it and make sure
         * that it is less than equal to 32,
         * else we check if there is a wildcard (*)
         * present in which case we set it to -1
         * else we assume that it is equal to 32.
         */
        char *special_char = strchr(bmi_address[i], '@'), *ptr = NULL;
        if (special_char == NULL)
        {
            special_char = strchr(bmi_address[i], '*');
            if (special_char == NULL)
            {
                netmasks[i] = 32;
            }
            else
            {
                netmasks[i] = -1;
            }
        }
        else
        {
            *special_char = '\0';
            netmasks[i] = strtol(special_char + 1, &ptr, 10);
            if (*ptr != '\0' || netmasks[i] < 0
                    || netmasks[i] > 32)
            {
                return -1;
            }
        }
        gossip_debug(GOSSIP_SERVER_DEBUG, "Parsed %s:%d\n",
                     bmi_address[i], netmasks[i]);
    }
    return 0;
}

DOTCONF_CB(get_root_squash)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);
    assert(fs_conf); /* TODO: replace with error handling */

    if (cmd->arg_count == 0)
    {
        fs_conf->exp_flags &= ~TROVE_EXP_ROOT_SQUASH;
    }
    else 
    {
        fs_conf->exp_flags |= TROVE_EXP_ROOT_SQUASH;
        fs_conf->root_squash_netmasks = 
                    (int *) calloc(cmd->arg_count, sizeof(int));
        if (fs_conf->root_squash_netmasks == NULL)
        {
            fs_conf->root_squash_count = 0;
            return("Could not allocate memory for root_squash_netmasks\n");
        }
        if (get_list_of_strings(cmd->arg_count, cmd->data.list,
                                &fs_conf->root_squash_hosts) < 0)
        {
            free(fs_conf->root_squash_netmasks);
            fs_conf->root_squash_netmasks = NULL;
            fs_conf->root_squash_count = 0;
            return("Could not allocate memory for root_squash_hosts\n");
        }
        fs_conf->root_squash_count = cmd->arg_count;
        /* Setup the netmasks */
        if (setup_netmasks(fs_conf->root_squash_count,
                           fs_conf->root_squash_hosts, 
                           fs_conf->root_squash_netmasks) < 0)
        {
            free(fs_conf->root_squash_netmasks);
            fs_conf->root_squash_netmasks = NULL;
            free_list_of_strings(fs_conf->root_squash_count,
                                &fs_conf->root_squash_hosts);
            fs_conf->root_squash_count = 0;
            return("Could not setup netmasks for root_squash_hosts\n");
        }
        gossip_debug(GOSSIP_SERVER_DEBUG,
                     "Parsed %d RootSquash wildcard entries\n",
                     cmd->arg_count);
    }
    return NULL;
}

DOTCONF_CB(get_root_squash_exceptions)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
                          PINT_llist_head(config_s->file_systems);
    assert(fs_conf); /* TODO: replace with error handling */

    if (cmd->arg_count != 0)
    {
        fs_conf->root_squash_exceptions_netmasks =
                      (int *) calloc(cmd->arg_count, sizeof(int));
        if (fs_conf->root_squash_exceptions_netmasks == NULL)
        {
            fs_conf->root_squash_exceptions_count = 0;
            return("Could not allocate memory for "
                   "root_squash_exceptions_netmasks\n");
        }
        if (get_list_of_strings(cmd->arg_count, cmd->data.list,
                    &fs_conf->root_squash_exceptions_hosts) < 0)
        {
            free(fs_conf->root_squash_exceptions_netmasks);
            fs_conf->root_squash_exceptions_netmasks = NULL;
            fs_conf->root_squash_exceptions_count = 0;
            return("Could not allocate memory for "
                   "root_squash_exceptions_hosts\n");
        }
        fs_conf->root_squash_exceptions_count = cmd->arg_count;
        /* Setup the netmasks */
        if (setup_netmasks(fs_conf->root_squash_exceptions_count,
                           fs_conf->root_squash_exceptions_hosts, 
                           fs_conf->root_squash_exceptions_netmasks) < 0)
        {
            free(fs_conf->root_squash_exceptions_netmasks);
            fs_conf->root_squash_exceptions_netmasks = NULL;
            free_list_of_strings(fs_conf->root_squash_exceptions_count,
                                 &fs_conf->root_squash_exceptions_hosts);
            fs_conf->root_squash_exceptions_count = 0;
            return("Could not setup netmasks for "
                   "root_squash_exceptions_hosts\n");
        }
        gossip_debug(GOSSIP_SERVER_DEBUG,
                     "Parsed %d RootSquashExceptions wildcard entries\n",
                     cmd->arg_count);
    }
    return NULL;
}


DOTCONF_CB(get_read_only)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);
    assert(fs_conf); /* TODO: replace with error handling */
    
    if (cmd->arg_count == 0)
    {
        fs_conf->exp_flags &= ~TROVE_EXP_READ_ONLY;
    }
    else
    {
        fs_conf->exp_flags |= TROVE_EXP_READ_ONLY;
        fs_conf->ro_netmasks = (int *) calloc(cmd->arg_count, sizeof(int));
        if (fs_conf->ro_netmasks == NULL)
        {
            fs_conf->ro_count = 0;
            return("Could not allocate memory for ro_netmasks\n");
        }
        if (get_list_of_strings(cmd->arg_count,
                                cmd->data.list,
                                &fs_conf->ro_hosts) < 0)
        {
            free(fs_conf->ro_netmasks);
            fs_conf->ro_netmasks = NULL;
            fs_conf->ro_count = 0;
            return("Could not allocate memory for ro_hosts\n");
        }
        fs_conf->ro_count  = cmd->arg_count;
        if (setup_netmasks(fs_conf->ro_count,
                           fs_conf->ro_hosts,
                           fs_conf->ro_netmasks) < 0)
        {
            free(fs_conf->ro_netmasks);
            fs_conf->ro_netmasks = NULL;
            free_list_of_strings(fs_conf->ro_count, &fs_conf->ro_hosts);
            fs_conf->ro_count = 0;
            return("Could not setup netmasks for ro_netmasks\n");
        }
        gossip_debug(GOSSIP_SERVER_DEBUG, "Parsed %d ro wildcard entries\n",
                     cmd->arg_count);
    }
    return NULL;
}

DOTCONF_CB(get_all_squash)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
             (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
             PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    if (cmd->arg_count == 0)
    {
        fs_conf->exp_flags &= ~TROVE_EXP_ALL_SQUASH;
    }
    else 
    {
        fs_conf->exp_flags |= TROVE_EXP_ALL_SQUASH;
        fs_conf->all_squash_netmasks =
                   (int *) calloc(cmd->arg_count, sizeof(int));
        if (fs_conf->all_squash_netmasks == NULL)
        {
            fs_conf->all_squash_count = 0;
            return("Could not allocate memory for all_squash_netmasks\n");
        }
        if (get_list_of_strings(cmd->arg_count,
                                cmd->data.list,
                                &fs_conf->all_squash_hosts) < 0)
        {
            free(fs_conf->all_squash_netmasks);
            fs_conf->all_squash_netmasks = NULL;
            fs_conf->all_squash_count = 0;
            return("Could not allocate memory for all_squash_hosts\n");
        }
        fs_conf->all_squash_count = cmd->arg_count;
        if (setup_netmasks(fs_conf->all_squash_count,
                           fs_conf->all_squash_hosts, 
                           fs_conf->all_squash_netmasks) < 0)
        {
            free(fs_conf->all_squash_netmasks);
            fs_conf->all_squash_netmasks = NULL;
            free_list_of_strings(fs_conf->all_squash_count,
                                 &fs_conf->all_squash_hosts);
            fs_conf->all_squash_count = 0;
            return("Could not setup netmasks for all_squash_hosts\n");
        }
        gossip_debug(GOSSIP_SERVER_DEBUG,
                     "Parsed %d AllSquash wildcard entries\n", 
                     cmd->arg_count);
    }
    return NULL;
}

DOTCONF_CB(get_anon_uid)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    unsigned int tmp_var;
    int ret = -1;
    struct server_configuration_s *config_s = 
             (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
             PINT_llist_head(config_s->file_systems);
    assert(fs_conf);
    ret = sscanf(cmd->data.str, "%u", &tmp_var);
    if(ret != 1)
    {
        return("AnonUID does not have a long long unsigned value.\n");
    }
    fs_conf->exp_anon_uid = tmp_var;
    return NULL;
}

DOTCONF_CB(get_anon_gid)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    unsigned int tmp_var;
    int ret = -1;
    struct server_configuration_s *config_s = 
             (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
             PINT_llist_head(config_s->file_systems);
    assert(fs_conf);
    ret = sscanf(cmd->data.str, "%u", &tmp_var);
    if(ret != 1)
    {
        return("AnonGID does not have a unsigned value.\n");
    }
    fs_conf->exp_anon_gid = tmp_var;
    return NULL;
}

DOTCONF_CB(get_bmi_module_list)
{
    int i = 0, len = 0;
    char buf[512] = {0};
    char *ptr = buf;

    struct server_configuration_s *config_s = 
             (struct server_configuration_s *)cmd->context;

    if (config_s->bmi_modules != NULL)
    {
        free(config_s->bmi_modules);
        config_s->bmi_modules = NULL;
    }
    for(i = 0; i < cmd->arg_count; i++)
    {
        strncat(ptr, cmd->data.list[i], 512 - len);
        len += strlen(cmd->data.list[i]);
    }
    config_s->bmi_modules = strdup(buf);
    return NULL;
}

#ifdef USE_TRUSTED

DOTCONF_CB(get_trusted_portlist)
{
    long port1, port2;
    char *separator = NULL, *option = NULL, *endptr = NULL;
    struct server_configuration_s *config_s = 
             (struct server_configuration_s *)cmd->context;

    option = strdup(cmd->data.str);
    if (option == NULL)
    {
        return("Could not allocate memory\n");
    }
    separator = strchr(option, '-');
    if (separator == NULL)
    {
        free(option);
        return("Portlist should be of the form <Port1-Port2>\n");
    }
    *separator = '\0';
    port1 = strtol(option, &endptr, 10);
    if (port1 < 0 || *endptr != '\0')
    {
        free(option);
        return("Portlist should be of the form <Port1-Port2>\n");
    }
    endptr = NULL;
    port2 = strtol(separator + 1, &endptr, 10);
    if (port2 < 0 || *endptr != '\0')
    {
        free(option);
        return("Portlist should be of the form <Port1-Port2>\n");
    }
    free(option);
    if (port2 < port1)
    {
        return("Portlist should be of the form <Port1-Port2> (Port1 <= Port2)\n");
    }
    config_s->allowed_ports[0] = port1;
    config_s->allowed_ports[1] = port2;
    /* okay, we enable trusted ports */
    config_s->ports_enabled = 1; 
    return NULL;
}

DOTCONF_CB(get_trusted_network)
{
    struct server_configuration_s *config_s = 
             (struct server_configuration_s *)cmd->context;

    if (cmd->arg_count == 0)
    {
        return NULL; 
    }

    config_s->allowed_masks = (int *) calloc(cmd->arg_count, sizeof(int));
    if (config_s->allowed_masks == NULL)
    {
        config_s->allowed_networks_count = 0;
        return("Could not allocate memory for allowed netmasks\n");
    }
    if (get_list_of_strings(cmd->arg_count,
                            cmd->data.list,
                            &config_s->allowed_networks) < 0)
    {
        free(config_s->allowed_masks);
        config_s->allowed_masks = NULL;
        config_s->allowed_networks_count = 0;
        return("Could not allocate memory for trusted networks\n");
    }
    config_s->allowed_networks_count = cmd->arg_count;
    /* Setup netmasks */
    if (setup_netmasks(config_s->allowed_networks_count,
                       config_s->allowed_networks,
                       config_s->allowed_masks) < 0)
    {
        free(config_s->allowed_masks);
        config_s->allowed_masks = NULL;
        free_list_of_strings(config_s->allowed_networks_count,
                             &config_s->allowed_networks);
        config_s->allowed_networks_count = 0;
        return("Parse error in netmask specification\n");
    }

    /* okay, we enable trusted network as well */
    config_s->network_enabled = 1;
    return NULL;
}

#endif

DOTCONF_CB(get_handle_recycle_timeout_seconds)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    fs_conf->handle_recycle_timeout_sec.tv_sec = (int)cmd->data.value;
    fs_conf->handle_recycle_timeout_sec.tv_usec = 0;

    return NULL;
}

static const char * replace_old_keystring(const char * oldkey)
{
    /* check for old keyval strings */
    if(!strcmp(oldkey, "dir_ent"))
    {
        return "de";
    }
    else if(!strcmp(oldkey, "datafile_handles"))
    {
        return "dh";
    }
    else if(!strcmp(oldkey, "metafile_dist"))
    {
        return "md";
    }
    else if(!strcmp(oldkey, "symlink_target"))
    {
        return "st";
    }

    return oldkey;
}


DOTCONF_CB(get_flow_buffer_size_bytes)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);
    fs_conf->fp_buffer_size = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_flow_buffers_per_flow)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);
    fs_conf->fp_buffers_per_flow = cmd->data.value;
    if(fs_conf->fp_buffers_per_flow < 2)
    {
        return("Error: FlowBuffersPerFlow must be at least 2.\n");
    }

    return NULL;
}

DOTCONF_CB(get_attr_cache_keywords_list)
{
    int i = 0, len = 0;
    char buf[512] = {0};
    char *ptr = buf;
    const char * rtok;
    struct filesystem_configuration_s *fs_conf = NULL;

    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);
    assert(fs_conf); /* TODO: replace with error handling */

    if (fs_conf->attr_cache_keywords != NULL)
    {
        char ** tokens;
        int token_count, j;

        token_count = PINT_split_string_list(&tokens,
                                             fs_conf->attr_cache_keywords);

        for(j = 0; j < token_count; ++j)
        {
            rtok = replace_old_keystring(tokens[j]);
            if(!strstr(buf, rtok))
            {
                len = strlen(rtok);
                strncat(ptr, rtok, len);
                strncat(ptr, ",", 1);
                ptr += len + 1;
            }
        }
       
        PINT_free_string_list(tokens, token_count);
        free(fs_conf->attr_cache_keywords);
    }

    for(i = 0; i < cmd->arg_count; i++)
    {
        char ** tokens;
        int token_count, j;

        token_count = PINT_split_string_list(&tokens, cmd->data.list[i]);

        for(j = 0; j < token_count; ++j)
        {
            rtok = replace_old_keystring(tokens[j]);
            if(!strstr(buf, rtok))
            {
                len = strlen(rtok);
                strncat(ptr, rtok, len);
                strncat(ptr, ",", 1);
                ptr += len + 1;
            }
        }

        PINT_free_string_list(tokens, token_count);
    }

    *ptr = '\0';
    fs_conf->attr_cache_keywords = strdup(buf);
    return NULL;
}

DOTCONF_CB(get_attr_cache_size)
{
    struct filesystem_configuration_s *fs_conf = NULL;

    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    fs_conf->attr_cache_size = (int)cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_attr_cache_max_num_elems)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    fs_conf->attr_cache_max_num_elems = (int)cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_file_stuffing)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    if(strcasecmp(cmd->data.str, "yes") == 0)
    {
        fs_conf->file_stuffing = 1;
    }
    else if(strcasecmp(cmd->data.str, "no") == 0)
    {
        fs_conf->file_stuffing = 0;
    }
    else
    {
        return("FileStuffing value must be 'yes' or 'no'.\n");
    }

    return NULL;
}


DOTCONF_CB(get_trove_sync_meta)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    if(strcasecmp(cmd->data.str, "yes") == 0)
    {
        fs_conf->trove_sync_meta = TROVE_SYNC;
    }
    else if(strcasecmp(cmd->data.str, "no") == 0)
    {
        fs_conf->trove_sync_meta = 0;
    }
    else
    {
        return("TroveSyncMeta value must be 'yes' or 'no'.\n");
    }
#ifndef HAVE_DB_DIRTY_READ
    if (fs_conf->trove_sync_meta != TROVE_SYNC)
    {
        gossip_err("WARNING: Forcing TroveSyncMeta to be yes instead of %s\n",
                   cmd->data.str);
        gossip_err("WARNING: Non-sync mode is NOT supported without "
                   "DB_DIRTY_READ support.\n");
        fs_conf->trove_sync_meta = TROVE_SYNC;
    }
#endif

    return NULL;
}

DOTCONF_CB(get_trove_sync_data)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);
    assert(fs_conf);

    if(strcasecmp(cmd->data.str, "yes") == 0)
    {
        fs_conf->trove_sync_data = TROVE_SYNC;
    }
    else if(strcasecmp(cmd->data.str, "no") == 0)
    {
        fs_conf->trove_sync_data = 0;
    }
    else
    {
        return("TroveSyncData value must be 'yes' or 'no'.\n");
    }

    return NULL;
}

DOTCONF_CB(get_db_cache_size_bytes)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    config_s->db_cache_size_bytes = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_trove_max_concurrent_io)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    if(config_s->configuration_context == CTX_SERVER_OPTIONS &&
       config_s->my_server_options == 0)
    {
        return NULL;
    }
    config_s->trove_max_concurrent_io = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_db_cache_type)
{
    struct server_configuration_s *config_s =
            (struct server_configuration_s *)cmd->context;
    
    if(strcmp(cmd->data.str, "sys") && strcmp(cmd->data.str, "mmap"))
    {
        return "Unsupported parameter supplied to DBCacheType option, must "
               "be either \"sys\" or \"mmap\"\n";
    }

    if (config_s->db_cache_type)
    {
        free(config_s->db_cache_type);
    }
    config_s->db_cache_type = strdup(cmd->data.str);
    if(!config_s->db_cache_type)
    {
        return "strdup() failure";
    }

    return NULL;
}

DOTCONF_CB(get_rootsrv)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    struct host_alias_s *root_alias = NULL;

    fs_conf = (struct filesystem_configuration_s *)
                            PINT_llist_head(config_s->file_systems);
    assert(fs_conf); /* TODO: replace with error handling */

    if (cmd->arg_count != 1)
    {
        return "Error: RootServer statement must include one alias\n";
    }

    /* look up alias */
    if(config_s->host_aliases &&
       !(root_alias = (host_alias_t *)PINT_llist_search(
                                                 config_s->host_aliases, 
                                                 (void *)cmd->data.list[0],
                                                 compare_aliases)))
    {
        return "Error: RootServer alias not found\n";
    }
    /* This doesn't tell us much - we have no way to id which root */
    /* update server type n SIDcache */
    SID_update_type(&root_alias->host_sid, SID_SERVER_ROOT);

    /* add to list of root servers for this fs */
    if (!fs_conf->root_servers)
    {
        fs_conf->root_servers = PINT_llist_new();
    }

    /* The SIDs here will later be copied into the root_sid_array of
     * the fs_config
     */
    PINT_llist_add_to_tail(fs_conf->root_servers, (void *)root_alias);

    /* increment the root_sid_count so we know how many there are */
    fs_conf->root_sid_count++;
    return NULL;
}

DOTCONF_CB(get_root_handle)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    PVFS_OID tmp_var;
    int ret = -1;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);
    assert(fs_conf); /* TODO: replace with error handling */

    ret = PVFS_OID_str2bin(cmd->data.str, &tmp_var);
    if(ret != 0)
    {
        return("RootHandle required argument is not a uuid value.\n");
    }
    fs_conf->root_handle = (PVFS_handle)tmp_var;
    return NULL;
}

DOTCONF_CB(get_root_dirdata_handle)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    PVFS_OID tmp_var;
    int ret = -1;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);
    assert(fs_conf); /* TODO: replace with error handling */

    ret = PVFS_OID_str2bin(cmd->data.str, &tmp_var);
    if(ret != 0)
    {
        return("RootDirdataHandle required argument is not a uuid value.\n");
    }
    fs_conf->root_dirdata_handle = (PVFS_handle)tmp_var;
    return NULL;
}

DOTCONF_CB(get_name)
{
    struct server_configuration_s *config_s = 
                          (struct server_configuration_s *)cmd->context;
    if (config_s->configuration_context == CTX_FILESYSTEM)
    {
        struct filesystem_configuration_s *fs_conf = NULL;

        fs_conf = (struct filesystem_configuration_s *)
                PINT_llist_head(config_s->file_systems);
        if (fs_conf->file_system_name)
        {
            gossip_err("WARNING: Overwriting %s with %s\n",
                       fs_conf->file_system_name,cmd->data.str);
        }
        fs_conf->file_system_name =
                (cmd->data.str ? strdup(cmd->data.str) : NULL);
    }
    else if (config_s->configuration_context == CTX_DISTRIBUTION)
    {
        if (0 == config_s->default_dist_config.name)
        {
            config_s->default_dist_config.name =
                    (cmd->data.str ? strdup(cmd->data.str) : NULL);
            config_s->default_dist_config.param_list = PINT_llist_new();
        }
        else
        {
            return "Only one distribution configuration is allowed.\n";
        }
    }
    return NULL;
}

DOTCONF_CB(get_filesystem_replication)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
                          (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);

    if (cmd->data.value < 1)
    {
        gossip_err("Metadata Replication Factor must be > 0");
        return NULL;
    }
    if (fs_conf->metadata_replication_factor)
    {
        gossip_err("WARNING: Overwriting %d with %d\n",
                   (int)fs_conf->metadata_replication_factor,
                   (int)cmd->data.value);
    }
    fs_conf->metadata_replication_factor = (int)cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_filesystem_collid)
{
    struct filesystem_configuration_s *fs_conf = NULL;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);
    if (fs_conf->coll_id)
    {
        gossip_err("WARNING: Overwriting %d with %d\n",
                   (int)fs_conf->coll_id, (int)cmd->data.value);
    }
    fs_conf->coll_id = (PVFS_fs_id)cmd->data.value;
    return NULL;
}

static int compare_aliases(void *vkey,
                           void *valias2)
{
    char *hostaliaskey1 = (char *)vkey;
    host_alias_t *alias2 = (host_alias_t *)valias2;
    
    return strcmp(hostaliaskey1, alias2->host_alias);
}

DOTCONF_CB(get_alias)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    if (cmd->arg_count != 1)
    {
        return "Error: Alias must include a single alias\n";
    }

    if (config_s->new_host->host_alias)
    {
        return "Error: Alias already defined for this server\n";
    }

    /* prevent users from adding the same alias twice */
    if(config_s->host_aliases &&
       PINT_llist_search(config_s->host_aliases, 
                         (void *)cmd->data.str,
                         compare_aliases))
    {
        return "Error: alias already defined\n";
    }

    config_s->new_host->host_alias = strdup(cmd->data.str);

    return NULL;
}

DOTCONF_CB(get_sid)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    if (cmd->arg_count != 1)
    {
        return "Error: SID must include a single UUID\n";
    }

    if (config_s->new_host->host_sid_text)
    {
        return "Error: SID already defined for this server\n";
    }

    config_s->new_host->host_sid_text = strdup(cmd->data.str);

    return NULL;
}

DOTCONF_CB(get_address_list)
{
    int i = 0;
    int len = 0;
    char *ptr;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    if (cmd->arg_count < 1)
    {
        return "Error: Address must include at least one address\n";
    }

    if (!config_s->new_host->bmi_address)
    {
        config_s->new_host->bmi_address =
                        (char *)calloc(1, PVFS_MAX_SERVER_ADDR_LIST);
    }
    else
    {
        len = strlen(config_s->new_host->bmi_address);
    }
    ptr = config_s->new_host->bmi_address;

    for (i = 0; i < cmd->arg_count; i++)
    {
        strncat(ptr, cmd->data.list[i], PVFS_MAX_SERVER_ADDR_LIST - len);
        len += strlen(cmd->data.list[i]);
        if (i + 1 < cmd->arg_count)
        {
            strncat(ptr, ",", PVFS_MAX_SERVER_ADDR_LIST - len);
        }
        len++;
    }

    return NULL;
}

DOTCONF_CB(get_type_list)
{
    int i;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    if (cmd->arg_count < 1)
    {
        return "Error: Type must include at least one server type\n";
    }

    /* convert type string into bit field */
    for (i = 0; i < cmd->arg_count; i++)
    {
        config_s->new_host->server_type |=
                        SID_string_to_type(cmd->data.list[i]);
    }

    return NULL;
}

DOTCONF_CB(get_attribute_list)
{
    int i;
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;

    if (cmd->arg_count < 1)
    {
        return "Error: Attribute must include at least one user attribute\n";
    }

    /* convert attribute as needed */
    for (i = 0; i < cmd->arg_count; i++)
    {
        SID_set_attr(cmd->data.list[i], &config_s->new_host->attributes);
    }

    return NULL;
}

/* V3 old version */
#if 0
DOTCONF_CB(get_primesrv)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    struct host_alias_s *prime_alias = NULL;

    if (cmd->arg_count != 1)
    {
        return "Error: PrimeServer statement must include one alias";
    }

    /* look up alias */
    if(config_s->host_aliases &&
       !(prime_alias = (host_alias_t *)PINT_llist_search(
                                                  config_s->host_aliases, 
                                                  (void *)cmd->data.list[0],
                                                  compare_aliases)))
    {
        return "Error: PrimeServer alias not found";
    }
    /* update server type n SIDcache */
    SID_update_type(&prime_alias->host_sid, SID_SERVER_PRIME);

    return NULL;
}
#endif

DOTCONF_CB(get_param)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    distribution_param_config_t *param =
            malloc(sizeof(distribution_param_config_t));

    if (NULL != param)
    {
        memset(param, 0, sizeof(*param));
        param->name = (cmd->data.str ? strdup(cmd->data.str) : NULL);
        PINT_llist_add_to_tail(config_s->default_dist_config.param_list,
                               param);
    }
    return NULL;
}

DOTCONF_CB(get_value)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    distribution_param_config_t *param;
    param = (distribution_param_config_t *)PINT_llist_tail(
            config_s->default_dist_config.param_list);
    if (NULL != param)
    {
        param->value = (PVFS_size)cmd->data.value;
    }
    return NULL;
}

DOTCONF_CB(get_default_num_dfiles)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    struct filesystem_configuration_s *fs_conf = NULL;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);

    fs_conf->default_num_dfiles = (int)cmd->data.value;
    if(fs_conf->default_num_dfiles < 0)
    {
        return("Error DefaultNumDFiles must be non-negative.\n");
    }
    return NULL;
}

DOTCONF_CB(get_default_dfile_replication)
{
    struct server_configuration_s *config_s = 
            (struct server_configuration_s *)cmd->context;
    struct filesystem_configuration_s *fs_conf = NULL;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);

    fs_conf->default_dfile_replication_factor = (int)cmd->data.value;
    if(fs_conf->default_dfile_replication_factor < 0)
    {
        return("Error DefaultDFileReplication must be non-negative.\n");
    }
    return NULL;
}

DOTCONF_CB(get_secret_key)
{
    struct server_configuration_s *config_s =
            (struct server_configuration_s *)cmd->context;
    struct filesystem_configuration_s *fs_conf = NULL;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);

    fs_conf->secret_key = strdup(cmd->data.str);
    return NULL;
}

DOTCONF_CB(get_immediate_completion)
{
    struct server_configuration_s *config_s =
            (struct server_configuration_s *)cmd->context;
    struct filesystem_configuration_s *fs_conf = NULL;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);

    if(!strcmp((char *)cmd->data.str, "yes"))
    {
        fs_conf->immediate_completion = 1;
    }
    else
    {
        fs_conf->immediate_completion = 0;
    }

    return NULL;
}

DOTCONF_CB(get_coalescing_high_watermark)
{
    struct server_configuration_s *config_s =
            (struct server_configuration_s *)cmd->context;
    struct filesystem_configuration_s *fs_conf = NULL;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);

    if(!strcmp((char *)cmd->data.str, "infinity"))
    {
        fs_conf->coalescing_high_watermark = -1;
    }
    else
    {
        sscanf(cmd->data.str, "%d", &fs_conf->coalescing_high_watermark);
    }
    return NULL;
}

DOTCONF_CB(get_coalescing_low_watermark)
{
    struct server_configuration_s *config_s =
            (struct server_configuration_s *)cmd->context;
    struct filesystem_configuration_s *fs_conf = NULL;

    fs_conf = (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);

    fs_conf->coalescing_low_watermark = cmd->data.value;
    return NULL;
}

DOTCONF_CB(get_trove_method)
{
    int * method;
    struct server_configuration_s *config_s =
            (struct server_configuration_s *)cmd->context;

    method = &config_s->trove_method;
    if(config_s->configuration_context == CTX_STORAGEHINTS)
    {
        /* we must be in a storagehints inside a filesystem context */
        struct filesystem_configuration_s *fs_conf =
                (struct filesystem_configuration_s *)
                PINT_llist_head(config_s->file_systems);

        method = &fs_conf->trove_method; 
    }

    if(!strcmp(cmd->data.str, "dbpf"))
    {
        *method = TROVE_METHOD_DBPF;
    }
    else if(!strcmp(cmd->data.str, "alt-aio"))
    {
        *method = TROVE_METHOD_DBPF_ALTAIO;
    }
    else if(!strcmp(cmd->data.str, "null-aio"))
    {
        *method = TROVE_METHOD_DBPF_NULLAIO;
    }
    else if(!strcmp(cmd->data.str, "directio"))
    {
        *method = TROVE_METHOD_DBPF_DIRECTIO;
    }
    else
    {
        return "Error unknown TroveMethod option\n";
    }
    return NULL;
}

DOTCONF_CB(get_small_file_size)
{
    struct server_configuration_s *config_s =
            (struct server_configuration_s *)cmd->context;

    /* we must be in a storagehints inside a filesystem context */
    struct filesystem_configuration_s *fs_conf =
            (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);

    fs_conf->small_file_size = cmd->data.value;
    return NULL;
}

DOTCONF_CB(directio_thread_num)
{
    struct server_configuration_s *config_s =
            (struct server_configuration_s *)cmd->context;

    struct filesystem_configuration_s *fs_conf =
            (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);

    fs_conf->directio_thread_num = cmd->data.value;

    return NULL;
}

DOTCONF_CB(directio_ops_per_queue)
{
    struct server_configuration_s *config_s =
            (struct server_configuration_s *)cmd->context;

    struct filesystem_configuration_s *fs_conf =
            (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);

    fs_conf->directio_ops_per_queue = cmd->data.value;

    return NULL;
}

DOTCONF_CB(directio_timeout)
{
    struct server_configuration_s *config_s =
            (struct server_configuration_s *)cmd->context;

    struct filesystem_configuration_s *fs_conf =
            (struct filesystem_configuration_s *)
            PINT_llist_head(config_s->file_systems);

    fs_conf->directio_timeout = cmd->data.value;

    return NULL;
}

DOTCONF_CB(get_key_store)
{
    struct server_configuration_s *config_s =
            (struct server_configuration_s*)cmd->context;

    if (config_s->configuration_context == CTX_SERVER_OPTIONS &&
            config_s->my_server_options == 0)
    {
        return NULL;
    }
    if (config_s->keystore_path)
    {
        free(config_s->keystore_path);
    }
    config_s->keystore_path =
            (cmd->data.str ? strdup(cmd->data.str) : NULL);
    return NULL;
}

DOTCONF_CB(get_server_key)
{
    struct server_configuration_s *config_s =
            (struct server_configuration_s*)cmd->context;

    if (config_s->configuration_context == CTX_SERVER_OPTIONS &&
            config_s->my_server_options == 0)
    {
        return NULL;
    }
    if (config_s->serverkey_path)
    {
        free(config_s->serverkey_path);
    }    
    config_s->serverkey_path =
            (cmd->data.str ? strdup(cmd->data.str) : NULL);
    return NULL;
}

DOTCONF_CB(get_credential_timeout)
{
    struct server_configuration_s *config_s = 
                    (struct server_configuration_s *)cmd->context;    

    if (cmd->data.value >= PVFS2_SECURITY_TIMEOUT_MIN &&
        cmd->data.value <= PVFS2_SECURITY_TIMEOUT_MAX)
    {
        config_s->credential_timeout = (int) cmd->data.value;
    }
    else
    {
        gossip_err("Warning: CredentialTimeoutSecs value invalid (%ld) - "
                   "using default (%d)\n", cmd->data.value, 
                   config_s->credential_timeout);
    }
    
    return NULL;
}

DOTCONF_CB(get_capability_timeout)
{
    struct server_configuration_s *config_s =
                 (struct server_configuration_s *)cmd->context;

    if (cmd->data.value >= PVFS2_SECURITY_TIMEOUT_MIN &&
        cmd->data.value <= PVFS2_SECURITY_TIMEOUT_MAX)
    {
        config_s->capability_timeout = (int) cmd->data.value;
    }
    else
    {
        gossip_err("Warning: CapabilityTimeoutSecs value invalid (%ld) - "
                   "using default (%d)\n", cmd->data.value,
                   config_s->capability_timeout);
    }

    return NULL;
}

DOTCONF_CB(get_credcache_timeout)
{
    struct server_configuration_s *config_s =
        (struct server_configuration_s *)cmd->context;

    if (cmd->data.value >= PVFS2_SECURITY_TIMEOUT_MIN &&
        cmd->data.value <= PVFS2_SECURITY_TIMEOUT_MAX)
    {
        config_s->credcache_timeout = (int) cmd->data.value;
    }
    else
    {
        gossip_err("Warning: CredentialCacheTimeoutSecs value invalid (%ld) - "
                   "using default (%d)\n", cmd->data.value,
                   config_s->credcache_timeout);
    }

    return NULL;
}

DOTCONF_CB(get_capcache_timeout)
{
    struct server_configuration_s *config_s =
        (struct server_configuration_s *)cmd->context;

    if (cmd->data.value >= PVFS2_SECURITY_TIMEOUT_MIN &&
        cmd->data.value <= PVFS2_SECURITY_TIMEOUT_MAX)
    {
        config_s->capcache_timeout = (int) cmd->data.value;
    }
    else
    {
        gossip_err("Warning: CapabilityCacheTimeoutSecs value invalid (%ld) - "
                   "using default (%d)\n", cmd->data.value,
                   config_s->capcache_timeout);
    }

    return NULL;
}

DOTCONF_CB(get_certcache_timeout)
{
    struct server_configuration_s *config_s =
        (struct server_configuration_s *)cmd->context;

    if (cmd->data.value >= PVFS2_SECURITY_TIMEOUT_MIN &&
        cmd->data.value <= PVFS2_SECURITY_TIMEOUT_MAX)
    {
        config_s->certcache_timeout = (int) cmd->data.value;
    }
    else
    {
        gossip_err("Warning: CertificateCacheTimeoutSecs value invalid (%ld) - "
                   "using default (%d)\n", cmd->data.value,
                   config_s->certcache_timeout);
    }

    return NULL;
}


DOTCONF_CB(get_ca_file)
{ 
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    if (config_s->configuration_context == CTX_SERVER_OPTIONS &&
            config_s->my_server_options == 0)
    {
        return NULL;
    }
    if (config_s->ca_file)
    {
        free(config_s->ca_file);
    }
    config_s->ca_file = 
        (cmd->data.str ? strdup(cmd->data.str) : NULL);
    
    return NULL;
}

DOTCONF_CB(get_user_cert_dn)
{ 
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    if (config_s->configuration_context == CTX_SERVER_OPTIONS &&
            config_s->my_server_options == 0)
    {
        return NULL;
    }
    if (config_s->user_cert_dn)
    {
        free(config_s->user_cert_dn);
    }
    config_s->user_cert_dn = 
        (cmd->data.str ? strdup(cmd->data.str) : NULL);
    
    return NULL;
}

DOTCONF_CB(get_user_cert_exp)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    if (config_s->configuration_context == CTX_SERVER_OPTIONS &&
            config_s->my_server_options == 0)
    {
        return NULL;
    }

    config_s->user_cert_exp = cmd->data.value;

    return NULL;
}

DOTCONF_CB(enter_ldap_context)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    config_s->configuration_context = CTX_LDAP;

    return PINT_dotconf_set_defaults(
        cmd->configfile,
        CTX_LDAP);
}

DOTCONF_CB(exit_ldap_context)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    config_s->configuration_context = CTX_SECURITY;

    return NULL;
}

DOTCONF_CB(get_ldap_hosts)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    if (config_s->ldap_hosts)
    {
        free(config_s->ldap_hosts);
    }
    config_s->ldap_hosts = 
        (cmd->data.str ? strdup(cmd->data.str) : NULL);

    return NULL;
}

DOTCONF_CB(get_ldap_bind_dn)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    if (config_s->ldap_bind_dn)
    {
        free(config_s->ldap_bind_dn);
    }
    config_s->ldap_bind_dn = 
        (cmd->data.str ? strdup(cmd->data.str) : NULL);

    return NULL;
}

DOTCONF_CB(get_ldap_bind_password)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    if (config_s->ldap_bind_password)
    {
        free(config_s->ldap_bind_password);
    }
    config_s->ldap_bind_password = 
        (cmd->data.str ? strdup(cmd->data.str) : NULL);

    return NULL;
}

DOTCONF_CB(get_ldap_search_mode)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    if (!cmd->data.str)
    {
        config_s->ldap_search_mode = 0;
        return NULL;
    }

    if (strcasecmp(cmd->data.str, "DN") == 0)
    {
        config_s->ldap_search_mode = 1;
    }
    else if (strcasecmp(cmd->data.str, "CN") == 0)
    {
        config_s->ldap_search_mode = 0;
    }
    else
    {
        return "SearchMode must be 'CN' or 'DN'";
    }

    return NULL;
}

DOTCONF_CB(get_ldap_search_root)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    if (config_s->ldap_search_root)
    {
        free(config_s->ldap_search_root);
    }
    config_s->ldap_search_root = 
        (cmd->data.str ? strdup(cmd->data.str) : NULL);

    return NULL;
}

DOTCONF_CB(get_ldap_search_class)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    if (config_s->ldap_search_class)
    {
        free(config_s->ldap_search_class);
    }
    config_s->ldap_search_class = 
        (cmd->data.str ? strdup(cmd->data.str) : NULL);

    return NULL;
}

DOTCONF_CB(get_ldap_search_attr)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    if (config_s->ldap_search_attr)
    {
        free(config_s->ldap_search_attr);
    }
    config_s->ldap_search_attr = 
        (cmd->data.str ? strdup(cmd->data.str) : NULL);

    return NULL;
}

DOTCONF_CB(get_ldap_search_scope)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    if (!cmd->data.str) 
    {
        config_s->ldap_search_scope = 2; /* subtree */
        return NULL;
    }

    if (strncasecmp(cmd->data.str, "one", 3) == 0)
    {
        config_s->ldap_search_scope = 1;
    }
    else if (strncasecmp(cmd->data.str, "sub", 3) == 0)
    {
        config_s->ldap_search_scope = 2;
    }
    else
    {
        return "SearchScope must be 'onelevel' or 'subtree'";
    }

    return NULL;
}

DOTCONF_CB(get_ldap_uid_attr)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    if (config_s->ldap_uid_attr)
    {
        free(config_s->ldap_uid_attr);
    }
    config_s->ldap_uid_attr = 
        (cmd->data.str ? strdup(cmd->data.str) : NULL);


    return NULL;
}

DOTCONF_CB(get_ldap_gid_attr)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    if (config_s->ldap_gid_attr)
    {
        free(config_s->ldap_gid_attr);
    }
    config_s->ldap_gid_attr = 
        (cmd->data.str ? strdup(cmd->data.str) : NULL);

    return NULL;
}

DOTCONF_CB(get_ldap_search_timeout)
{
    struct server_configuration_s *config_s = 
        (struct server_configuration_s *)cmd->context;

    config_s->ldap_search_timeout = cmd->data.value;

    return NULL;
}

DOTCONF_CB(tree_width)
{
    struct server_configuration_s *config_s =
        (struct server_configuration_s *)cmd->context;

    config_s->tree_width = cmd->data.value;

    return NULL;
}

DOTCONF_CB(tree_threshold)
{
    struct server_configuration_s *config_s =
        (struct server_configuration_s *)cmd->context;

    config_s->tree_threshold = cmd->data.value;

    return NULL;
}

DOTCONF_CB(distr_dir_servers_initial)
{
    struct server_configuration_s *config_s =
        (struct server_configuration_s *)cmd->context;

    config_s->distr_dir_servers_initial = cmd->data.value;

    return NULL;
}

DOTCONF_CB(distr_dir_servers_max)
{
    struct server_configuration_s *config_s =
            (struct server_configuration_s *)cmd->context;

    config_s->distr_dir_servers_max = cmd->data.value;

    return NULL;
}

DOTCONF_CB(distr_dir_split_size)
{
    struct server_configuration_s *config_s =
            (struct server_configuration_s *)cmd->context;

    config_s->distr_dir_split_size = cmd->data.value;

    return NULL;
}


/*
 * Function: PINT_config_release
 *
 * Params:   struct server_configuration_s*
 *
 * Returns:  void
 *
 * Synopsis: De-allocates memory consumed internally
 *           by the specified server_configuration_s
 *   
 */
void PINT_config_release(struct server_configuration_s *config_s)
{
    if (config_s)
    {
        if (config_s->host_id)
        {
            free(config_s->host_id);
            config_s->host_id = NULL;
        }

        if (config_s->data_path)
        {
            free(config_s->data_path);
            config_s->data_path = NULL;
        }

        if (config_s->meta_path)
        {
            free(config_s->meta_path);
            config_s->meta_path = NULL;
        }

        if (config_s->fs_config_filename)
        {
            free(config_s->fs_config_filename);
            config_s->fs_config_filename = NULL;
        }

        if (config_s->fs_config_buf)
        {
            free(config_s->fs_config_buf);
            config_s->fs_config_buf = NULL;
        }

        if (config_s->logfile)
        {
            free(config_s->logfile);
            config_s->logfile = NULL;
        }

        if (config_s->logtype)
        {
            free(config_s->logtype);
            config_s->logtype = NULL;
        }

        if (config_s->event_logging)
        {
            free(config_s->event_logging);
            config_s->event_logging = NULL;
        }

        if (config_s->bmi_modules)
        {
            free(config_s->bmi_modules);
            config_s->bmi_modules = NULL;
        }

        if (config_s->flow_modules)
        {
            free(config_s->flow_modules);
            config_s->flow_modules = NULL;
        }
#ifdef USE_TRUSTED
        if (config_s->allowed_networks)
        {
            int i;
            for (i = 0; i < config_s->allowed_networks_count; i++)
            {
                free(config_s->allowed_networks[i]);
                config_s->allowed_networks[i] = NULL;
            }
            free(config_s->allowed_networks);
            config_s->allowed_networks = NULL;
        }
        if (config_s->allowed_masks)
        {
            free(config_s->allowed_masks);
            config_s->allowed_masks = NULL;
        }
        if (config_s->security && config_s->security_dtor)
        {
            config_s->security_dtor(config_s->security);
            config_s->security = NULL;
        }
#endif
        /* free all filesystem objects */
        if (config_s->file_systems)
        {
            PINT_llist_free(config_s->file_systems, free_filesystem);
            config_s->file_systems = NULL;
        }

        /* free all host alias objects */
        if (config_s->host_aliases)
        {
            PINT_llist_free(config_s->host_aliases, free_host_alias);
            config_s->host_aliases = NULL;
        }

        if (config_s->db_cache_type)
        {
            free(config_s->db_cache_type);
            config_s->db_cache_type = NULL;
        }

        if (config_s->server_alias)
        {
            free(config_s->server_alias);
            config_s->server_alias = NULL;
        }

        free(config_s->keystore_path);
        config_s->keystore_path = NULL;
        free(config_s->serverkey_path);
        config_s->serverkey_path = NULL;
    }
}

/* This function checks to see that the minimum fields for a valid host
 * alias are present before we add a host into the list of aliases.
 */
static int is_valid_host(host_alias_t *host)
{
    if (!host->host_alias || !host->host_sid_text || !host->bmi_address ||
        !host->server_type)
    {
        return 0; /* false, it is not valid */
    }
    return 1; /* true, it is valid */
}

/* This function checks to see of the alias pointed to by str is already
 * present on the list of aliases pointed to by host_aliases.
 */
static int is_valid_alias(PINT_llist * host_aliases, char *str)
{
    int ret = 0;
    PINT_llist *cur = NULL;
    struct host_alias_s *cur_alias;

    if (str)
    {
        cur = host_aliases;
        while(cur)
        {
            cur_alias = PINT_llist_head(cur);
            if (!cur_alias)
            {
                break;
            }
            assert(cur_alias->host_alias);
            assert(cur_alias->bmi_address);

            if (strcmp(str, cur_alias->host_alias) == 0)
            {
                ret = 1; /* true, it is valid (found) */
                break;
            }
            cur = PINT_llist_next(cur);
        }
    }
    return ret; /* false, it is not valid (not found) */
}

static int is_populated_filesystem_configuration(
        struct filesystem_configuration_s *fs)
{
    return ((fs && fs->coll_id && fs->file_system_name &&
             PVFS_OID_NE(&fs->root_handle, &PVFS_HANDLE_NULL)) ? 1 : 0);
}

static int is_valid_filesystem_configuration(
        struct server_configuration_s *config,
        struct filesystem_configuration_s *fs)
{
    int ret = 1;
    if (ret == 0)
    {
        gossip_err("RootHandle (%s) is NOT within the meta handle "
                   "ranges specified for this filesystem (%s).\n",
                   PVFS_OID_str(&fs->root_handle),fs->file_system_name);
    }
    return ret;
}

static void free_host_alias(void *ptr)
{
    struct host_alias_s *alias = (struct host_alias_s *)ptr;
    if (alias)
    {
        free(alias->host_alias);
        free(alias->host_sid_text);
        free(alias->bmi_address);
        free(alias->attributes);
        free(alias);
    }
}

static void free_filesystem(void *ptr)
{
    struct filesystem_configuration_s *fs =
            (struct filesystem_configuration_s *)ptr;

    if (fs)
    {
        free(fs->file_system_name);
        fs->file_system_name = NULL;

        if(fs->root_sid_array)
        {
            free(fs->root_sid_array);
            fs->root_sid_array = NULL;
        }

        /* Items on the root_servers list are also on the alias list of
         * the server config so don't actually remove them, only the links
         */
        PINT_llist_free(fs->root_servers, NULL);

        /* if the optional hints are used, free them */
        if (fs->attr_cache_keywords)
        {
            free(fs->attr_cache_keywords);
            fs->attr_cache_keywords = NULL;
        }
        if(fs->secret_key)
        {
            free(fs->secret_key);
        }
        /* free all ro_hosts specifications */
        if (fs->ro_hosts)
        {
            free_list_of_strings(fs->ro_count, &fs->ro_hosts);
            fs->ro_count = 0;
        }
        if (fs->ro_netmasks)
        {
            free(fs->ro_netmasks);
            fs->ro_netmasks = NULL;
        }
        /* free all root_squash_exception_hosts specifications */
        if (fs->root_squash_exceptions_hosts)
        {
            free_list_of_strings(fs->root_squash_exceptions_count,
                                 &fs->root_squash_exceptions_hosts);
            fs->root_squash_exceptions_count = 0;
        }
        if (fs->root_squash_exceptions_netmasks)
        {
            free(fs->root_squash_exceptions_netmasks);
            fs->root_squash_exceptions_netmasks = NULL;
        }
        /* free all root_squash_hosts specifications */
        if (fs->root_squash_hosts)
        {
            free_list_of_strings(fs->root_squash_count, &fs->root_squash_hosts);
            fs->root_squash_count = 0;
        }
        if (fs->root_squash_netmasks)
        {
            free(fs->root_squash_netmasks);
            fs->root_squash_netmasks = NULL;
        }
        /* free all all_squash_hosts specifications */
        if (fs->all_squash_hosts)
        {
            free_list_of_strings(fs->all_squash_count, &fs->all_squash_hosts);
            fs->all_squash_count = 0;
        }
        if (fs->all_squash_netmasks)
        {
            free(fs->all_squash_netmasks);
            fs->all_squash_netmasks = NULL;
        }
        free(fs);
        fs = NULL;
    }
}

/** copy_filesystem - an exercise in bad code!
 *
 * I HATE this code - poorly documented and called from an obscure
 * place in the confguration processing, it has the effect of making
 * important information in the configuration disappear for no
 * particular reason.  IF there is a GOOD reason to copy rather than
 * edit a linked list (which is, afterall why we have linked lists, so
 * we can edit them) then this good reason must be well documented!
 * And for the most part, the scalr data can be copied with memcpy!
 * FOr now this code still exists but should be removed as soon as
 * feasible!!!    WBL V3
 */
static void copy_filesystem(struct filesystem_configuration_s *dest_fs,
                            struct filesystem_configuration_s *src_fs)
{
    PINT_llist *cur = NULL;
    struct host_alias_s *cur_root_server = NULL;
    struct host_alias_s *new_root_server = NULL;

    if (dest_fs && src_fs)
    {
        dest_fs->file_system_name = strdup(src_fs->file_system_name);
        assert(dest_fs->file_system_name);

        dest_fs->coll_id = src_fs->coll_id;
        dest_fs->root_handle = src_fs->root_handle;
        dest_fs->root_dirdata_handle = src_fs->root_dirdata_handle;
        dest_fs->root_sid_count = src_fs->root_sid_count;
        if (src_fs->root_sid_array)
        {
            dest_fs->root_sid_array = (PVFS_SID *)malloc(
                                   dest_fs->root_sid_count * sizeof(PVFS_SID));
            if (!dest_fs->root_sid_array)
            {
                gossip_err("Failed to allocate memory for root_sid_array\n");
                return;
            }
            memcpy(dest_fs->root_sid_array,
                   src_fs->root_sid_array,
                   dest_fs->root_sid_count * sizeof(PVFS_SID));
        }
        else
        {
            /* this should never happen, but until the list of root
             * servers is converted into SIDS it is possible this is the
             * state of the source.
             */
            dest_fs->root_sid_array = NULL;
        }

        dest_fs->default_num_dfiles = src_fs->default_num_dfiles;
        dest_fs->metadata_replication_factor = 
                                   src_fs->metadata_replication_factor;
        dest_fs->flowproto = src_fs->flowproto;
        dest_fs->encoding = src_fs->encoding;

        dest_fs->root_servers = PINT_llist_new();

        assert(dest_fs->root_servers);

        if(src_fs->secret_key)
        {
            dest_fs->secret_key = strdup(src_fs->secret_key);
        }

        /* copy all root servers */
        cur = src_fs->root_servers;
        while(cur)
        {
            cur_root_server = PINT_llist_head(cur);
            if (!cur_root_server)
            {
                /* end of list */
                break;
            }
            new_root_server = (struct host_alias_s *)malloc(
                                            sizeof(struct host_alias_s));
            if (!new_root_server)
            {
                gossip_err("failed to alloacte memory for a root server\n");
                return;
            }

            new_root_server->host_sid_text =
                            strdup(cur_root_server->host_sid_text);

            PINT_llist_add_to_tail(dest_fs->root_servers, new_root_server);
            cur = PINT_llist_next(cur);
        }

/* V3 */
#if 0
        dest_fs->handle_recycle_timeout_sec =
                src_fs->handle_recycle_timeout_sec;
#endif

        /* if the optional hints are used, copy them too */
        if (src_fs->attr_cache_keywords)
        {
            dest_fs->attr_cache_keywords = strdup(src_fs->attr_cache_keywords);
            assert(dest_fs->attr_cache_keywords);
        }

        dest_fs->attr_cache_size = src_fs->attr_cache_size;
        dest_fs->attr_cache_max_num_elems = src_fs->attr_cache_max_num_elems;

        dest_fs->trove_sync_meta = src_fs->trove_sync_meta;
        dest_fs->trove_sync_data = src_fs->trove_sync_data;
 
        /* copy all relevant export options */
        dest_fs->exp_flags    = src_fs->exp_flags;
        dest_fs->ro_count     = src_fs->ro_count;
        dest_fs->root_squash_count = src_fs->root_squash_count;
        dest_fs->all_squash_count = src_fs->all_squash_count;
        if (src_fs->ro_count > 0 && src_fs->ro_hosts)
        {
            int i;
            dest_fs->ro_hosts = (char **) calloc(src_fs->ro_count, sizeof(char *));
            assert(dest_fs->ro_hosts); /* TODO: replace with error handling */
            for (i = 0; i < src_fs->ro_count; i++)
            {
                dest_fs->ro_hosts[i] = strdup(src_fs->ro_hosts[i]);
                assert(dest_fs->ro_hosts[i]);
            }
        }
        if (src_fs->ro_count > 0 && src_fs->ro_netmasks)
        {
            dest_fs->ro_netmasks = (int *) calloc(src_fs->ro_count, sizeof(int));
            assert(dest_fs->ro_netmasks); /* TODO; replace with error handling */
            memcpy(dest_fs->ro_netmasks,
                   src_fs->ro_netmasks,
                   src_fs->ro_count * sizeof(int));
        }
        if (src_fs->root_squash_count > 0 && src_fs->root_squash_hosts)
        {
            int i;
            dest_fs->root_squash_hosts =
                    (char **) calloc(src_fs->root_squash_count, sizeof(char *));
            assert(dest_fs->root_squash_hosts); /* TODO; replace with error handling */
            for (i = 0; i < src_fs->root_squash_count; i++)
            {
                dest_fs->root_squash_hosts[i] =
                        strdup(src_fs->root_squash_hosts[i]);
                assert(dest_fs->root_squash_hosts[i]); /* TODO; replace with error handling */
            }
        }
        if (src_fs->root_squash_count > 0 && src_fs->root_squash_netmasks)
        {
            dest_fs->root_squash_netmasks =
                    (int *) calloc(src_fs->root_squash_count, sizeof(int));
            assert(dest_fs->root_squash_netmasks); /* TODO; replace with error handling */
            memcpy(dest_fs->root_squash_netmasks,
                   src_fs->root_squash_netmasks,
                   src_fs->root_squash_count * sizeof(int));
        }
        if (src_fs->all_squash_count > 0 && src_fs->all_squash_hosts)
        {
            int i;
            dest_fs->all_squash_hosts =
                    (char **) calloc(src_fs->all_squash_count, sizeof(char *));
            assert(dest_fs->all_squash_hosts); /* TODO; replace with error handling */
            for (i = 0; i < src_fs->all_squash_count; i++)
            {
                dest_fs->all_squash_hosts[i] =
                        strdup(src_fs->all_squash_hosts[i]);
                assert(dest_fs->all_squash_hosts[i]); /* TODO; replace with error handling */
            }
        }
        if (src_fs->all_squash_count > 0 && src_fs->all_squash_netmasks)
        {
            dest_fs->all_squash_netmasks =
                    (int *) calloc(src_fs->all_squash_count, sizeof(int));
            assert(dest_fs->all_squash_netmasks); /* TODO; replace with error handling */
            memcpy(dest_fs->all_squash_netmasks,
                   src_fs->all_squash_netmasks,
                          src_fs->all_squash_count * sizeof(int));
        }
        dest_fs->exp_anon_uid = src_fs->exp_anon_uid;
        dest_fs->exp_anon_gid = src_fs->exp_anon_gid;

        dest_fs->fp_buffer_size = src_fs->fp_buffer_size;
        dest_fs->fp_buffers_per_flow = src_fs->fp_buffers_per_flow;
    }
}

/* V3 currently obsolete - seems like a useful func not sure why its
 * static
 */
#if 0
static host_alias_t *find_host_alias_ptr_by_alias(
        struct server_configuration_s *config_s,
        char *alias,
        int *index)
{
    PINT_llist *cur = NULL;
    struct host_alias_s *ret = NULL;
    struct host_alias_s *cur_alias = NULL;
    int ind = 0;

    if (config_s && alias)
    {
        cur = config_s->host_aliases;
        while(cur)
        {
            ind++;
            cur_alias = PINT_llist_head(cur);
            if (!cur_alias)
            {
                break;
            }
            assert(cur_alias->host_alias);
            assert(cur_alias->bmi_address);

            if (strcmp(cur_alias->host_alias,alias) == 0)
            {
                ret = cur_alias;
                break;
            }
            cur = PINT_llist_next(cur);
        }
    }
    if(index) *index = ind - 1;
    return ret;
}
#endif

#ifdef USE_TRUSTED
/*
 * Function: PINT_config_get_allowed_ports
 *
 * Params:   struct server_configuration_s          *server_config
 *           int           *enabled
 *           unsigned long *ports (OUT)
 *
 * Returns:  by filling up *ports from the allowed_ports array
 *           0 on success and -1 on failure
 *
 * Synopsis: Retrieve the list of allowed ports (i.e. range)
 */
int PINT_config_get_allowed_ports(struct server_configuration_s *config_s,
                                  int *enabled,
                                  unsigned long *allowed_ports)
{
    int ret = -1;

    if (config_s)
    {
        *enabled = config_s->ports_enabled;
        if (*enabled == 1)
        {
            allowed_ports[0] = config_s->allowed_ports[0];
            allowed_ports[1] = config_s->allowed_ports[1];
        }
        ret = 0;
    }
    return ret;
}

/*
 * Function: PINT_config_get_allowed_networks
 *
 * Params:   struct server_configuration_s *server_config
 *           int  *enabled
 *           int  *allowed_network_count (OUT)
 *           char **allowed_networks (OUT)
 *           int *allowed_netmasks (OUT)
 *
 * Returns:  Fills up *allowed_network_count, *allowed_networks and
 *           *allowed_netmasks
 *           and returns 0 on success and -1 on failure
 *
 * Synopsis: Retrieve the list of allowed network addresses and netmasks
 */

int PINT_config_get_allowed_networks(struct server_configuration_s *config_s,
                                     int  *enabled,
                                     int  *allowed_networks_count,
                                     char ***allowed_networks,
                                     int  **allowed_masks)
{
    int ret = -1;

    if (config_s)
    {
        *enabled = config_s->network_enabled;
        if (*enabled == 1)
        {
            *allowed_networks_count = config_s->allowed_networks_count;
            *allowed_networks = config_s->allowed_networks;
            *allowed_masks    = config_s->allowed_masks;
        }
        ret = 0;
    }
    return ret;
}

#endif

/*
 * Function: PINT_config_get_host_addr_ptr
 *
 * Params:   struct server_configuration_s*,
 *           char *alias
 *
 * Returns:  char * (bmi_address) on success; NULL on failure
 *
 * Synopsis: retrieve the bmi_address matching the specified alias
 *   
 */
char *PINT_config_get_host_addr_ptr(struct server_configuration_s *config_s,
                                    char *alias)
{
    char *ret = (char *)0;
    PINT_llist *cur = NULL;
    struct host_alias_s *cur_alias = NULL;

    if (config_s && alias)
    {
        cur = config_s->host_aliases;
        while(cur)
        {
            cur_alias = PINT_llist_head(cur);
            if (!cur_alias)
            {
                break;
            }
            assert(cur_alias->host_alias);
            assert(cur_alias->bmi_address);

            if (strcmp(cur_alias->host_alias, alias) == 0)
            {
                ret = cur_alias->bmi_address;
                break;
            }
            cur = PINT_llist_next(cur);
        }
    }
    return ret;
}

/*
 * Function: PINT_config_get_host_alias_ptr
 *
 * Params:   struct server_configuration_s*,
 *           char *bmi_address
 *
 * Returns:  char * (alias) on success; NULL on failure
 *
 * Synopsis: retrieve the alias matching the specified bmi_url
 *
 * This is a linear search, should probably be a hash table
 *   
 */
char *PINT_config_get_host_alias_ptr(struct server_configuration_s *config_s,
                                     char *bmi_url)
{
    char *ret = (char *)0;
    PINT_llist *cur = NULL;
    struct host_alias_s *cur_alias = NULL;

    if (config_s && bmi_url)
    {
        cur = config_s->host_aliases;
        while(cur)
        {
            cur_alias = PINT_llist_head(cur);
            if (!cur_alias)
            {
                break;
            }
            assert(cur_alias->host_alias);
            assert(cur_alias->bmi_address);

            if (strcmp(cur_alias->bmi_address, bmi_url) == 0)
            {
                ret = cur_alias->host_alias;
                break;
            }
            cur = PINT_llist_next(cur);
        }
    }
    return ret;
}

/*
 * verify that the config file exists.  if so, cache it in RAM so
 * that getconfig will not have to re-read the file contents each time.
 * returns 0 on success; 1 on failure.
 *
 *even if this call fails half way into it, a PINT_config_release
 *call should properly de-alloc all consumed memory.
 */
static int cache_config_files(struct server_configuration_s *config_s,
                              char *global_config_filename)
{
    int fd = 0, nread = 0, file_found = 0;
    struct stat statbuf;
#ifdef WIN32
    char working_dir[MAX_PATH+1];
#else
    char *working_dir = NULL;
#endif
    char *my_global_fn = NULL;
    char buf[512] = {0};

    assert(config_s);

#ifdef WIN32
    GetCurrentDirectory(MAX_PATH+1, working_dir);
#else
    working_dir = getenv("PWD");
#endif

    /* pick some filenames if not provided */
    my_global_fn = ((global_config_filename != NULL) ?
                    global_config_filename : "fs.conf");

    while(!file_found)
    {
        static int one_shot = 0;
        memset(&statbuf, 0, sizeof(struct stat));
        if ((file_found = !stat(my_global_fn, &statbuf)) == 1)
        {
            if (statbuf.st_size == 0)
            {
                gossip_err("Invalid config file %s.  This "
                           "file is 0 bytes in length!\n", my_global_fn);
                goto error_exit;
            }
            config_s->fs_config_filename = strdup(my_global_fn);
            config_s->fs_config_buflen = statbuf.st_size + 1;
        }
        else if (errno == ENOENT)
        {
	    gossip_err("Failed to find global config file %s.  This "
                       "file does not exist!\n", my_global_fn);
            goto error_exit;
        }
        else /* I don't think this code ever runs - WBL */
        {
            if (one_shot)
            {
                goto error_exit;
            }
            one_shot++;
            assert(working_dir);
#ifdef WIN32
            _snprintf(buf, 512, "%s\\%s",working_dir, my_global_fn);
#else
            snprintf(buf, 512, "%s/%s",working_dir, my_global_fn);
#endif
            my_global_fn = buf;
        }
    }

    if (!config_s->fs_config_filename || (config_s->fs_config_buflen == 0))
    {
        gossip_err("Failed to stat fs config file.  Please make sure that ");
        gossip_err("the file %s\nexists, is not a zero file size, and has\n",
                   config_s->fs_config_filename);
        gossip_err("permissions suitable for opening and reading it.\n");
        goto error_exit;
    }

    if ((fd = open(my_global_fn, O_RDONLY)) == -1)
    {
        gossip_err("Failed to open fs config file %s.\n",
                   my_global_fn);
        goto error_exit;
    }

    config_s->fs_config_buf = (char *) malloc(config_s->fs_config_buflen);
    if (!config_s->fs_config_buf)
    {
        gossip_err("Failed to allocate %d bytes for caching the fs "
                   "config file\n", (int) config_s->fs_config_buflen);
        goto close_fd_fail;
    }

    memset(config_s->fs_config_buf, 0, config_s->fs_config_buflen);
    nread = read(fd,
                 config_s->fs_config_buf,
                 (config_s->fs_config_buflen - 1));
    if (nread != (config_s->fs_config_buflen - 1))
    {
        gossip_err("Failed to read fs config file %s "
                   "(nread is %d | config_buflen is %d)\n",
                   my_global_fn, nread, (int)(config_s->fs_config_buflen - 1));
        goto close_fd_fail;
    }
    close(fd);

    return 0;

close_fd_fail:
    close(fd);

error_exit:
    return 1;
}

/*
 * returns 1 if the specified configuration object is valid
 * (i.e. contains values that make sense); 0 otherwise
 */
int PINT_config_is_valid_configuration(struct server_configuration_s *config_s)
{
    int ret = 0, fs_count = 0;
    PINT_llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;
    
    if (config_s && config_s->bmi_modules &&
        config_s->event_logging && config_s->logfile)
    {
        cur = config_s->file_systems;
        while(cur)
        {
            cur_fs = PINT_llist_head(cur);
            if (!cur_fs)
            {
                break;
            }

            ret += is_valid_filesystem_configuration(config_s,cur_fs);
            fs_count++;

            cur = PINT_llist_next(cur);
        }
        ret = ((ret == fs_count) ? 1 : 0);
    }
    return ret;
}


/*
 * returns 1 if the specified coll_id is valid based on
 * the specified server_configuration struct; 0 otherwise
 */
int PINT_config_is_valid_collection_id(struct server_configuration_s *config_s,
                                       PVFS_fs_id fs_id)
{
    int ret = 0;
    PINT_llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;

    if (config_s)
    {
        cur = config_s->file_systems;
        while(cur)
        {
            cur_fs = PINT_llist_head(cur);
            if (!cur_fs)
            {
                break;
            }
            if (cur_fs->coll_id == fs_id)
            {
                ret = 1;
                break;
            }
            cur = PINT_llist_next(cur);
        }
    }
    return ret;
}

/*
 * returns pointer to fs config if the config object has information on 
 * the specified filesystem; NULL otherwise
 */
struct filesystem_configuration_s* PINT_config_find_fs_name(
                struct server_configuration_s *config_s,
                char *fs_name)
{
    PINT_llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;

    if (config_s && fs_name)
    {
        cur = config_s->file_systems;
        while(cur)
        {
            cur_fs = PINT_llist_head(cur);
            if (!cur_fs)
            {
                break;
            }
            assert(cur_fs->file_system_name);
            if (strcmp(cur_fs->file_system_name,fs_name) == 0)
            {
                return(cur_fs);
            }
            cur = PINT_llist_next(cur);
        }
    }
    return(NULL);
}

/* PINT_config_find_fs()
 *
 * searches the given server configuration information to find a file
 * system configuration that matches the fs_id
 *
 * returns pointer to file system config struct on success, NULL on failure
 */
struct filesystem_configuration_s* PINT_config_find_fs_id(
                struct server_configuration_s* config_s,
                PVFS_fs_id fs_id)
{
    PINT_llist *cur = NULL;
    struct filesystem_configuration_s *cur_fs = NULL;

    if (config_s)
    {
        cur = config_s->file_systems;
        while(cur)
        {
            cur_fs = PINT_llist_head(cur);
            if (!cur_fs)
            {
                break;
            }
            if (cur_fs->coll_id == fs_id)
            {
                return(cur_fs);
            }
            cur = PINT_llist_next(cur);
        }
    }
    return(NULL);
}

PVFS_fs_id PINT_config_get_fs_id_by_fs_name(
                struct server_configuration_s *config_s,
                char *fs_name)
{
    PVFS_fs_id fs_id = 0;
    struct filesystem_configuration_s *fs =
                    PINT_config_find_fs_name(config_s, fs_name);
    if (fs)
    {
        fs_id = fs->coll_id;
    }
    return fs_id;
}

/* PINT_config_get_filesystems()
 *
 * returns a PINT_llist of all filesystems registered in the
 * specified configuration object
 *
 * returns pointer to a list of file system config structs on success,
 * NULL on failure
 */
PINT_llist *PINT_config_get_filesystems(struct server_configuration_s *config_s)
{
    return (config_s ? config_s->file_systems : NULL);
}

/*
 * given a configuration object, weed out all information about other
 * filesystems if the fs_id does not match that of the specifed fs_id
 */
int PINT_config_trim_filesystems_except(struct server_configuration_s *config_s,
                                        PVFS_fs_id fs_id)
{
    int ret = -PVFS_EINVAL;
    PINT_llist *cur = NULL, *new_fs_list = NULL;
    struct filesystem_configuration_s *cur_fs = NULL, *new_fs = NULL;

    if (config_s)
    {
        new_fs_list = PINT_llist_new();
        if (!new_fs_list)
        {
            return -PVFS_ENOMEM;
        }

        cur = config_s->file_systems;
        while(cur)
        {
            cur_fs = PINT_llist_head(cur);
            if (!cur_fs)
            {
                break;
            }

            if (cur_fs->coll_id == fs_id)
            {
                new_fs = (struct filesystem_configuration_s *)malloc(
                        sizeof(struct filesystem_configuration_s));
                assert(new_fs);

                memset(new_fs,
                       0,
                       sizeof(struct filesystem_configuration_s));

                copy_filesystem(new_fs, cur_fs);
                PINT_llist_add_to_head(new_fs_list, (void *)new_fs);
                break;
            }
            cur = PINT_llist_next(cur);
        }

        PINT_llist_free(config_s->file_systems,free_filesystem);
        config_s->file_systems = new_fs_list;

        if (PINT_llist_count(config_s->file_systems) == 1)
        {
            ret = 0;
        }
    }
    return ret;
}

int PINT_config_get_fs_key(struct server_configuration_s *config,
                           PVFS_fs_id fs_id,
                           char ** key,
                           int * length)
{
#ifndef HAVE_OPENSSL
    *key = NULL;
    *length = 0;
    return -PVFS_ENOSYS;
#else
    int len, b64len;
    char *b64buf;
    struct filesystem_configuration_s *fs_conf = NULL;
    BIO *b64 = NULL;
    BIO *mem = NULL;
    BIO *bio = NULL;

    if (config)
    {
        fs_conf = PINT_config_find_fs_id(config, fs_id);
    }
    
    if(!fs_conf)
    {
        gossip_err("Could not locate fs_conf for fs_id %d\n", fs_id);
        return -PVFS_EINVAL;
    }
    /* This is actually ok since an FS may not have secret key */
    if (!fs_conf->secret_key)
    {
        *length = 0;
        *key = NULL;
        return 0;
    }
    
    b64len = strlen(fs_conf->secret_key);
    b64buf = malloc(b64len+1);
    if(!b64buf)
    {
        return -PVFS_ENOMEM;
    }
    memcpy(b64buf, fs_conf->secret_key, b64len);

    /* for some reason openssl's base64 decoding needs a newline at the end */
    b64buf[b64len] = '\n';

    b64 = BIO_new(BIO_f_base64());
    mem = BIO_new_mem_buf(b64buf, b64len+1);
    bio = BIO_push(b64, mem);

    len = BIO_pending(bio);
    *key = malloc(len);
    if(!*key)
    {
        BIO_free_all(bio);
        return -PVFS_ENOMEM;
    }
    
    *length = BIO_read(bio, *key, len);

    free(b64buf);
    
    BIO_free_all(bio);
    return 0;
#endif /* HAVE_OPENSSL */
}

#ifdef __PVFS2_TROVE_SUPPORT__

/*
 * create a storage space based on configuration settings object
 * with the particular host settings local to the caller
 */
int PINT_config_pvfs2_mkspace(struct server_configuration_s *config)
{
    int ret = 1;
    int s = 0;
    int create_collection_only = 0;
    PINT_llist *cur = NULL;
    PVFS_handle root_handle = PVFS_HANDLE_NULL;
    PVFS_handle root_dirdata_handle = PVFS_HANDLE_NULL;
    PVFS_SID *root_sid_array = NULL;
    int root_sid_count = 0;

    /* V3 obsolete - no more handle ranges */
#if 0
    char *cur_meta_handle_range = NULL, *cur_data_handle_range = NULL;
#endif

    filesystem_configuration_t *cur_fs = NULL;

    if (config)
    {
        cur = config->file_systems;
        while(cur)
        {
            cur_fs = PINT_llist_head(cur);
            if (!cur_fs)
            {
                break;
            }

            /*
             * for the first fs/collection we encounter, create the
             * storage space if it doesn't exist.
             */
            gossip_debug(
                GOSSIP_SERVER_DEBUG,"\n*****************************\n");
            gossip_debug(
                GOSSIP_SERVER_DEBUG, "Creating new PVFS2 %s\n",
                (create_collection_only ? "collection" :
                 "storage space"));

            /* see if this is a root server and if not do not pass the
             * handles.  Look through the root SIDs if one matches that
             * of the current host, then this is a root server and we
             * pass the handles and such
             */

            for(s = 0; s < cur_fs->root_sid_count; s++)
            {
                if (PVFS_SID_EQ(&cur_fs->root_sid_array[s],
                                  &config->host_sid))
                {
                    root_handle = cur_fs->root_handle;
                    root_dirdata_handle = cur_fs->root_dirdata_handle;
                    root_sid_array = cur_fs->root_sid_array;
                    root_sid_count = cur_fs->root_sid_count;
                    break;
                }
            }

            ret = pvfs2_mkspace(config->data_path,
                                config->meta_path,
                                config->config_path,
                                cur_fs->file_system_name,
                                cur_fs->coll_id,
                                root_handle,
                                root_dirdata_handle,
                                root_sid_array,
                                root_sid_count,
/* V3 nolonger used */
#if 0
                                cur_meta_handle_range, /* V3 fix this */
                                cur_data_handle_range, /* V3 fix this */
#endif
                                create_collection_only,
                                1);

            gossip_debug(
                GOSSIP_SERVER_DEBUG,"\n*****************************\n");

            /*
             * now that the storage space is created, set the
             * create_collection_only variable so that subsequent
             * calls to pvfs2_mkspace will not fail when it finds
             * that the storage space already exists; this causes
             * pvfs2_mkspace to only add the collection to the
             * already existing storage space.
             */
            create_collection_only = 1;

            cur = PINT_llist_next(cur);
        }
    }
    return ret;
}

/*
 * remove a storage space based on configuration settings object
 * with the particular host settings local to the caller
 */
int PINT_config_pvfs2_rmspace(struct server_configuration_s *config)
{
    int ret = 1;
    int remove_collection_only = 0;
    PINT_llist *cur = NULL;
    filesystem_configuration_t *cur_fs = NULL;

    if (config)
    {
        cur = config->file_systems;
        while(cur)
        {
            cur_fs = PINT_llist_head(cur);
            if (!cur_fs)
            {
                break;
            }

            cur = PINT_llist_next(cur);
            remove_collection_only = (PINT_llist_head(cur) ? 1 : 0);

            gossip_debug(
                GOSSIP_SERVER_DEBUG,"\n*****************************\n");
            gossip_debug(
                GOSSIP_SERVER_DEBUG,"Removing existing PVFS2 %s\n",
                (remove_collection_only ? "collection" :
                 "storage space"));
            ret = pvfs2_rmspace(config->data_path,
                                config->meta_path,
                                config->config_path,
                                cur_fs->file_system_name,
                                cur_fs->coll_id,
                                remove_collection_only,
                                1);
            gossip_debug(
                GOSSIP_SERVER_DEBUG,"\n*****************************\n");
        }
    }
    return ret;
}

/*
 * returns the metadata sync mode (storage hint) for the specified
 * fs_id if valid; TROVE_SYNC otherwise
 */
int PINT_config_get_trove_sync_meta(struct server_configuration_s *config,
                                    PVFS_fs_id fs_id)
{
    struct filesystem_configuration_s *fs_conf = NULL;

    if (config)
    {
        fs_conf = PINT_config_find_fs_id(config, fs_id);
    }
    return (fs_conf ? fs_conf->trove_sync_meta : TROVE_SYNC);
}

/*
 * returns the data sync mode (storage hint) for the specified
 * fs_id if valid; TROVE_SYNC otherwise
 */
int PINT_config_get_trove_sync_data(struct server_configuration_s *config,
                                    PVFS_fs_id fs_id)
{
    struct filesystem_configuration_s *fs_conf = NULL;

    if (config)
    {
        fs_conf = PINT_config_find_fs_id(config, fs_id);
    }
    return (fs_conf ? fs_conf->trove_sync_data : TROVE_SYNC);
}

#endif

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

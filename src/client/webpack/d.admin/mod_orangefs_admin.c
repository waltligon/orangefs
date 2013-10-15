#include <ctype.h>
#include <string.h>

#include <syslog.h>

#include <httpd.h>
#include <ap_config.h>
#include <ap_provider.h>
#include <http_core.h>
#include <http_config.h>
#include <http_log.h>
#include <http_protocol.h>
#include <http_request.h>
#include <apr_strings.h>

#include <pvfs2.h>
#include <pvfs2-mgmt.h>
#include <pint-distribution.h>

#include <sys/types.h>
#include <pwd.h>

#include "jsmn.h"

static unsigned int url_parent(char *url)
{
    unsigned int len;
    char *s;
    unsigned int i;
    len = strlen(url);
    s = url + len - 1;
    for (i = 0; i < len; s--, i++) {
        if (*s == '/' && i != 0)
            break;
    }
    s++;
    return s-url;
}

static char err[256];
#define HANDLE_ERR(call, rc, r) \
if (rc == -PVFS_ENOENT)\
    return HTTP_NOT_FOUND;\
else if (rc == -PVFS_EACCES)\
    return HTTP_FORBIDDEN;\
if (rc < 0) {\
    PVFS_strerror_r(rc, err, 256);\
    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,\
                  "mod_orangefs_admin:" __FILE__ ":%u: %s "\
                  "failed (%d) %s.", __LINE__, call, rc, err);\
    return HTTP_INTERNAL_SERVER_ERROR;\
}

typedef struct {
    request_rec *r;
    char *path;
    PVFS_fs_id fsid;
    PVFS_credentials cred;
} req_t;

static int handler_attr_get(req_t *req)
{
    int rc;
    PVFS_sysresp_lookup lookup;
    PVFS_sysresp_getattr getattr;

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                  "mod_orangefs_admin:handler_attr_get");

    /* Construct JSON of attributes. This covers most standard UNIX
       attributes. */

    apr_table_add(req->r->headers_out, "Content-type",
                  "application/json");

    rc = PVFS_sys_lookup(req->fsid, req->path, &req->cred, &lookup,
                         PVFS2_LOOKUP_LINK_NO_FOLLOW, 0);
    HANDLE_ERR("PVFS_sys_lookup", rc, req->r);

    rc = PVFS_sys_getattr(lookup.ref, PVFS_ATTR_SYS_COMMON_ALL
                          |PVFS_ATTR_SYS_SIZE
                          |PVFS_ATTR_SYS_DIRENT_COUNT,
                          &req->cred,
                          &getattr, 0);
    HANDLE_ERR("PVFS_sys_lookup", rc, req->r);

    ap_rprintf(req->r, "{\"owner\":%u,"
               "\"group\":%u,"
               "\"perms\":%u,"
               "\"atime\":%lu,"
               "\"mtime\":%lu,"
               "\"ctime\":%lu,"
               "\"size\":%ld,"
               "\"objtype\":",
               getattr.attr.owner,
               getattr.attr.group,
               getattr.attr.perms,
               getattr.attr.atime,
               getattr.attr.mtime,
               getattr.attr.ctime,
               getattr.attr.size);
    switch (getattr.attr.objtype) {
    case PVFS_TYPE_NONE:
        ap_rprintf(req->r, "\"none\"");
        break;
    case PVFS_TYPE_METAFILE:
        ap_rprintf(req->r, "\"metafile\"");
        break;
    case PVFS_TYPE_DATAFILE:
        ap_rprintf(req->r, "\"datafile\"");
        break;
    case PVFS_TYPE_DIRECTORY:
        ap_rprintf(req->r, "\"directory\",\"dirent_count\":%lu",
                   getattr.attr.dirent_count);
        break;
    case PVFS_TYPE_SYMLINK:
        ap_rprintf(req->r, "\"symlink\"");
        break;
    case PVFS_TYPE_DIRDATA:
        ap_rprintf(req->r, "\"dirdata\"");
        break;
    case PVFS_TYPE_INTERNAL:
        ap_rprintf(req->r, "\"internal\"");
        break;
    default:
        ap_rprintf(req->r, "\"unknown\"");
    }
    ap_rprintf(req->r, "}");
    return OK;
}

static int handler_attr_put(req_t *req)
{
    int rc;
    PVFS_sysresp_lookup lookup;
    PVFS_sysresp_getattr getattr;
    unsigned int bytes;
    char buffer[4096];
    jsmn_parser p;
    jsmntok_t tokens[50];
    unsigned long i;

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                  "mod_orangefs_admin:handler_attr_put");

    /* Change attributes. This accepts JSON in the same format as output
       from handler_attr_get. Attributes which do not exist or are not
       possible for the filesystem to change are silently ignored. */

    if (ap_setup_client_block(req->r, REQUEST_CHUNKED_DECHUNK)
        != OK) {
        return HTTP_BAD_REQUEST;
    }

    rc = PVFS_sys_lookup(req->fsid, req->path, &req->cred, &lookup,
                         PVFS2_LOOKUP_LINK_NO_FOLLOW, 0);
    HANDLE_ERR("PVFS_sys_lookup", rc, req->r);

    rc = PVFS_sys_getattr(lookup.ref, PVFS_ATTR_SYS_COMMON_ALL
                          |PVFS_ATTR_SYS_SIZE
                          |PVFS_ATTR_SYS_DIRENT_COUNT,
                          &req->cred,
                          &getattr, 0);
    HANDLE_ERR("PVFS_sys_lookup", rc, req->r);

    /* Get data. This returns HTTP 400 if input is more than 4
       kilobytes. */
    if (ap_should_client_block(req->r)) {
        bytes = ap_get_client_block(req->r, (char*)buffer, 4096);
        if (bytes >= 4096) {
            return HTTP_BAD_REQUEST;
        }
        buffer[bytes] = 0;
    }

    /* Parse JSON and change the attr structure. */
    jsmn_init(&p);
    if (jsmn_parse(&p, buffer, tokens, 50) != JSMN_SUCCESS) {
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    if (tokens[0].type != JSMN_OBJECT) {
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    getattr.attr.mask = 0;
    for (i = 1; i < tokens[0].size+1; i++) {
        if (strncmp(buffer+tokens[i].start, "owner", 5) == 0) {
            i++;
            getattr.attr.owner = strtol(buffer+tokens[i].start, 0, 10);
            getattr.attr.mask = getattr.attr.mask | PVFS_ATTR_SYS_UID;
        } else if (strncmp(buffer+tokens[i].start, "group", 5) == 0) {
            i++;
            getattr.attr.group = strtol(buffer+tokens[i].start, 0, 10);
            getattr.attr.mask = getattr.attr.mask | PVFS_ATTR_SYS_GID;
        } else if (strncmp(buffer+tokens[i].start, "perms", 5) == 0) {
            i++;
            /* Handle octal numbers. Perhaps this should be done
               everywhere. */
            if (buffer[tokens[i].start] == '0')
                getattr.attr.perms = strtol(buffer+tokens[i].start, 0,
                                            8);
            else
                getattr.attr.perms = strtol(buffer+tokens[i].start, 0,
                                            10);
            getattr.attr.mask = getattr.attr.mask | PVFS_ATTR_SYS_PERM;
        } else {
            i++;
        }
    }

    /* Change attributes and return the new data. */
    rc = PVFS_sys_setattr(lookup.ref, getattr.attr, &req->cred,
                          PVFS_HINT_NULL);
    HANDLE_ERR("PVFS_sys_setattr", rc, req->r);
    return handler_attr_get(req);
}

static int handler_attr(req_t *req)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                  "mod_orangefs_admin:handler_attr");

    if (req->r->method_number == M_OPTIONS) {
        apr_table_add(req->r->headers_out, "Allow", "GET,PUT");
        return OK;
    }
    switch (req->r->method_number) {
    case M_GET:
        return handler_attr_get(req);
    case M_PUT:
        return handler_attr_put(req);
    default:
        return HTTP_NOT_IMPLEMENTED;
    }
}

static int handler_dir_delete(req_t *req)
{
    int i;
    char parent[PVFS_PATH_MAX];
    char name[PVFS_NAME_MAX];
    int rc;
    PVFS_sysresp_lookup lookup;

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                  "mod_orangefs_admin:handler_dir_delete");

    /* Get parent directory. */

    i = url_parent(req->path);
    if (i < PVFS_PATH_MAX)
        strncpy(parent, req->path, i);
    else
        return HTTP_REQUEST_URI_TOO_LARGE;

    strncpy(name, req->path+i, PVFS_NAME_MAX);
    i = strlen(name)-1;
    while (name[i] == '/')
        name[i--] = 0;

    rc = PVFS_sys_lookup(req->fsid, parent, &req->cred, &lookup,
                         PVFS2_LOOKUP_LINK_NO_FOLLOW, 0);
    HANDLE_ERR("PVFS_sys_lookup", rc, req->r);

    rc = PVFS_sys_remove(name, lookup.ref, &req->cred, 0);
    HANDLE_ERR("PVFS_sys_remove", rc, req->r);

    return OK;
}

static int handler_dir_get(req_t *req)
{
    int rc;
    PVFS_sysresp_lookup lookup;
    PVFS_ds_position token;
    PVFS_sysresp_readdir readdir;
    unsigned int i;

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                  "mod_orangefs_admin:handler_dir_get");

    /* Return an array containing the contents of the directory. */

    apr_table_add(req->r->headers_out, "Content-type",
                  "application/json");

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                  "mod_orangefs_admin:handler_dir_get lookup %d %s",
                  req->fsid, req->path);

    rc = PVFS_sys_lookup(req->fsid, req->path, &req->cred, &lookup,
                         PVFS2_LOOKUP_LINK_NO_FOLLOW, 0);
    HANDLE_ERR("PVFS_sys_lookup", rc, req->r);

    ap_rprintf(req->r, "[");
    token = PVFS_ITERATE_START;
    while (token != PVFS_ITERATE_END) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                      "mod_orangefs_admin:handler_dir_get readdir %d",
                      token);
        rc = PVFS_sys_readdir(lookup.ref, token, 100, &req->cred,
                              &readdir, 0);
        if (rc != 0)
            free(readdir.dirent_array);
        HANDLE_ERR("PVFS_sys_readdir", rc, req->r);
        for (i = 0; i < readdir.pvfs_dirent_outcount; i++) {
            if (i != 0 || token != PVFS_ITERATE_START)
                ap_rprintf(req->r, ",");
            ap_rprintf(req->r, "\"%s\"",
                       readdir.dirent_array[i].d_name);
        }
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                      "mod_orangefs_admin:handler_dir_get readdir "
                      "got %d", readdir.pvfs_dirent_outcount);
        free(readdir.dirent_array);
        token = readdir.token;
    }
    ap_rprintf(req->r, "]");
    return OK;
}

static int handler_dir_put(req_t *req)
{
    int i;
    char parent[PVFS_PATH_MAX];
    char name[PVFS_NAME_MAX];
    int rc;
    PVFS_sysresp_lookup lookup;
    PVFS_sysresp_getattr getattr;
    PVFS_sysresp_mkdir mkdir;

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                  "mod_orangefs_admin:handler_dir_put");

    /* Create a directory. This is not recursive. */

    i = url_parent(req->path);
    if (i < PVFS_PATH_MAX)
        strncpy(parent, req->path, i);
    else
        return HTTP_REQUEST_URI_TOO_LARGE;

    strncpy(name, req->path+i, PVFS_NAME_MAX);
    i = strlen(name)-1;
    while (name[i] == '/')
        name[i--] = 0;

    rc = PVFS_sys_lookup(req->fsid, parent, &req->cred, &lookup,
                         PVFS2_LOOKUP_LINK_NO_FOLLOW, 0);
        return HTTP_NOT_FOUND;
    HANDLE_ERR("PVFS_sys_lookup", rc, req->r);

    rc = PVFS_sys_getattr(lookup.ref, PVFS_ATTR_SYS_COMMON_ALL
                          |PVFS_ATTR_SYS_SIZE
                          |PVFS_ATTR_SYS_DIRENT_COUNT,
                          &req->cred,
                          &getattr, 0);
    HANDLE_ERR("PVFS_sys_getattr", rc, req->r);

    rc = PVFS_sys_mkdir(name, lookup.ref, getattr.attr, &req->cred,
                        &mkdir, 0);
    HANDLE_ERR("PVFS_sys_mkdir", rc, req->r);

    return OK;
}

static int handler_dir(req_t *req)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                  "mod_orangefs_admin:handler_dir");

    if (req->r->method_number == M_OPTIONS) {
        apr_table_add(req->r->headers_out, "Allow", "DELETE,GET,PUT");
        return OK;
    }
    switch (req->r->method_number) {
    case M_DELETE:
        return handler_dir_delete(req);
    case M_GET:
        return handler_dir_get(req);
    case M_PUT:
        return handler_dir_put(req);
    default:
        return HTTP_NOT_IMPLEMENTED;
    }
}

static int handler_dist_get(req_t *req)
{
    int rc;
    PVFS_sysresp_lookup lookup;
    PVFS_ds_keyval key, val;
    PINT_dist *dist;

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                  "mod_orangefs_admin:handler_dist_get");

    apr_table_add(req->r->headers_out, "Content-type",
                  "application/json");

    key.buffer = "system.pvfs2.md";
    key.buffer_sz = strlen(key.buffer)+1;
    val.buffer = apr_palloc(req->r->pool, 4096);
    val.buffer_sz = 4096;

    rc = PVFS_sys_lookup(req->fsid, req->path, &req->cred, &lookup,
                         PVFS2_LOOKUP_LINK_NO_FOLLOW, 0);
    HANDLE_ERR("PVFS_sys_lookup", rc, req->r);

    rc = PVFS_sys_geteattr(lookup.ref, &req->cred, &key, &val, 0);
    HANDLE_ERR("PVFS_sys_geteattr", rc, req->r);

	/* XXX: Server location; params has a newline. */
    PINT_dist_decode(&dist, val.buffer);
    ap_rprintf(req->r, "{\"dist_name\":\"%s\",\"params\":\"%s\"}",
               dist->dist_name, dist->methods->params_string(dist->params));
    PINT_dist_free(dist);
    
    return OK;
}

static int handler_dist(req_t *req)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                  "mod_orangefs_admin:handler_dist");

    if (req->r->method_number == M_OPTIONS) {
        apr_table_add(req->r->headers_out, "Allow", "GET,PUT");
        return OK;
    }
    switch (req->r->method_number) {
    case M_GET:
        return handler_dist_get(req);
    default:
        return HTTP_NOT_IMPLEMENTED;
    }
}

static int handler_eattr_get(req_t *req)
{
    int rc;
    PVFS_sysresp_lookup lookup;
    PVFS_sysresp_getattr getattr;
    PVFS_sysresp_listeattr listeattr;
    unsigned long i;
    PVFS_ds_keyval val;
    unsigned long j;

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                  "mod_orangefs_admin:handler_eattr_get");

    /* Retrieve extended attribute. These are often binary data. */

    apr_table_add(req->r->headers_out, "Content-type",
                  "application/json");

    rc = PVFS_sys_lookup(req->fsid, req->path, &req->cred, &lookup,
                         PVFS2_LOOKUP_LINK_NO_FOLLOW, 0);
    HANDLE_ERR("PVFS_sys_lookup", rc, req->r);

    rc = PVFS_sys_getattr(lookup.ref, PVFS_ATTR_SYS_COMMON_ALL
                          |PVFS_ATTR_SYS_SIZE
                          |PVFS_ATTR_SYS_DIRENT_COUNT,
                          &req->cred,
                          &getattr, 0);
    HANDLE_ERR("PVFS_sys_getattr", rc, req->r);

    rc = PVFS_sys_listeattr(lookup.ref, PVFS_ITERATE_START, 0,
                            &req->cred, &listeattr, 0);
    HANDLE_ERR("PVFS_sys_listeattr", rc, req->r);

    /* Obtain the list of keys. */
    listeattr.token = PVFS_ITERATE_START;
    listeattr.key_array = apr_palloc(req->r->pool,
                                     sizeof(PVFS_ds_keyval)
                                     *listeattr.nkey);
    for (i = 0; i < listeattr.nkey; i++) {
        listeattr.key_array[i].buffer = apr_palloc(req->r->pool,
                                                   PVFS_NAME_MAX);
        listeattr.key_array[i].buffer_sz = PVFS_NAME_MAX;
    }
    rc = PVFS_sys_listeattr(lookup.ref, PVFS_ITERATE_START,
                            listeattr.nkey, &req->cred, &listeattr, 0);
    HANDLE_ERR("PVFS_sys_listeattr", rc, req->r);

    /* Obtain values and generate JSON. */
    ap_rprintf(req->r, "[");
    for (i = 0; i < listeattr.nkey; i++) {
        val.buffer = apr_palloc(req->r->pool, PVFS_NAME_MAX);
        val.buffer_sz = PVFS_NAME_MAX;
        rc = PVFS_sys_geteattr(lookup.ref, &req->cred,
                               listeattr.key_array+i, &val, 0);
        HANDLE_ERR("PVFS_sys_geteattr", rc, req->r);
        ap_rprintf(req->r, "{\"key\":\"%s\",\"value\":[",
                   (char*)listeattr.key_array[i].buffer);
        for (j = 0; j < val.read_sz; j++) {
            ap_rprintf(req->r, "%u%s", ((unsigned char*)val.buffer)[j],
                       j == val.read_sz-1 ? "" : ",");
        }
        ap_rprintf(req->r, "]}%s", i == listeattr.nkey-1 ? "" : ",");
    }
    ap_rprintf(req->r, "]");

    return OK;
}

static int handler_eattr(req_t *req)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                  "mod_orangefs_admin:handler_eattr");

    if (req->r->method_number == M_OPTIONS) {
        apr_table_add(req->r->headers_out, "Allow", "GET");
        return OK;
    }
    switch (req->r->method_number) {
    case M_GET:
        return handler_eattr_get(req);
    default:
        return HTTP_NOT_IMPLEMENTED;
    }
}

static int handler_io_get(req_t *req)
{
    int rc;
    PVFS_sysresp_lookup lookup;
    PVFS_sysresp_getattr getattr;
    PVFS_Request file_req, mem_req;
    unsigned int i;
    unsigned char buffer[4096];
    PVFS_sysresp_io io;

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                  "mod_orangefs_admin:handler_io_get");

    /* Copy a file to the client. */

    rc = PVFS_sys_lookup(req->fsid, req->path, &req->cred, &lookup,
                         PVFS2_LOOKUP_LINK_NO_FOLLOW, 0);
    HANDLE_ERR("PVFS_sys_lookup", rc, req->r);

    rc = PVFS_sys_getattr(lookup.ref, PVFS_ATTR_SYS_COMMON_ALL
                          |PVFS_ATTR_SYS_SIZE
                          |PVFS_ATTR_SYS_DIRENT_COUNT,
                          &req->cred,
                          &getattr, 0);
    HANDLE_ERR("PVFS_sys_lookup", rc, req->r);

    io.total_completed = 1;
    i = 0;
    while (io.total_completed > 0) {
        file_req = PVFS_BYTE;
        rc = PVFS_Request_contiguous(4096, PVFS_BYTE, &mem_req);
        HANDLE_ERR("PVFS_Request_contiguous", rc, req->r);
        rc = PVFS_sys_io(lookup.ref, file_req, i, buffer, mem_req,
                         &req->cred, &io, PVFS_IO_READ, 0);
        HANDLE_ERR("PVFS_sys_io", rc, req->r);
        ap_rwrite(buffer, io.total_completed, req->r);
        PVFS_Request_free(&mem_req);
        i = i + io.total_completed;
    }
    return OK;
}

static int handler_io_put(req_t *req)
{
    char parent[PVFS_PATH_MAX];
    char name[PVFS_NAME_MAX];
    int rc;
    PVFS_sysresp_lookup lookup_parent, lookup;
    PVFS_sysresp_getattr getattr;
    PVFS_sysresp_create create;
    PVFS_Request file_req, mem_req;
    unsigned int i, bytes;
    unsigned char buffer[4096];
    PVFS_sysresp_io io;

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                  "mod_orangefs_admin:handler_io_put");

    /* Copy a file from the client. */

    if (ap_setup_client_block(req->r, REQUEST_CHUNKED_DECHUNK)
        != OK) {
        return HTTP_BAD_REQUEST;
    }

    i = url_parent(req->path);
    if (i < PVFS_PATH_MAX)
        strncpy(parent, req->path, i);
    else
        return HTTP_REQUEST_URI_TOO_LARGE;

    strncpy(name, req->path+i, PVFS_NAME_MAX);
    i = strlen(name)-1;
    while (name[i] == '/')
        name[i--] = 0;

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                  "mod_orangefs_admin:handler_io_put parent %s",
                  parent);

    /* Create the file if necessary. */
    rc = PVFS_sys_lookup(req->fsid, req->path, &req->cred, &lookup,
                         PVFS2_LOOKUP_LINK_NO_FOLLOW, 0);
    if (rc == -PVFS_ENOENT) {
        rc = PVFS_sys_lookup(req->fsid, parent, &req->cred,
                             &lookup_parent,
                             PVFS2_LOOKUP_LINK_NO_FOLLOW, 0);
        HANDLE_ERR("PVFS_sys_lookup", rc, req->r);
        rc = PVFS_sys_getattr(lookup_parent.ref,
                              PVFS_ATTR_SYS_COMMON_ALL, &req->cred,
                              &getattr, 0);
        HANDLE_ERR("PVFS_sys_getattr", rc, req->r);
        rc = PVFS_sys_create(name, lookup_parent.ref, getattr.attr,
                             &req->cred, 0, &create, 0, PVFS_HINT_NULL);
        HANDLE_ERR("PVFS_sys_create", rc, req->r);
        rc = PVFS_sys_lookup(req->fsid, req->path, &req->cred,
                             &lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW, 0);
        HANDLE_ERR("PVFS_sys_lookup", rc, req->r);
        
    } else {
        HANDLE_ERR("PVFS_sys_lookup", rc, req->r);
    }

    rc = PVFS_sys_getattr(lookup_parent.ref, PVFS_ATTR_SYS_COMMON_ALL
                          |PVFS_ATTR_SYS_SIZE
                          |PVFS_ATTR_SYS_DIRENT_COUNT,
                          &req->cred,
                          &getattr, 0);
    HANDLE_ERR("PVFS_sys_getattr", rc, req->r);

    /* Truncate the file. */
    rc = PVFS_sys_truncate(lookup.ref, 0, &req->cred, 0);
    HANDLE_ERR("PVFS_sys_truncate", rc, req->r);

    i = 0;
    if (ap_should_client_block(req->r)) {
        for (bytes = ap_get_client_block(req->r, (char*)buffer, 4096);
             bytes > 0;
             bytes = ap_get_client_block(req->r, (char*)buffer, 4096)) {
            file_req = PVFS_BYTE;
            rc = PVFS_Request_contiguous(bytes, PVFS_BYTE, &mem_req);
            HANDLE_ERR("PVFS_Request_contiguous", rc, req->r);
            rc = PVFS_sys_io(lookup.ref, file_req, i, buffer, mem_req,
                             &req->cred, &io, PVFS_IO_WRITE, 0);
            HANDLE_ERR("PVFS_sys_io", rc, req->r);
            PVFS_Request_free(&mem_req);
            i = i + io.total_completed;
        }
    }
    return OK;
}

static int handler_io(req_t *req)
{
    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                  "mod_orangefs_admin:handler_io");

    if (req->r->method_number == M_OPTIONS) {
        apr_table_add(req->r->headers_out, "Allow", "GET,PUT");
        return OK;
    }
    switch (req->r->method_number) {
    case M_GET:
        return handler_io_get(req);
    case M_PUT:
        return handler_io_put(req);
    default:
        return HTTP_NOT_IMPLEMENTED;
    }
}

static int handler_statfs(req_t *req)
{
    int rc;
    PVFS_sysresp_statfs resp;
    struct PVFS_mgmt_server_stat *stat_array;
    int outcount, i;

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, req->r,
                  "mod_orangefs_admin:handler_statfs");

    if (req->r->method_number == M_OPTIONS) {
        apr_table_add(req->r->headers_out, "Allow", "GET");
        return OK;
    }
    if (req->r->method_number != M_GET)
        return HTTP_NOT_IMPLEMENTED;

    rc = PVFS_sys_statfs(req->fsid, &req->cred, &resp, 0);
    HANDLE_ERR("PVFS_sys_statfs", rc, req->r);
    ap_rprintf(req->r, "{\"fsid\":%d,"
                       "\"bytesAvailable\":%ld,"
                       "\"bytesTotal\":%ld,"
                       "\"handlesAvailable\":%lu,"
                       "\"handlesTotal\":%lu,\"servers\":[",
               resp.statfs_buf.fs_id,
               resp.statfs_buf.bytes_available,
               resp.statfs_buf.bytes_total,
               resp.statfs_buf.handles_available_count,
               resp.statfs_buf.handles_total_count);

    stat_array = apr_palloc(req->r->pool,
                            sizeof(struct PVFS_mgmt_server_stat)
                            *resp.server_count);
    outcount = resp.server_count;
    rc = PVFS_mgmt_statfs_all(req->fsid, &req->cred, stat_array,
                              &outcount, 0, 0);
    HANDLE_ERR("PVFS_mgmt_statfs_all", rc, req->r);
    for (i = 0; i < outcount; i++) {
        ap_rprintf(req->r, "{\"bmiAddress\":\"%s\","
                           "\"ramTotalBytes\":%lu,"
                           "\"ramFreeBytes\":%lu,"
                           "\"load1\":%lu,"
                           "\"load5\":%lu,"
                           "\"load15\":%lu,"
                           "\"uptimeSeconds\":%lu,",
                   stat_array[i].bmi_address,
                   stat_array[i].ram_total_bytes,
                   stat_array[i].ram_free_bytes,
                   stat_array[i].load_1,
                   stat_array[i].load_5,
                   stat_array[i].load_15,
                   stat_array[i].uptime_seconds);
    ap_rprintf(req->r, "\"type\":[");
    if (stat_array[i].server_type & PVFS_MGMT_IO_SERVER
        && stat_array[i].server_type & PVFS_MGMT_META_SERVER)
        ap_rprintf(req->r, "\"io\",\"meta\"");
    else if (stat_array[i].server_type & PVFS_MGMT_IO_SERVER)
        ap_rprintf(req->r, "\"io\"");
    else if (stat_array[i].server_type & PVFS_MGMT_META_SERVER)
        ap_rprintf(req->r, "\"meta\"");
    ap_rprintf(req->r, "]");
    ap_rprintf(req->r, "}%s", i == outcount-1 ? "" : ",");
    }
    ap_rprintf(req->r, "]}");
    return OK;
}

static char *certpath;

static int handler(request_rec *r)
{
    req_t req;
    struct passwd *pw;
    char uids[8], gids[8];
    struct orangefs_file_system *fs;
    unsigned int len;
    char *s, *ss;
    char method[16];
    char path[PVFS_PATH_MAX];
    int rc;

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                  "mod_orangefs_admin:handler");

    if (!r->handler || strcmp(r->handler, "orangefs_admin") != 0)
        return DECLINED;

    req.r = r;

    if (r->user) {
        pw = getpwnam(r->user);
    } else {
        pw = getpwnam("nobody");
    }
    if (pw) {
        snprintf(uids, 8, "%d", pw->pw_uid);
        snprintf(gids, 8, "%d", pw->pw_gid);
        if (certpath)
          rc = PVFS_util_gen_credential(uids, gids, 0,
               apr_pstrcat(r->pool, certpath, "/", r->user, "-key.pem"),
               apr_pstrcat(r->pool, certpath, "/", r->user, "-cert.pem"),
               &req.cred);
        else
          rc = PVFS_util_gen_credential(uids, gids, 0, 0, 0, &req.cred);
    } else {
        rc = PVFS_util_gen_credential(
             apr_table_get(r->subprocess_env, "AUTHENTICATE_UIDNUMBER"),
             apr_table_get(r->subprocess_env, "AUTHENTICATE_GIDNUMBER"),
             0,
             apr_pstrcat(r->pool, certpath, "/", r->user, "-key.pem"),
             apr_pstrcat(r->pool, certpath, "/", r->user, "-cert.pem"),
             &req.cred);
    }
    if (rc != 0) {
        ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                      "mod_orangefs_admin:handler "
                      "PVFS_util_gen_credential error %d", rc);
    }

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                  "mod_orangefs_admin:handler uid %s gid %s",
                  uids, gids);

    if (r->path_info == 0)
        return HTTP_NOT_FOUND;

    s = r->path_info;
    if (*s == '/')
        s++;
    ss = strchr(s, '/');
    if (ss == 0)
        return HTTP_NOT_FOUND;
    *ss = 0;
    strncpy(method, s, 16);
    rc = PVFS_util_resolve(ss+1, &(req.fsid), path, PVFS_PATH_MAX);
    if (rc == -PVFS_EINVAL)
        return HTTP_NOT_FOUND;
    HANDLE_ERR("PVFS_util_resolve", rc, r);
    if (*path == 0) {
        path[0] = '/';
        path[1] = 0;
    }
    req.path = path;

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r,
                  "mod_orangefs_admin:handler method %s path %s",
                  method, path);

    if (strcmp(method, "attr") == 0) {
        return handler_attr(&req);
    } else if (strcmp(method, "dir") == 0) {
        return handler_dir(&req);
    } else if (strcmp(method, "dist") == 0) {
        return handler_dist(&req);
    } else if (strcmp(method, "eattr") == 0) {
        return handler_eattr(&req);
    } else if (strcmp(method, "io") == 0) {
        return handler_io(&req);
    } else if (strcmp(method, "statfs") == 0) {
        return handler_statfs(&req);
    }
    return HTTP_NOT_IMPLEMENTED;
}

static int pvfsinit;

static int post_config(apr_pool_t *pconf, apr_pool_t *plog,
                       apr_pool_t *ptemp, server_rec *s)
{
    int rc;

    ap_log_perror(APLOG_MARK, APLOG_DEBUG, 0, plog,
                  "mod_orangefs_admin:post_config");

    if (!pvfsinit)
        return 0;

    rc = PVFS_util_init_defaults();
    if (rc < 0) {
        PVFS_strerror_r(rc, err, 256);
        ap_log_perror(APLOG_MARK, APLOG_DEBUG, 0, plog,
                      "mod_orangefs_admin:" __FILE__ ":%u: "
                      "PVFS_util_init_defaults failed (%d) %s.",
                      __LINE__, rc, err);
        return !OK;
    }
    ap_add_version_component(pconf, PROVIDER_NAME "/" VERSION);
    /*gossip_set_debug_mask(1, GOSSIP_MAX_DEBUG);*/
    return OK;
}

static const char *orangefs_admin_pvfsinit(cmd_parms *cmd, void *cfg,
                                           const char *arg1)
{
    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, cmd->server,
                  "mod_orangefs_admin:orangefs_admin_pvfsinit %s",
                  arg1);

    pvfsinit = strcmp(arg1, "orangefs_admin_module") == 0;
    return 0;
}

static const char *orangefs_admin_certpath(cmd_parms *cmd, void *cfg,
                                           const char *arg1)
{
    certpath = apr_pstrdup(cmd->pool, arg1);
    return 0;
}

static const command_rec directives[] = {
    AP_INIT_TAKE1("PVFSInit", orangefs_admin_pvfsinit,
                  NULL, RSRC_CONF,
                  "Set to this module's name to specify that this "
                  "module should initialize PVFS. (default is On.)"),
    AP_INIT_TAKE1("OrangeFSAdminCertpath", orangefs_admin_certpath,
                  0, ACCESS_CONF, "The path to read user keys and "
                  "certificates from."),
    { NULL }
};

static void register_hooks(apr_pool_t *pool)
{
    ap_hook_handler(handler, NULL, NULL, APR_HOOK_LAST);
    ap_hook_post_config(post_config, NULL, NULL, APR_HOOK_LAST);
}

module AP_MODULE_DECLARE_DATA orangefs_admin_module = {
    STANDARD20_MODULE_STUFF,
    NULL,
    NULL,
    NULL,
    NULL,
    directives,
    register_hooks
};

/* Copyright (C) 2010  Clemson University Research Foundation            */
/* Copyright (C) 2010  Omnibond Systems, LLC.                            */
/*                                                                       */
/* This program is free software: you can redistribute it and/or modify  */
/* it under the terms of the GNU General Public License as published by  */
/* the Free Software Foundation, either version 3 of the License, or     */
/* (at your option) any later version.                                   */
/*                                                                       */
/* This program is distributed in the hope that it will be useful,       */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of        */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         */
/* GNU General Public License for more details.                          */
/*                                                                       */
/* You should have received a copy of the GNU General Public License     */
/* along with this program.  If not, see <http://www.gnu.org/licenses/>. */
/*                                                                       */

/*!
 *  \file mod_orangefs_s3.c
 *  \brief Apache PVFS2 module.
 *
 *
 */


#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <pwd.h>

#include <pvfs2.h>
#include <pvfs2-types.h>
#include <pvfs2-sysint.h>
#include <pvfs2-util.h>
#include <pvfs2-request.h>

#include <apr.h>
#include <apr_optional.h>
#include <apr_strings.h>
#include <apr_md5.h>
#include <apr_lib.h>
#include <apr_base64.h>
#include <apr_want.h>
#include <apr_thread_proc.h>

#include <ap_provider.h>
#include <httpd.h>
#include <http_protocol.h>
#include <http_config.h>
#include <http_core.h>
#include <http_log.h>
#include <http_main.h>
#include <http_request.h>


#include <libxml/xmlreader.h>
#include <libxml/catalog.h>
#include <libxml/xmlregexp.h>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>


#define PVFS2_VERSION "mod_orangefs_s3/1.0"
#define ON "on"

#ifndef LIBXML_READER_ENABLED
#error "Error:  xmlReader support not compiled in"
#endif

/* function prototypes */
static int orangefs_s3_handler(request_rec *r);
static int orangefs_s3_post_config(apr_pool_t *pconf, apr_pool_t *plog, 
                                   apr_pool_t *ptemp, server_rec *s);
static int orangefs_s3_open_logs(apr_pool_t *pconf, apr_pool_t *plog, 
                                 apr_pool_t *ptemp, server_rec *s);
static void* orangefs_s3_create_srv_config(apr_pool_t *pool, server_rec *srv);
static void *orangefs_s3_merge_srv_config(apr_pool_t *p, 
                                          void *base_, 
                                          void *vhost_);
static const char* orangefs_s3_setTraceOn(cmd_parms *cmd, void *cfg);
static const char* orangefs_s3_setBucketRoot(cmd_parms *cmd, void *cfg, 
                                             const char *bucket_root);
static const char* set_PVFSInit(cmd_parms *cmd,
                                   void *cfg,
                                   const char *arg);
static const char* orangefs_s3_setOwnerID(cmd_parms *cmd, void *cfg, 
                                          const char *ownerID);
static const char* orangefs_s3_setDisplayName(cmd_parms *cmd, void *cfg, 
                                              const char *displayName);
static const char* orangefs_s3_addAWSAccount(cmd_parms *cmd, void *cfg,
			                     const char *args);
static void orangefs_s3_register_hooks(apr_pool_t *pool);


/* mod_orangefs_s3 server configuration */
typedef struct {
  apr_pool_t *modulePool;
  char *bucket_root;
  const char *PVFSInit;
  char *ownerID;
  char *displayName;
  char pvfs_path[PVFS_NAME_MAX];
  apr_hash_t *awsAccounts;
  int fsid;
  int quit;
} orangefs_s3_config;

/* mod_orangefs_s3 configuration options */
static const command_rec orangefs_s3_cmds[] = {
  AP_INIT_TAKE1("BucketRoot", orangefs_s3_setBucketRoot,
                NULL, OR_ALL, "Bucket root for s3"),
  AP_INIT_TAKE1("PVFSInit", set_PVFSInit,NULL,RSRC_CONF,
                "Set to this module's name to specify that this module "
                "should initialize PVFS. (default is On.)"),
  AP_INIT_TAKE1("OwnerID", orangefs_s3_setOwnerID,
                NULL, OR_ALL, "root owner ID for s3"),
  AP_INIT_TAKE1("DisplayName", orangefs_s3_setDisplayName,
                NULL, OR_ALL, "root display name for s3"),
  AP_INIT_RAW_ARGS("AWSAccount", orangefs_s3_addAWSAccount,
                   NULL, OR_ALL, "Add AWS Account"),
  AP_INIT_NO_ARGS("TraceOn", orangefs_s3_setTraceOn,
                  NULL, ACCESS_CONF, "OrangeFS Trace On"),
  {NULL}
};

/* mod_orangefs_s3 apache2 module definition */
module AP_MODULE_DECLARE_DATA orangefs_s3_module = {
  STANDARD20_MODULE_STUFF,
  NULL,
  NULL,
  orangefs_s3_create_srv_config,
  orangefs_s3_merge_srv_config,
  orangefs_s3_cmds,
  orangefs_s3_register_hooks
};

/* 
   struct orangefs_s3_asw_account

     Contains an entry for AWS authentication, including id, key, uid and gid.
 */
typedef struct {
  char *access_id;
  char *secret_key;
  char *uid;
  char *gid;
} orangefs_s3_aws_account;

/*
  struct orangefs_s3_request

    Contains all request related information to pass along to internal routines.
 */
typedef struct {
    request_rec *r;
    orangefs_s3_config *conf;
    apr_pool_t *pool;
    apr_hash_t *params;
    PVFS_credentials *credentials;
    PVFS_credentials *root;
    char *cn;
    char *sn;
} orangefs_s3_request;

/*
  struct orangefs_s3_resource
 
   Convenience structure to hold everything related to an individual 
   PVFS2 resource.
 */
typedef struct {
    char *pvfs_path;
    PVFS_dirent *entry;
    PVFS_object_ref obj;
} orangefs_s3_resource;

/*
  struct orangefs_s3_s3_list
 
    Struct to describe a container within the S3 namespace of PVFS2
 */
typedef struct {
  char *bucket;
  char *prefix;
  char *marker;
  char *delimiter;
} orangefs_s3_s3_list;


/* some defines and constants */
const char *EXT_ATTR_S3_CREATE_DATE        = "user.s3.create-date";
const char *EXT_ATTR_S3_OWNER_ID           = "user.s3.owner.id";
const char *EXT_ATTR_S3_OWNER_DISPLAY_NAME = "user.s3.owner.display-name";
const char *EXT_ATTR_S3_ENTITY_TAG         = "user.s3.entity-tag";
const char *EXT_ATTR_S3_SIZE               = "user.s3.size";

const int PERM_S3_FULL_CONTROL 	= 1;
const int PERM_S3_WRITE		= 2;
const int PERM_S3_WRITE_ACP	= 4;
const int PERM_S3_READ		= 8;
const int PERM_S3_READ_ACP	= 16;

/* global debug flag. We'll make this settable from orangefs_s3_setTraceOn
   or some other proper place, for now it is just set.
 */
int debug_orangefs_s3 = 1;

/*
   Read the POST request data and return it, along with the size,
   into an allocated buffer.
  
   Returns 0 on error, non-zero on success
 */
static int orangefs_s3_load_post_data(request_rec *r, 
                                      char **post_data, 
                                      apr_size_t *sz)
{
  apr_status_t status;
  int end = 0;
  apr_size_t bytes;
  const char *buf;
  apr_bucket *b;
  apr_bucket_brigade *bb;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"orangefs_s3_load_post_data:");
  }

  *post_data = NULL;
  *sz = 0;

  /* initialize the bucket brigade from the request */
  bb = apr_brigade_create(r->pool, r->connection->bucket_alloc);

  /* loop over each bucket until we get an EOS */
  do {
    status = ap_get_brigade(r->input_filters, bb, AP_MODE_READBYTES,
                            APR_BLOCK_READ, 4096);
    if (status == APR_SUCCESS) {
      for (b = APR_BRIGADE_FIRST(bb); 
           b!= APR_BRIGADE_SENTINEL(bb);
           b = APR_BUCKET_NEXT(b)) 
      {
  
        /* check for EOS */
        if (APR_BUCKET_IS_EOS(b)) {
          end = 1;
          break;
        } else if (APR_BUCKET_IS_METADATA(b)) {
          /* do not read metadata */
          continue;
        }

        /* read into buf */
        status = apr_bucket_read(b, &buf, &bytes, APR_BLOCK_READ);
        if (status == APR_SUCCESS) {
          if (*post_data == NULL) {
            *post_data = apr_pcalloc(r->pool, bytes+1);
            memcpy(*post_data, buf, bytes);
            *sz = bytes;
          } else {
            const char *ptr = *post_data;
            *post_data = apr_pcalloc(r->pool, *sz+bytes+1);
            memcpy(*post_data, ptr, *sz);
            memcpy(*post_data+*sz, buf, bytes);
            *sz += bytes;
          }
        } else {
          return 0;
        }
      }
    }

    apr_brigade_cleanup(bb);
  } while (!end && (status == APR_SUCCESS));
 
  return 1; 
}

static apr_hash_t *parse_form_from_string(request_rec *r, char *args)
{
  apr_hash_t *form;
  apr_array_header_t *values;
  char *pair;
  char *eq;
  const char *delim = "&";
  char *last;
  char **ptr;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"parse_form_from_string:");
  }

  if (args == NULL) {
    return NULL;
  }

  form = apr_hash_make(r->pool);

  /* Split the input on '&' */
  for (pair = apr_strtok(args, delim, &last); 
       pair != NULL;
       pair = apr_strtok(NULL, delim, &last)) 
  {

    for (eq = pair; *eq; ++eq) {
      if (*eq == '+') {
        *eq = ' ';
      }
    }

    /* split into Key / Value and unescape it */
    eq = strchr(pair, '=');
    
    if (eq) {
      *eq++ = '\0';
      ap_unescape_url(pair);
      ap_unescape_url(eq);
    } else {
      eq = "";
      ap_unescape_url(pair);
    }

    values = apr_hash_get(form, pair, APR_HASH_KEY_STRING);
    if (values == NULL) {
      values = apr_array_make(r->pool, 1, sizeof(const char*));
      apr_hash_set(form, pair, APR_HASH_KEY_STRING, values);
    }
    ptr = apr_array_push(values);
    *ptr = apr_pstrdup(r->pool, eq); 
  }

  return form;
}

/*
   The orangefs_s3_recurse routine is used by various functions inside 
   this module whenever a task needs to recurse the file system and 
   execute a visitor pattern.
  
     req       structure containing the original request
     resource  structure describing the requested resource
     userInfo  arbitrary parameter to be passed onto the visitor pattern
     visitor   function to invoke when visiting a resource recursively
  
 */
static int orangefs_s3_recurse(orangefs_s3_request *req, 
                               orangefs_s3_resource *resource, 
                               void *userInfo,
                               void(*visitor)(orangefs_s3_request *req, 
                                              orangefs_s3_resource *resource, 
                                              void *userInfo))
{
  PVFS_sysresp_getattr getattr_response;
  PVFS_sysresp_readdir readdir_response;
  PVFS_ds_position token = PVFS_READDIR_START;
  int rc, i;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"orangefs_s3_recurse:");
  }

  /* first call visitor for "this" node */
  visitor(req, resource, userInfo);

  memset(&getattr_response, 0, sizeof(PVFS_sysresp_getattr));
  rc = PVFS_sys_getattr(resource->obj, PVFS_ATTR_SYS_ALL, 
                        req->credentials, &getattr_response, NULL);
  if (rc != 0) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
                 "PVFS_sys_getattr returned %d.", rc);
    return HTTP_INTERNAL_SERVER_ERROR;
  }

  if (getattr_response.attr.objtype == PVFS_TYPE_DIRECTORY) {
    /* read the entire directory */
    do {
      memset(&readdir_response, 0, sizeof(PVFS_sysresp_readdir));
      rc = PVFS_sys_readdir(resource->obj, token, 60, req->credentials, 
                            &readdir_response, NULL);
      if (rc < 0) {
        ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
                     "PVFS_sys_readdir returned %d.", rc);
        return HTTP_INTERNAL_SERVER_ERROR;
      }

      for (i = 0; i < readdir_response.pvfs_dirent_outcount; i++) {
        orangefs_s3_resource *child_resource = 
          apr_pcalloc(req->pool, sizeof(orangefs_s3_resource));
  
        child_resource->entry = &readdir_response.dirent_array[i];
        child_resource->obj.handle = readdir_response.dirent_array[i].handle;
        child_resource->obj.fs_id = resource->obj.fs_id;
        child_resource->pvfs_path = 
          apr_pstrcat(req->pool, resource->pvfs_path, "/", 
                      readdir_response.dirent_array[i].d_name, NULL);

        rc = orangefs_s3_recurse(req, child_resource, userInfo, visitor);
        if (rc != OK)
          return rc;
      }

      token = readdir_response.token;
    } while ((token != PVFS_READDIR_END) && !req->conf->quit);
  }

  return OK;
}

static char * orangefs_s3_bin_to_hex(apr_pool_t *pool, 
                                     unsigned char *bin, 
                                     int length)
{
  int i;
  char hexval[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 
                      'a', 'b', 'c', 'd', 'e', 'f' };
  char *hexstr = apr_pcalloc(pool, length * 2 + 1);
  
  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"orangefs_s3_bin_to_hex:");
  }

  for (i = 0; i < length; i++) {
    hexstr[i*2] = hexval[(bin[i] >> 4) & 0xf];
    hexstr[(i*2)+1] = hexval[bin[i] & 0x0f];
  }

  return hexstr;
}

/*
   This routine will write the contents of the POST data to a PVFS2 object and
   return the contents MD5 sum.
 */
static int orangefs_s3_write_post_data_ref(orangefs_s3_request *req, 
                                           PVFS_object_ref *ref, 
                                           PVFS_hint hints, 
                                           size_t *size, 
                                           unsigned char *md5)
{
  apr_status_t status;
  int end = 0;
  apr_size_t bytes;
  const char *buf;
  apr_bucket *b;
  apr_bucket_brigade *bb;
  PVFS_Request mem_req, file_req;
  PVFS_sysresp_io resp_io;
  size_t offset = 0;
  apr_md5_ctx_t md5_ctx;
  int rc;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
                 "orangefs_s3_write_post_data_ref:");
  }

  /* initialize the bucket brigade from the request */
  bb = apr_brigade_create(req->r->pool, req->r->connection->bucket_alloc);

  apr_md5_init(&md5_ctx);

  /* loop over each bucket until we get an EOS */
  do {
    status = ap_get_brigade(req->r->input_filters, bb, AP_MODE_READBYTES,
                            APR_BLOCK_READ, 4096);
    if (status == APR_SUCCESS) {
      for (b = APR_BRIGADE_FIRST(bb);
           b!= APR_BRIGADE_SENTINEL(bb);
           b = APR_BUCKET_NEXT(b)) {

        /* check for EOS */
        if (APR_BUCKET_IS_EOS(b)) {
          end = 1;
          break;
        } else if (APR_BUCKET_IS_METADATA(b)) {
          /* do not read metadata */
          continue;
        }

        /* read into buf */
        status = apr_bucket_read(b, &buf, &bytes, APR_BLOCK_READ);
        if (status == APR_SUCCESS) {
          file_req = PVFS_BYTE;
          rc = PVFS_Request_contiguous(bytes, PVFS_BYTE, &mem_req);
          if (rc < 0) {
            ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                         "PVFS_sys_resolve returned %d.", rc);
            return -1;
          }

          rc = PVFS_sys_write(*ref, file_req, offset, (void *)buf, mem_req,
                              req->credentials, &resp_io, hints);

          offset += bytes;

          PVFS_Request_free(&mem_req);

          apr_md5_update(&md5_ctx, buf, bytes);

        } else {
          return -1;
        }
      }
    }

    apr_brigade_cleanup(bb);
  } while (!end && (status == APR_SUCCESS));

  apr_md5_final(md5, &md5_ctx);
  *size = offset;

  return 0;
}

/*
   Checks the owner_id against the entry's owner and ACL's to determine
   whether the owner_id is authorized to access the entry with permission.
 */
static int orangefs_s3_authorized(orangefs_s3_request *req, 
                                  PVFS_object_ref *entry, 
                                  int permission)
{
  int rc = 0;
  PVFS_ds_keyval key, val;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"orangefs_s3_authorized:");
  }

  key.buffer = apr_pstrdup(req->pool, EXT_ATTR_S3_OWNER_ID);
  key.buffer_sz = strlen(key.buffer) + 1;
  val.buffer = apr_pcalloc(req->pool, 4096);
  val.buffer_sz = 4096;

  rc = PVFS_sys_geteattr(*entry, req->root, &key, &val, NULL);
  if (rc >= 0) {
    /* check to see if we are the owner of this bucket */
    if (atoi((char*)val.buffer) == req->credentials->userid) {
      /* owner gets full permissions */
      rc = 1;
    } else {
      /* not owner */
      rc = 0;
    }
  } else {
    /* missing S3 owner-id attribute */
    rc = 0;
  }

  if (rc == 0) {
    /* not owner, check ACL's */

  }

  return rc;
}

static int orangefs_s3_copy_object(orangefs_s3_request *req, 
                                   char *bucket, 
                                   char *path, 
                                   char *source)
{

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"orangefs_s3_copy_object:");
  }

  return OK;
}

static int orangefs_s3_delete_object(orangefs_s3_request *req, 
                                     char *bucket, 
                                     char *path)
{
  char *parent_path, *entry_name;
  PVFS_sysresp_lookup resp_lookup;
  char *ptr;
  int rc;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "orangefs_s3_delete_object for bucket %s path %s.", 
                 bucket, path);
  }

  parent_path = apr_pstrcat(req->pool, req->conf->pvfs_path, "/", bucket, NULL);

  for (ptr = path + strlen(path) - 1; (ptr > path) && (*ptr != '/'); ptr--);

  if (ptr > path) {
    entry_name = apr_pstrdup(req->pool, ptr + 1);
    parent_path = apr_pstrcat(req->pool, parent_path,
                              apr_pstrndup(req->pool, path, 
                              (ptr - path)), NULL);
  } else {
    entry_name = apr_pstrdup(req->pool, path + 1);
  }

  memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
  rc = PVFS_sys_lookup(req->conf->fsid, parent_path, 
                       req->root, &resp_lookup, 
                       PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
  if (rc < 0) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "PVFS_sys_lookup returned %d.", rc);
    return HTTP_NOT_FOUND;
  }

  rc = PVFS_sys_remove(entry_name, resp_lookup.ref, req->credentials, NULL);
  if (rc < 0) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "PVFS_sys_remove returned %d.", rc);
    return HTTP_NOT_FOUND;
  }

  return OK;
}

/*
   When adding an object to a S3 bucket, it may be necessary to create
   parent directories along the way.  This routine creates all directories
   necessary (mkdir -p)
 */
static PVFS_object_ref * orangefs_s3_mkdir_p(orangefs_s3_request *req, 
                                             int fsid, 
                                             char *path)
{
  PVFS_sysresp_lookup *resp_lookup;
  PVFS_sysresp_mkdir mkdir_response;
  PVFS_sys_attr attr;
  PVFS_object_ref *parent_ref;
  PVFS_ds_keyval key, val;
  char *ptr, *entry_name, *parent_path = NULL;
  int mode = 493;
  int rc;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "orangefs_s3_mkdir_p() for path %s.", path);
  }

  resp_lookup = apr_pcalloc(req->pool, sizeof(PVFS_sysresp_lookup));
  rc = PVFS_sys_lookup(fsid, path, req->root, resp_lookup, 
                       PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
  if (rc == 0) {
    return &resp_lookup->ref;
  }

  for (ptr = path + strlen(path) - 1; (ptr > path) && (*ptr != '/'); ptr--);

  if (ptr > path) {
    entry_name = apr_pstrdup(req->pool, ptr + 1);
    parent_path = apr_pstrndup(req->pool, path, (ptr - path));
  } else {
    entry_name = apr_pstrdup(req->pool, path + 1);
  }

  /* make the parent, recursively */
  parent_ref = orangefs_s3_mkdir_p(req, fsid, parent_path);
  if (!parent_ref) {
    return NULL;
  }

  /* now make the entry */
  attr.owner = req->credentials->userid;
  attr.group = req->credentials->group_array[0];
  attr.perms = mode;
  attr.mask = (PVFS_ATTR_SYS_ALL_SETABLE);

  memset(&mkdir_response, 0, sizeof(PVFS_sysresp_mkdir));
  rc = PVFS_sys_mkdir(entry_name, *parent_ref, attr, 
                      req->credentials, &mkdir_response, NULL);
  if (rc < 0) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "PVFS_sys_mkdir() returned %d.", rc);
    return NULL;
  }

  /* assign the S3 owner attributes */
  key.buffer = (void*) apr_pstrdup(req->pool, EXT_ATTR_S3_OWNER_ID);
  key.buffer_sz = strlen(key.buffer) + 1;
  val.buffer = apr_pcalloc(req->pool, BUFSIZ);
  sprintf(val.buffer, "%d", req->credentials->userid);
  val.buffer_sz = strlen(val.buffer) + 1;

  rc = PVFS_sys_seteattr(mkdir_response.ref, req->credentials, 
                         &key, &val, 0, NULL);
  if (rc < 0) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "PVFS_sys_seteattr() for owner id returned rc %d.", rc);
  }

  key.buffer = (void*)EXT_ATTR_S3_OWNER_DISPLAY_NAME;
  key.buffer_sz = strlen(key.buffer) + 1;
  val.buffer = req->cn;
  val.buffer_sz = strlen(val.buffer) + 1;

  rc = PVFS_sys_seteattr(mkdir_response.ref, req->credentials, 
                         &key, &val, 0, NULL);
  if (rc < 0) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "PVFS_sys_seteattr() for owner display name returned rc %d.", 
                 rc);
  }

  rc = PVFS_sys_lookup(fsid, path, req->credentials, resp_lookup, 
                       PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
  if (rc == 0) {
    return &resp_lookup->ref;
  }

  return NULL;
}

static int orangefs_s3_put_object(orangefs_s3_request *req, 
                                  char *bucket, 
                                  char *path)
{
  char *entry_name, *parent_path, *entry_path;
  PVFS_sysresp_lookup resp_lookup;
  PVFS_sysresp_create resp_create;
  PVFS_object_ref *parent_ref;
  PVFS_object_ref *ref;
  PVFS_ds_keyval key, val;
  PVFS_sys_dist *new_dist = NULL;
  PVFS_sys_attr attr;
  PVFS_hint hints = NULL;
  unsigned char md5[APR_MD5_DIGESTSIZE];
  char tmp[1024];
  size_t size = 0;
  char *ptr;
  int rc, i;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "orangefs_s3_put_object for bucket %s path %s.", bucket, path);
  }

  PVFS_hint_import_env(&hints);

  entry_path = apr_pstrcat(req->pool, req->conf->pvfs_path, "/", 
                           bucket, path, NULL);

  memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
  rc = PVFS_sys_lookup(req->conf->fsid, entry_path, req->credentials, 
                       &resp_lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
  if (rc == 0) {
    /* file already exists */
    ref = &resp_lookup.ref;
  } else {
    /* does not exist, need to create it */
    entry_name = apr_pstrdup(req->pool, path);
    entry_name++;

    /* fill out our attr */
    attr.owner = req->credentials->userid;
    attr.group = req->credentials->group_array[0];
    attr.perms = 256;
    attr.mask = (PVFS_ATTR_SYS_ALL_SETABLE);
    attr.dfile_count = 0;

    parent_path = 
      apr_pstrcat(req->pool, req->conf->pvfs_path, "/", bucket, NULL);

    /* walk backwards from the end to find the last '/' */
    for (ptr = path + strlen(path) -1; (ptr > path) && (*ptr != '/'); ptr--);

    if (ptr > path) {
      entry_name = apr_pstrdup(req->pool, ptr + 1);
      parent_path = apr_pstrcat(req->pool, parent_path, 
                                apr_pstrndup(req->pool, path, 
                                (ptr - path)), NULL);
    } else {
      entry_name = apr_pstrdup(req->pool, path + 1);
    }
    
    parent_ref = orangefs_s3_mkdir_p(req, req->conf->fsid, parent_path);
    if (parent_ref == NULL) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                   "Unable to get or create parent directory %s", parent_path);
      return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* need to create the entry */
    rc = PVFS_sys_create(entry_name, *parent_ref, attr, req->credentials, 
                         new_dist, &resp_create, NULL, hints);
    if (rc < 0) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                   "PVFS_sys_create returned %d.", rc);
      return HTTP_INTERNAL_SERVER_ERROR;
    }

    ref = &resp_create.ref;
  }

  /* now we need to write the PUT/POST data */
  memset(md5, 0, APR_MD5_DIGESTSIZE);
  rc = orangefs_s3_write_post_data_ref(req, ref, hints, &size, md5);

  /* convert md5sum to hex string */
  {
    char hexval[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 
                        'a', 'b', 'c', 'd', 'e', 'f' };
  
    memset(tmp, 0, APR_MD5_DIGESTSIZE*2+1); 
    for (i = 0; i < APR_MD5_DIGESTSIZE; i++) {
      tmp[i*2] = hexval[(md5[i] >> 4) & 0xf];
      tmp[(i*2)+1] = hexval[md5[i] & 0x0f];
    }
  }

  key.buffer = (void*)EXT_ATTR_S3_ENTITY_TAG;
  key.buffer_sz = strlen(key.buffer) + 1;
  val.buffer = tmp;
  val.buffer_sz = strlen(val.buffer) + 1;

  rc = PVFS_sys_seteattr(*ref, req->credentials, &key, &val, 0, NULL);
  if (rc < 0) {

  }
 
  /* write out etag response header */
  apr_table_setn(req->r->headers_out, "ETag", 
                 apr_pstrcat(req->pool, "\"", tmp, "\"", NULL));

  key.buffer = (void*)EXT_ATTR_S3_SIZE;
  key.buffer_sz = strlen(key.buffer) + 1;
  sprintf(tmp, "%lu", size);
  val.buffer = tmp;
  val.buffer_sz = strlen(val.buffer) + 1;

  rc = PVFS_sys_seteattr(*ref, req->credentials, &key, &val, 0, NULL);
  if (rc < 0) {

  }

  key.buffer = (void*)EXT_ATTR_S3_OWNER_ID;
  key.buffer_sz = strlen(key.buffer) + 1;
  val.buffer = apr_pcalloc(req->pool, BUFSIZ);
  sprintf(val.buffer, "%d", req->credentials->userid);
  val.buffer_sz = strlen(val.buffer) + 1;

  rc = PVFS_sys_seteattr(*ref, req->credentials, &key, &val, 0, NULL);
  if (rc < 0) {

  }

  key.buffer = (void*)EXT_ATTR_S3_OWNER_DISPLAY_NAME;
  key.buffer_sz = strlen(key.buffer) + 1;
  val.buffer = req->cn;
  val.buffer_sz = strlen(val.buffer) + 1;

  rc = PVFS_sys_seteattr(*ref, req->credentials, &key, &val, 0, NULL);
  if (rc < 0) {

  }

  return OK;
}

static int orangefs_s3_get_object(orangefs_s3_request *req, 
                                  char *bucket, 
                                  char *path)
{
  char *entry_path;
  PVFS_sysresp_lookup resp_lookup;
  PVFS_Request mem_req, file_req;
  PVFS_sysresp_io resp_io;
  PVFS_object_ref *ref;
  PVFS_hint hints = NULL;
  PVFS_ds_keyval k, v;
  char buffer[4096];
  char buffer2[256];
  size_t offset = 0;
  int rc;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "orangefs_s3_get_object for bucket %s path %s.", bucket, path);
  }

  if (req->params) {
    apr_array_header_t *arr;

    arr = apr_hash_get(req->params, "acl", APR_HASH_KEY_STRING);
    if (arr) {
    }

    arr = apr_hash_get(req->params, "torrent", APR_HASH_KEY_STRING);
    if (arr) {
    }
  }

  entry_path = apr_pstrcat(req->pool, req->conf->pvfs_path, "/", 
                           bucket, path, NULL);

  memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
  rc = PVFS_sys_lookup(req->conf->fsid, entry_path, req->root, 
                       &resp_lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);

  if (rc < 0) {
    /* no such file */
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "PVFS_sys_lookup for %s returned %d.", entry_path, rc);
    return HTTP_NOT_FOUND;
  }

  ref = &resp_lookup.ref;
  PVFS_hint_import_env(&hints);

  k.buffer = (void*)EXT_ATTR_S3_SIZE;
  k.buffer_sz = strlen(k.buffer) + 1;
  v.buffer = buffer;
  v.buffer_sz = 4096;

  memset(buffer, 0, 4096);
  rc = PVFS_sys_geteattr(resp_lookup.ref, req->credentials, &k, &v, NULL);
  if (rc >= 0) {
    memset(buffer2, 0, 256);
    memcpy(buffer2, buffer, v.read_sz);
    apr_table_setn(req->r->headers_out, "Content-Length", 
                   apr_pstrdup(req->pool, (char*)buffer2));
  }

  k.buffer = (void*)EXT_ATTR_S3_ENTITY_TAG;
  k.buffer_sz = strlen(k.buffer) + 1;
  v.buffer = buffer;
  v.buffer_sz = 4096;

  memset(buffer, 0, 4096);
  rc = PVFS_sys_geteattr(resp_lookup.ref, req->credentials, &k, &v, NULL);
  if (rc >= 0) {
    memset(buffer2, 0, 256);
    memcpy(buffer2, buffer, v.read_sz);
    apr_table_setn(req->r->headers_out, "ETag", 
                   apr_pstrcat(req->pool, "\"", (char*)buffer2, "\"", NULL));
  }

  /* if it's a HEAD request, return without content */
  if (strcmp(req->r->method, "HEAD") == 0) {
    return OK;
  }

  do {
    file_req = PVFS_BYTE;
    rc = PVFS_Request_contiguous(4096, PVFS_BYTE, &mem_req);
    if (rc < 0) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                   "PVFS_Request_contiguous returned rc %d.", rc);
      return HTTP_INTERNAL_SERVER_ERROR;
    }

    rc = PVFS_sys_read(*ref, file_req, offset, buffer, mem_req, 
                       req->credentials, &resp_io, hints);
    if (rc < 0) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                   "PVFS_sys_read returned rc %d.", rc);
      return HTTP_INTERNAL_SERVER_ERROR;
    }

    offset += resp_io.total_completed;

    ap_rwrite(buffer, resp_io.total_completed, req->r);
  } while (resp_io.total_completed > 0);

  PVFS_Request_free(&mem_req);

  return OK;
}

static int orangefs_s3_get_bucket_acl(orangefs_s3_request *req, char *bucket)
{
  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "orangefs_s3_get_bucket_acl: bucket:%s:",bucket);
  }

  return OK;
}

static int orangefs_s3_get_bucket_lifecycle(orangefs_s3_request *req, 
                                            char *bucket)
{

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "orangefs_s3_get_bucket_lifecycle: bucket:%s:",bucket);
  }

  ap_rprintf(req->r, "<Error>");
  ap_rprintf(req->r, "  <Code>NoSuchLifecycleConfiguration</Code>");
  ap_rprintf(req->r, "  <Message>The lifecycle configuration does not exist.</Message>");
  ap_rprintf(req->r, "  <Resource>%s</Resource>", bucket);
  //ap_rprintf(req->r, "  <RequestId></RequestId>");
  ap_rprintf(req->r, "</Error>");

  return HTTP_NOT_FOUND;
}

static int orangefs_s3_get_bucket_policy(orangefs_s3_request *req, char *bucket)
{

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "orangefs_s3_get_bucket_policy: bucket:%s:",bucket);
  }

  ap_rprintf(req->r, "<Error>");
  ap_rprintf(req->r, "  <Code>NoSuchBucketPolicy</Code>");
  ap_rprintf(req->r, "  <Message>The specified bucket does not have a bucket policy.</Message>");
  ap_rprintf(req->r, "  <Resource>%s</Resource>", bucket);
  //ap_rprintf(req->r, "  <RequestId></RequestId>");
  ap_rprintf(req->r, "</Error>");

  return HTTP_NOT_FOUND;
}

static int orangefs_s3_get_bucket_location(orangefs_s3_request *req, 
                                           char *bucket)
{

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "orangefs_s3_get_bucket_location: bucket:%s:",bucket);
  }

  /* for now, just reply with nothing until we can figure out location */
  ap_rprintf(req->r, 
    "<LocationConstraint xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\"/>");

  return OK;
}

static int orangefs_s3_get_bucket_logging(orangefs_s3_request *req, char *bucket)
{

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "orangefs_s3_get_bucket_logging: bucket:%s:",bucket);
  }

  /* for now, just reply with nothing until we can figure out logging */
  ap_rprintf(req->r, 
    "<BucketLoggingStatus xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\"/>");

  return OK;
}

static int orangefs_s3_get_bucket_notification(orangefs_s3_request *req, 
                                               char *bucket)
{

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "orangefs_s3_get_bucket_notification: bucket:%s:",bucket);
  }

  /* for now, just reply with nothing until we can figure out notification */
  ap_rprintf(req->r, "<NotificationConfiguration/>");

  return OK;
}

static int orangefs_s3_get_bucket_versions(orangefs_s3_request *req, 
                                           char *bucket)
{

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "orangefs_s3_get_bucket_versions: bucket:%s:",bucket);
  }

  return HTTP_NOT_FOUND;
}

static int orangefs_s3_get_bucket_requestPayment(orangefs_s3_request *req, 
                                                 char *bucket)
{

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "orangefs_s3_get_bucket_requestPayment: bucket:%s:",bucket);
  }

  /* for now, just reply with nothing until we can figure out requestPayment */
  ap_rprintf(req->r, "<RequestPaymentConfiguration "
                     "xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\"/>");

  return OK;
}

static int orangefs_s3_get_bucket_versioning(orangefs_s3_request *req, 
                                             char *bucket)
{

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "orangefs_s3_get_bucket_versioning: bucket:%s:",bucket);
  }

  /* for now, just reply with nothing until we can figure out versioning */
  ap_rprintf(req->r, "<VersioningConfiguration "
                     "xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\"/>");

  return OK;
}

static int orangefs_s3_get_bucket_website(orangefs_s3_request *req, 
                                          char *bucket)
{

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "orangefs_s3_get_bucket_website: bucket:%s:",bucket);
  }

  /* for now, just reply with nothing until we can figure out website */
  ap_rprintf(req->r, "<WebsiteConfiguration "
                     "xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\"/>");

  return OK;
}

/*
  Each file and directory path is represented as a single object in an 
  S3 bucket, without a hierarchy, so to speak. This routine is a callback 
  routine to list the contents of an entire bucket, recursively treating 
  each file as an object.

  req is the original request
  resource is the current resource being returned
  userInfo is the optional parameter to be passed to each visitor callback
 */
static void orangefs_s3_get_bucket_recurse(orangefs_s3_request *req, 
                                           orangefs_s3_resource *resource, 
                                           void *userInfo)
{
  orangefs_s3_s3_list *listInfo;
  PVFS_ds_keyval key, val;
  PVFS_sysresp_getattr resp_getattr;
  char *s3_path = NULL;
  struct tm *time;
  char scratch_time[26];
  int rc;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"orangefs_s3_get_bucket_recurse:");
  }

  if (resource->entry == NULL) {
    return;
  }

  memset(&resp_getattr, 0, sizeof(PVFS_sysresp_getattr));
  if ((rc = PVFS_sys_getattr(resource->obj, PVFS_ATTR_SYS_ALL_NOHINT, 
                             req->credentials, &resp_getattr, NULL)) != 0) {
    return;
  }

  if (resp_getattr.attr.objtype == PVFS_TYPE_DIRECTORY) {
    return;
  }

  listInfo = (orangefs_s3_s3_list *)userInfo;

  ap_rprintf(req->r,   "<Contents>");

  /* the resource->pvfs_path includes the full bucket path as well, 
     so we need to strip that off */
  s3_path = strstr(resource->pvfs_path, listInfo->bucket) + 
            strlen(listInfo->bucket) + 1;

  ap_rprintf(req->r,     "<Key>%s</Key>", s3_path);

  time = localtime((const time_t*)&resp_getattr.attr.ctime);
  strftime(scratch_time, 26, "%FT%H:%M:%S.000Z", time);
  ap_rprintf(req->r,   "<LastModified>%s</LastModified>", scratch_time);

  key.buffer = apr_pstrdup(req->pool, EXT_ATTR_S3_ENTITY_TAG);
  key.buffer_sz = strlen(key.buffer) + 1;
  val.buffer = apr_pcalloc(req->pool, 4096);
  val.buffer_sz = 4096;

  rc = PVFS_sys_geteattr(resource->obj, req->credentials, &key, &val, NULL);
  if (rc >= 0) {
    ap_rprintf(req->r, "<ETag>&quot;%s&quot;</ETag>", (char*)val.buffer);
  } else {
    ap_rprintf(req->r, 
               "<ETag>&quot;00000000000000000000000000000000&quot;</ETag>");
  }

  key.buffer = apr_pstrdup(req->pool, EXT_ATTR_S3_SIZE);
  key.buffer_sz = strlen(key.buffer) + 1;
  val.buffer = apr_pcalloc(req->pool, 4096);
  val.buffer_sz = 4096;

  rc = PVFS_sys_geteattr(resource->obj, req->credentials, &key, &val, NULL);
  if (rc >= 0) {
    ap_rprintf(req->r,   "<Size>%s</Size>", (char*)val.buffer);
  } else {
    ap_rprintf(req->r,   "<Size>%lu</Size>", resp_getattr.attr.size);
  }

  ap_rprintf(req->r,     "<StorageClass>STANDARD</StorageClass>");

  ap_rprintf(req->r,     "<Owner>");

  key.buffer = apr_pstrdup(req->pool, EXT_ATTR_S3_OWNER_ID);
  key.buffer_sz = strlen(key.buffer) + 1;
  val.buffer = apr_pcalloc(req->pool, 4096);
  val.buffer_sz = 4096;

  rc = PVFS_sys_geteattr(resource->obj, req->credentials, &key, &val, NULL);
  if (rc >= 0)
    ap_rprintf(req->r,     "<ID>%s</ID>", (char*)val.buffer);

  key.buffer = apr_pstrdup(req->pool, EXT_ATTR_S3_OWNER_DISPLAY_NAME);
  key.buffer_sz = strlen(key.buffer) + 1;
  val.buffer = apr_pcalloc(req->pool, 4096);
  val.buffer_sz = 4096;

  rc = PVFS_sys_geteattr(resource->obj, req->credentials, &key, &val, NULL);
  if (rc >= 0) {
    ap_rprintf(req->r,     "<DisplayName>%s</DisplayName>", (char*)val.buffer);
  }

  ap_rprintf(req->r,     "</Owner>");

  ap_rprintf(req->r,   "</Contents>");
}

static int orangefs_s3_delete_bucket(orangefs_s3_request *req, char *bucket)
{
  char *parent_path, *entry_name;
  PVFS_sysresp_lookup resp_lookup;
  int rc;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
      "orangefs_s3_delete_bucket for bucket %s.", bucket);
  }

  if (req->params) {
    apr_array_header_t *arr;

    arr = apr_hash_get(req->params, "policy", APR_HASH_KEY_STRING);
    if (arr) {
    }

    arr = apr_hash_get(req->params, "website", APR_HASH_KEY_STRING);
    if (arr) {
    }
  }

  parent_path = apr_pstrdup(req->pool, req->conf->pvfs_path);
  entry_name = apr_pstrdup(req->pool, bucket);

  memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
  rc = PVFS_sys_lookup(req->conf->fsid, parent_path, req->credentials, 
                       &resp_lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
  if (rc < 0) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
      "PVFS_sys_resolve returned %d.", rc);
    return HTTP_NOT_FOUND;
  }

  rc = PVFS_sys_remove(entry_name, resp_lookup.ref, req->root, NULL);
  if (rc < 0) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "PVFS_sys_remove returned %d.",rc);
    return HTTP_CONFLICT;
  }

  return OK;
}

static int orangefs_s3_put_bucket(orangefs_s3_request *req, char *bucket)
{
  PVFS_sysresp_mkdir mkdir_response;
  PVFS_sysresp_lookup resp_lookup;
  PVFS_object_ref ref;
  PVFS_ds_keyval key, val;
  PVFS_sys_attr attr;
  int mode = 493;
  int rc;  

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
      "orangefs_s3_put_bucket for bucket %s.", bucket);
  }

  if (req->params) {
    apr_array_header_t *arr;

    arr = apr_hash_get(req->params, "x-amz-acl", APR_HASH_KEY_STRING);
    if (arr) {
    }

    arr = apr_hash_get(req->params, "acl", APR_HASH_KEY_STRING);
    if (arr) {
    }

    arr = apr_hash_get(req->params, "policy", APR_HASH_KEY_STRING);
    if (arr) {
    }

    arr = apr_hash_get(req->params, "logging", APR_HASH_KEY_STRING);
    if (arr) {
    }

    arr = apr_hash_get(req->params, "notification", APR_HASH_KEY_STRING);
    if (arr) {
    }

    arr = apr_hash_get(req->params, "requestPayment", APR_HASH_KEY_STRING);
    if (arr) {
    }

    arr = apr_hash_get(req->params, "versioning", APR_HASH_KEY_STRING);
    if (arr) {
    }

    arr = apr_hash_get(req->params, "website", APR_HASH_KEY_STRING);
    if (arr) {
    }
  }

  memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
  rc = PVFS_sys_lookup(req->conf->fsid, req->conf->pvfs_path, req->root, 
                       &resp_lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);

  if (rc < 0) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "PVFS_sys_lookup returned %d.", rc);
    return HTTP_INTERNAL_SERVER_ERROR;
  }

  ref.fs_id = resp_lookup.ref.fs_id;
  ref.handle = resp_lookup.ref.handle;

  attr.owner = req->credentials->userid;
  attr.group = req->credentials->group_array[0];
  attr.perms = mode;
  attr.mask = (PVFS_ATTR_SYS_ALL_SETABLE);

  memset(&mkdir_response, 0, sizeof(PVFS_sysresp_mkdir));
  rc = PVFS_sys_mkdir(bucket, ref, attr, req->root, &mkdir_response, NULL);
  if (rc < 0) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "PVFS_sys_mkdir() returned rc %d.", rc);
    return HTTP_INTERNAL_SERVER_ERROR;
  }

  /* assign the S3 owner attributes */
  key.buffer = (void*) apr_pstrdup(req->pool, EXT_ATTR_S3_OWNER_ID);
  key.buffer_sz = strlen(key.buffer) + 1;
  val.buffer = apr_pcalloc(req->pool, BUFSIZ);
  sprintf(val.buffer, "%d", req->credentials->userid);
  val.buffer_sz = strlen(val.buffer) + 1;

  rc = PVFS_sys_seteattr(mkdir_response.ref, req->root, &key, &val, 0, NULL);
  if (rc < 0) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                  "PVFS_sys_seteattr() for owner id returned rc %d.", rc);
  }

  key.buffer = (void*) apr_pstrdup(req->pool, EXT_ATTR_S3_OWNER_DISPLAY_NAME);
  key.buffer_sz = strlen(key.buffer) + 1;
  val.buffer = req->cn;
  val.buffer_sz = strlen(val.buffer) + 1;

  rc = PVFS_sys_seteattr(mkdir_response.ref, req->root, &key, &val, 0, NULL);
  if (rc < 0) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
                 "PVFS_sys_seteattr() for owner display name returned rc %d.", 
                 rc);
  }

  return OK;
}

static int orangefs_s3_get_bucket(orangefs_s3_request *req, char *bucket)
{
  char *delimiter = NULL, *marker = NULL;
  char *prefix = NULL;
  PVFS_sysresp_lookup resp_lookup;
  char pvfs_path[PVFS_NAME_MAX];
  orangefs_s3_resource resource;
  orangefs_s3_s3_list listInfo;
  int rc;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
      "orangefs_s3_get_bucket for bucket %s.", bucket);
  }

  if (req->params) {
    apr_array_header_t *arr;

    arr = apr_hash_get(req->params, "acl", APR_HASH_KEY_STRING);
    if (arr) {
      /* need to perform a bucket ACL get request */
      return orangefs_s3_get_bucket_acl(req, bucket);
    }

    arr = apr_hash_get(req->params, "lifecycle", APR_HASH_KEY_STRING);
    if (arr) {
      /* need to perform a bucket Lifecycle get request */
      return orangefs_s3_get_bucket_lifecycle(req, bucket);
    }

    arr = apr_hash_get(req->params, "policy", APR_HASH_KEY_STRING);
    if (arr) {
      /* need to perform a bucket policy get request */
      return orangefs_s3_get_bucket_policy(req, bucket);
    }

    arr = apr_hash_get(req->params, "location", APR_HASH_KEY_STRING);
    if (arr) {
      /* need to perform a bucket location get request */
      return orangefs_s3_get_bucket_location(req, bucket);
    }

    arr = apr_hash_get(req->params, "logging", APR_HASH_KEY_STRING);
    if (arr) {
      /* need to perform a bucket logging get request */
      return orangefs_s3_get_bucket_logging(req, bucket);
    }

    arr = apr_hash_get(req->params, "notification", APR_HASH_KEY_STRING);
    if (arr) {
      /* need to perform a bucket notification get request */
      return orangefs_s3_get_bucket_notification(req, bucket);
    }

    arr = apr_hash_get(req->params, "versions", APR_HASH_KEY_STRING);
    if (arr) {
      /* need to perform a bucket versions get request */
      return orangefs_s3_get_bucket_versions(req, bucket);
    }

    arr = apr_hash_get(req->params, "requestPayment", APR_HASH_KEY_STRING);
    if (arr) {
      /* need to perform a bucket requestPayment get request */
      return orangefs_s3_get_bucket_requestPayment(req, bucket);
    }

    arr = apr_hash_get(req->params, "versioning", APR_HASH_KEY_STRING);
    if (arr) {
      /* need to perform a bucket versioning get request */
      return orangefs_s3_get_bucket_versioning(req, bucket);
    }

    arr = apr_hash_get(req->params, "website", APR_HASH_KEY_STRING);
    if (arr) {
      /* need to perform a bucket website get request */
      return orangefs_s3_get_bucket_website(req, bucket);
    }
  }

  /* List Objects request */
  memset(pvfs_path,0,PVFS_NAME_MAX);
  strcpy(pvfs_path, req->conf->pvfs_path);
  strcat(pvfs_path, "/");
  strcat(pvfs_path, bucket);

  memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
  rc = PVFS_sys_lookup(req->conf->fsid, pvfs_path, req->root,
                       &resp_lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
  if (rc < 0) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "PVFS_sys_lookup returned %d.", rc);

    ap_rprintf(req->r, "<Error>");
    ap_rprintf(req->r, "  <Code>NoSuchBucket</Code>");
    ap_rprintf(req->r, "  <Message>The specified bucket does not exist.</Message>");
    ap_rprintf(req->r, "  <Resource>%s</Resource>", bucket);
    //ap_rprintf(req->r, "  <RequestId></RequestId>");
    ap_rprintf(req->r, "</Error>");

    return HTTP_NOT_FOUND;
  }

  ap_rprintf(req->r, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
  ap_rprintf(req->r, "<ListBucketResult xmlns=\"http://doc.s3.amazonaws.com/2006-03-01\">");
  ap_rprintf(req->r,   "<Name>%s</Name>", bucket);

  if (prefix) {
    ap_rprintf(req->r,   "<Prefix>%s</Prefix>", prefix);
  } else {
    ap_rprintf(req->r,   "<Prefix/>");
  }

  if (marker) {
    ap_rprintf(req->r,   "<Marker>%s</Marker>", marker);
  } else {
    ap_rprintf(req->r,   "<Marker/>");
  }

  ap_rprintf(req->r,   "<MaxKeys>%d</MaxKeys>", 1000);
  ap_rprintf(req->r,   "<IsTruncated>false</IsTruncated>");

  listInfo.bucket = bucket;
  listInfo.prefix = prefix;
  listInfo.marker = marker;
  listInfo.delimiter = delimiter;

  resource.pvfs_path = pvfs_path;
  resource.entry = NULL;
  memcpy(&resource.obj, &resp_lookup.ref, sizeof(resp_lookup.ref));

  rc = orangefs_s3_recurse(req, &resource, &listInfo, 
                           orangefs_s3_get_bucket_recurse);

  ap_rprintf(req->r, "</ListBucketResult>");

  return OK;
}

/*
 * This routine implements the ListAllMyBuckets request.  It uses the 
 * BucketRoot as the top-level directory.
 */
static int orangefs_s3_get_service(orangefs_s3_request *req)
{
  PVFS_sysresp_lookup resp_lookup;
  PVFS_sysresp_getattr resp_getattr;
  PVFS_sysresp_readdir resp_readdir;
  PVFS_ds_position token = PVFS_READDIR_START;
  PVFS_ds_keyval key, val;
  PVFS_object_ref entry;
  int count = 60;
  int rc, i;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"orangefs_s3_get_service:");
  }

  memset(&resp_lookup, 0, sizeof(PVFS_sysresp_lookup));
  rc = PVFS_sys_lookup(req->conf->fsid, req->conf->pvfs_path, req->root, 
                       &resp_lookup, PVFS2_LOOKUP_LINK_NO_FOLLOW, NULL);
  if (rc < 0) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "PVFS_sys_lookup for %s returned %d.", 
                 req->conf->pvfs_path, rc);
    return HTTP_INTERNAL_SERVER_ERROR;
  }

  memset(&resp_readdir, 0, sizeof(PVFS_sysresp_readdir));
  rc = PVFS_sys_readdir(resp_lookup.ref, token, count, req->root, 
                        &resp_readdir, NULL);
  if (rc < 0) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "PVFS_sys_readdir returned %d.", rc);
    return HTTP_INTERNAL_SERVER_ERROR;
  }

  ap_rprintf(req->r, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
  ap_rprintf(req->r, "<ListAllMyBucketsResult xmlns=\"http://doc.s3.amazonaws.com/2006-03-01\">");

  ap_rprintf(req->r,   "<Owner>"); 
  ap_rprintf(req->r,     "<ID>0</ID>"); 
  ap_rprintf(req->r,     "<DisplayName>s3-pvfs</DisplayName>"); 
  ap_rprintf(req->r,   "</Owner>"); 

  ap_rprintf(req->r,   "<Buckets>"); 

  for (i = 0; i < resp_readdir.pvfs_dirent_outcount; i++) {
    char *name = resp_readdir.dirent_array[i].d_name;
    entry.fs_id = req->conf->fsid;
    entry.handle = resp_readdir.dirent_array[i].handle; 

    if (!orangefs_s3_authorized(req, &entry, PERM_S3_READ)) {
      continue;
    }

    ap_rprintf(req->r,   "<Bucket>");
    ap_rprintf(req->r,     "<Name>%s</Name>", name);

    key.buffer = apr_pstrdup(req->pool, EXT_ATTR_S3_CREATE_DATE);
    key.buffer_sz = strlen(key.buffer) + 1;
    val.buffer = apr_pcalloc(req->pool, 4096);
    val.buffer_sz = 4096;

    rc = PVFS_sys_geteattr(entry, req->root, &key, &val, NULL);
    if (rc >= 0) {
      ap_rprintf(req->r, "<CreationDate>%s</CreationDate>", (char*)val.buffer);
    } else {
      struct tm *time;
      char scratch_time[26] = {0};

      /* use the ctime field */
      memset(&resp_getattr, 0, sizeof(PVFS_sysresp_getattr));
      if ((rc = PVFS_sys_getattr(entry, PVFS_ATTR_SYS_ALL_NOHINT, req->root, 
                                 &resp_getattr, NULL)) == 0) {
        time = localtime((const time_t*)&resp_getattr.attr.ctime);
        strftime(scratch_time, 26, "%FT%H:%M:%S.000Z", time);
        ap_rprintf(req->r,     "<CreationDate>%s</CreationDate>", 
                   scratch_time /*"2006-02-03T16:45:09.000Z"*/);
      }
    }

    ap_rprintf(req->r,   "</Bucket>");
  }

  ap_rprintf(req->r,   "</Buckets>"); 
  ap_rprintf(req->r, "</ListAllMyBucketsResult>"); 

  return OK;
}

static char *orangefs_s3_resolve_bucket(request_rec *r)
{
  char *bucket = NULL, *rest = NULL;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"orangefs_s3_resolve_bucket:");
  }

  /* check the Host header to see if the client specified a bucketname 
     in the DNS name 
   */
  if (strcmp(r->hostname, r->server->server_hostname) == 0) {
    return NULL;
  } else {
    bucket = apr_strtok((char *)r->hostname, ".", &rest);
  }

  return bucket;
}

static int orangefs_s3(orangefs_s3_request *req)
{
  int rc;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"orangefs_s3:");
  }

  rc = PVFS_util_resolve(req->conf->bucket_root, &req->conf->fsid, 
                         req->conf->pvfs_path, PVFS_NAME_MAX);

  if (rc < 0) {
    return HTTP_INTERNAL_SERVER_ERROR;
  }

  /* Amazon S3 interface on PVFS2 */
  ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
               "Using BucketRoot [%s], resolved to [%s].", 
               req->conf->bucket_root, req->conf->pvfs_path);

  /* check the Host header to see if the client specified a bucketname 
     in the DNS name 
   */
  if (strcmp(req->r->hostname, req->r->server->server_hostname) == 0) {
    /*
      The Host header matches our server site name.  This can mean the request
      is a service request (list buckets, etc.), or it could mean that the 
      bucket is specified on the URL (Walrus implementation)
    */
    if (strcmp(req->r->uri, "/") == 0) {
      /* we were given a s3 service operation request (List Buckets) */
      if (req->r->method_number == M_GET) {
        rc = orangefs_s3_get_service(req);
      } else {
        rc = HTTP_METHOD_NOT_ALLOWED;
      }
    } else {
      char *bucket, *rest;

      bucket = apr_strtok(apr_pstrdup(req->pool, req->r->uri), "/", &rest);

      if (apr_strtok(NULL, "/", &rest) == NULL) {
        /* bucket was specified on the service request, so we need to 
           perform bucket operations 
         */
        if (req->r->method_number == M_GET) {
          rc = orangefs_s3_get_bucket(req, bucket);
        } else if (req->r->method_number == M_DELETE) {
          rc = orangefs_s3_delete_bucket(req, bucket);
        }
      } else {
        char *path = (req->r->uri + strlen(bucket) + 1);

        ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                     "Processing s3 object request for bucket %s, object %s.", 
                     bucket, path);

        if (req->r->method_number == M_GET) {
          rc = orangefs_s3_get_object(req, bucket, path);
        } else if (req->r->method_number == M_PUT) {
          rc = orangefs_s3_put_object(req, bucket, path);
        } else if (req->r->method_number == M_DELETE) {
          rc = orangefs_s3_delete_object(req, bucket, path);
        } else {
          rc = HTTP_METHOD_NOT_ALLOWED;
        }
      }

    }
  } else {
    /* bucket or object request;  determine which bucket is being requested */
    char *bucket, *rest;

    bucket = apr_strtok((char *)req->r->hostname, ".", &rest);

    /* check if this is a bucket request or an object request by looking 
       at the URI 
     */
    if (strcmp(req->r->uri, "/") == 0) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                   "Processing s3 bucket request for %s.", bucket);

      if (req->r->method_number == M_GET) {
        rc = orangefs_s3_get_bucket(req, bucket);
      } else if (req->r->method_number == M_PUT) {
        rc = orangefs_s3_put_bucket(req, bucket);
      } else if (req->r->method_number == M_DELETE) {
        rc = orangefs_s3_delete_bucket(req, bucket);
      } else {
        rc = HTTP_METHOD_NOT_ALLOWED;
      }

    } else {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                   "Processing s3 object request for bucket %s, object %s.", 
                   bucket, req->r->uri);

      if (req->r->method_number == M_GET) {
        rc = orangefs_s3_get_object(req, bucket, req->r->uri);
      } else if (req->r->method_number == M_PUT) {
        /* check if this a PUT/copy or just a PUT by checking the 
           x-amz-copy-source header 
         */
        if (apr_table_get(req->r->headers_in, "x-amz-copy-source")) {
          rc = orangefs_s3_copy_object(req, bucket, req->r->uri,
               (char*)apr_table_get(req->r->headers_in, "x-amz-copy-source"));
        } else {
          rc = orangefs_s3_put_object(req, bucket, req->r->uri);
        }
      } else if (req->r->method_number == M_DELETE) {
        rc = orangefs_s3_delete_object(req, bucket, req->r->uri);
      } else {
        rc = HTTP_METHOD_NOT_ALLOWED;
      }
    }
  }

  return rc;
}

/*
   The main request content handler. Sets up the request structures
   and passes off to the internal s3 handler.
 */
static int orangefs_s3_handler(request_rec *r)
{
  orangefs_s3_config *conf;
  apr_pool_t *pool;
  int rc = OK;
  apr_hash_t *params;
  orangefs_s3_request *req;
  char *uid = NULL, *gid = NULL;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"orangefs_s3_handler:");
  }

  if (!r->handler || strcmp(r->handler, "orangefs_s3") != 0) {
    return DECLINED;
  }

  ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
               "[%s] accepted %s %s request on %s for %s.", 
               r->server->server_hostname, r->handler, r->method, 
               r->hostname, r->uri);

  uid = (char*)apr_table_get(r->subprocess_env, "AUTHENTICATE_UIDNUMBER");
  gid = (char*)apr_table_get(r->subprocess_env, "AUTHENTICATE_GIDNUMBER");

  if (uid == NULL || gid == NULL) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "Unable to resolve a uidNumber and gidNumber for "
                 "this request.");
    return HTTP_INTERNAL_SERVER_ERROR;
  }

  /* get our server configuration */
  conf = ap_get_module_config(r->server->module_config, &orangefs_s3_module);
  if (!conf) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "Unable to locate pvfs2 server configuration");
    return HTTP_INTERNAL_SERVER_ERROR;
  }

  /* create a temporary request pool */
  apr_pool_create(&pool, NULL);

  /* get our HTTP parameters */
  params = parse_form_from_string(r, r->args);

  /* fill out our request handle */
  req = apr_pcalloc(pool, sizeof(orangefs_s3_request));
  req->r = r;
  req->conf = conf;
  req->pool = pool;
  req->params = params;
  req->credentials = apr_pcalloc(pool, sizeof(PVFS_credentials));
  req->root = apr_pcalloc(pool, sizeof(PVFS_credentials));

  /* create PVFS credentials */
  PVFS_util_gen_credentials(req->credentials);
  if (uid && gid) {
    req->credentials->userid = atoi(uid);
    req->credentials->num_groups = 1;
    req->credentials->group_array = malloc(sizeof(PVFS_gid));
    req->credentials->group_array[0] = atoi(gid);
  } else {
    req->credentials->userid = 0;
    req->credentials->num_groups = 1;
    req->credentials->group_array = malloc(sizeof(PVFS_gid));
    req->credentials->group_array[0] = 0;
  }

  PVFS_util_gen_credentials(req->root);
  req->root->userid = 0;
  req->root->num_groups = 1;
  req->root->group_array = malloc(sizeof(PVFS_gid));
  req->root->group_array[0] = 0;

  req->cn = (char*)apr_table_get(r->subprocess_env, "AUTHENTICATE_CN");
  req->sn = (char*)apr_table_get(r->subprocess_env, "AUTHENTICATE_SN");

  /* check which interface module is requested */
  rc = orangefs_s3(req);

  apr_pool_destroy(pool);

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"Finished request.");
  }

  return rc;
}

/* Extracts/decodes the authorization header. */
static int orangefs_s3_get_aws_auth(request_rec *r, 
                                    const char **user, 
                                    const char **pw)
{
  const char *auth_line;
  char *rest;
  char *decoded_line;
  int length;
  char *hexstr;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"orangefs_s3_get_aws_auth:");
  }

  auth_line = apr_table_get(r->headers_in, (PROXYREQ_PROXY == r->proxyreq)
                                            ? "Proxy-Authorization"
                                            : "Authorization");

  if (!auth_line) {
    *user = NULL;
    *pw = NULL;
    return HTTP_UNAUTHORIZED;
  }

  auth_line = apr_pstrdup(r->pool, auth_line);
  if (!auth_line) {
    *user = NULL;
    *pw = NULL;
    return HTTP_UNAUTHORIZED;
  }

  if (strcasecmp(ap_getword(r->pool, &auth_line, ' '), "AWS")) {
    *user = NULL;
    *pw = NULL;
    return HTTP_UNAUTHORIZED;
  }

  while (apr_isspace(*auth_line)) {
    auth_line++;
  }

  *user = apr_strtok((char*)auth_line, ":", &rest);
  *pw = apr_strtok(NULL, ":", &rest);
  if (*user == 0 || *pw == 0) {
    return HTTP_UNAUTHORIZED;
  }

  decoded_line = apr_pcalloc(r->pool, apr_base64_decode_len(*pw) + 1);
  length = apr_base64_decode(decoded_line, *pw);
  *pw = decoded_line;

  r->user = (char *) *user;

  return OK;
}

/*
   This routine parses the request headers to generate the string to be
   signed and used for authorizing the request.  For reference, see:
  
     http://docs.amazonwebservices.com/AmazonS3/2006-03-01/dev/RESTAuthentication.html
  
   According to Amazon, the signature data consists of:
  
     StringToSign = HTTP-Verb + "\n" +
            Content-MD5 + "\n" +
            Content-Type + "\n" +
            Date + "\n" +
            CanonicalizedAmzHeaders + "\n" +
            CanonicalizedResource;
  
     CanonicalizedResource = [ "/" + Bucket ] +
            <HTTP-Request-URI, from the protocol name up to the query string> +
             [ sub-resource, if present.  For example "?acl", "?locaton", "?logging", or "?torrent"];
   
    CanonicalizedAmzHeaders = <described below>
  
 */
static char * orangefs_s3_get_signature_data(request_rec *r)
{
  const char *date = NULL, *content_md5 = NULL, 
             *content_type = NULL, *header = NULL, *bucket = NULL;
  apr_array_header_t *headers;
  char *data;
  int i;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"orangefs_s3_get_signature_data:");
  }

  /* HTTP-Verb + "\n" */
  data = apr_pstrdup(r->pool, r->method);
  data = apr_pstrcat(r->pool, data, "\n", NULL);

  /* Content-MD5 + "\n" */
  content_md5 = apr_table_get(r->headers_in, "Content-Md5");
  if (content_md5) {
    data = apr_pstrcat(r->pool, data, content_md5, NULL);
  }
  data = apr_pstrcat(r->pool, data, "\n", NULL);

  /* Content-Type+ "\n" */
  content_type = apr_table_get(r->headers_in, "Content-Type");
  if (content_type) {
    data = apr_pstrcat(r->pool, data, content_type, NULL);
  }
  data = apr_pstrcat(r->pool, data, "\n", NULL);

  /* Date + "\n" */
  date = apr_table_get(r->headers_in, "Date");
  if (date) {
    data = apr_pstrcat(r->pool, data, date, NULL);
  }
  data = apr_pstrcat(r->pool, data, "\n", NULL);

  /* CanonicalizedAmzHeaders + "\n" */
  /* TODO!!  We need to:
       - lower-case header name
       - sort them by header name
       - concat dup values, per RFC 2616
       - remove any newlines
   */
  headers = apr_array_copy(r->pool, apr_table_elts(r->headers_in));
  for (i = 0; i < headers->nelts*3; i+=3) {
    const char *s = ((const char **)headers->elts)[i];
    header = apr_table_get(r->headers_in, s);

    if (strstr(s, "x-amz-") == s || strstr(s, "X-Amz-") == s) {
      data = apr_pstrcat(r->pool, data, s, ":", header, NULL);
      data = apr_pstrcat(r->pool, data, "\n", NULL);
    }
  }

  /* CanonicalizedResource */
  bucket = orangefs_s3_resolve_bucket(r);
  if (bucket) {
    data = apr_pstrcat(r->pool, data, "/", bucket, NULL);
  }
  data = apr_pstrcat(r->pool, data, r->uri, NULL);

  return data;
}

/*
   Computes the signature for the authentication and validates the
   request.
 */
static int orangefs_s3_authenticate_aws_user(request_rec *r)
{
  orangefs_s3_config *conf = 
    (orangefs_s3_config *)ap_get_module_config(r->server->module_config, 
                                               &orangefs_s3_module);
  const char *access_id, *secret_key_signature, *current_auth;
  orangefs_s3_aws_account *account;
  const EVP_MD *evp_md = EVP_sha1();
  char *hexstr;
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int md_len;
  char *data;
  int rc;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
                 "orangefs_s3_authenticate_aws_user:");
  }

  current_auth = ap_auth_type(r);
  if (current_auth == NULL || strcmp(current_auth, "AWS"))
    return DECLINED;

  rc = orangefs_s3_get_aws_auth(r, &access_id, &secret_key_signature); 
  if (rc) {
    return rc;
  }

  /* convert to hex string for logging */
  hexstr = orangefs_s3_bin_to_hex(r->pool, 
                                 (unsigned char *)secret_key_signature, 20);

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "Authenticating AWS request for access id [%s]", access_id);
  }

  account = apr_hash_get(conf->awsAccounts, access_id, APR_HASH_KEY_STRING);

  if (account == NULL) {
    /* no account found in http configuration */
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "access_id [%s] not found in configuration", access_id);
  }

  if (account == NULL) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "access_id [%s] not found.", access_id);
    return HTTP_UNAUTHORIZED;
  }

  /* we found the account, now get the headers and check the signature */
  data = orangefs_s3_get_signature_data(r);

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"signing data [%s]", data);
  }

  /* create a signature for the headers */
  HMAC(evp_md, account->secret_key, strlen(account->secret_key), 
       (unsigned char*)data, strlen(data), md, &md_len);

  /* compare signatures */
  if (memcmp(secret_key_signature, md, md_len) != 0) {
    hexstr = orangefs_s3_bin_to_hex(r->pool, (unsigned char *)md, md_len);
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
                 "Signature [%s] does not match!", hexstr);
    return HTTP_UNAUTHORIZED;
  }

  apr_table_set(r->subprocess_env, "AUTHENTICATE_CN", account->access_id);
  if (account->uid && account->gid) {
    apr_table_set(r->subprocess_env, "AUTHENTICATE_UIDNUMBER", account->uid);
    apr_table_set(r->subprocess_env, "AUTHENTICATE_GIDNUMBER", account->gid);
  } else {
    /* no configured uid/gid, so use the OS to determine it */
    struct passwd *pw;

    pw = getpwnam(account->access_id);
    if (pw) {
      apr_table_set(r->subprocess_env, "AUTHENTICATE_UIDNUMBER", 
                    apr_itoa(r->pool, pw->pw_uid));
      apr_table_set(r->subprocess_env, "AUTHENTICATE_GIDNUMBER", 
                    apr_itoa(r->pool, pw->pw_gid));
    }
  }

  return OK;
}

static int orangefs_s3_post_config(apr_pool_t *pconf, apr_pool_t *plog, 
                                   apr_pool_t *ptemp, server_rec *s) 
{
  void *data = NULL;
  orangefs_s3_config *conf = 
    (orangefs_s3_config *)ap_get_module_config(s->module_config, 
                                               &orangefs_s3_module);
  const char *userdata_key = "orangefs_s3_post_config";
  int fsid;
  int rc;
  /*
    in the blob of STANDARD20_MODULE_STUFF in this module's data
    structure, there's a name field which is set to the name
    of the c file our source code is in: "mod_xyz.c"... we're
    going to scrape the module's name (xyz) out of the name field.

    If there is more than one OrangeFS related LoadModule directive
    in httpd.conf, a server level directive (PVFSInit) can be
    set to a module's name - only the module named on the PVFSInit
    directive should run PVFS_util_init_defaults. The LoadModule directive
    of the module named by the PVFSInit directive should come before
    LoadModule directives for any other OrangeFS related modules.
  */
  int nameLen=strlen(orangefs_s3_module.name);
  int fragLen=nameLen-strlen("mod_")-strlen(".c");
  char *name=apr_pcalloc(pconf,nameLen+strlen("_module"));

  if (nameLen > 6 &&
      !strncmp(orangefs_s3_module.name,"mod_",4) &&
      !strncmp((orangefs_s3_module.name)+nameLen-2,".c",2)) {
    strcpy(name,(orangefs_s3_module.name)+4);
    name[fragLen]='\0';
    strcat(name,"_module");
  } else {
    strcpy(name,"off");
  }

  if (debug_orangefs_s3) {
    if (conf->PVFSInit) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, 
        "orangefs_s3_post_config:%s:%s:",conf->PVFSInit,name);
    } else {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "orangefs_s3_post_config:NO PVFSINIT:");
    }
  }

  /* append version info for this module */
  ap_add_version_component(pconf, PVFS2_VERSION);

  /* check to see if we need to run PVFS_util_init_defaults()... */
  if (conf->PVFSInit) {
    if (!strcasecmp(conf->PVFSInit,name) ||
        !strcasecmp(conf->PVFSInit,"on")) {
ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"orangefs_s3_post_config:RUN INIT:");
      if (rc = PVFS_util_init_defaults()) {
        ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
          "orangefs_s3_post_config:PVFS_util_init_defaults "
          "returned:%d:, pid:%d:, PVFSInit:%s:",
          rc,getpid(),conf->PVFSInit);
      }
    }
  }

  apr_pool_userdata_get(&data, userdata_key, s->process->pool);
  if (data == NULL) {
    apr_pool_userdata_set((const void *)1, userdata_key,
                          apr_pool_cleanup_null, s->process->pool);
    return OK;
  }

  return OK;
}

static int orangefs_s3_open_logs(apr_pool_t *pconf, apr_pool_t *plog, 
                                 apr_pool_t *ptemp, server_rec *s)
{

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, "orangefs_s3_open_logs:");
  }

  return OK;
}

static void orangefs_s3_child_init(apr_pool_t *p, server_rec *s)
{

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, "orangefs_s3_child_init:");
  }

}

static void* orangefs_s3_create_srv_config(apr_pool_t *pool, server_rec *srv) 
{
  orangefs_s3_config *ret;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, "orangefs_s3_create_srv_config:");
  }

  /* initialize libxml2 and check for AFB compatibility */
  LIBXML_TEST_VERSION;

  ret = apr_pcalloc(pool, sizeof(orangefs_s3_config));

  ret->bucket_root = NULL;
  ret->awsAccounts = NULL;
  ret->PVFSInit = apr_pstrdup(pool,ON);

  return (void *)ret;
}

static void *orangefs_s3_merge_srv_config(apr_pool_t *p, 
                                          void *base_conf, 
                                          void *new_conf)
{

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, "orangefs_s3_merge_srv_config:");
  }

  orangefs_s3_config *b = (orangefs_s3_config *)base_conf;
  orangefs_s3_config *v = (orangefs_s3_config *)new_conf;

  if (b->bucket_root == NULL) {
    b->bucket_root = v->bucket_root; 
  }

  if (b->awsAccounts == NULL) {
    b->awsAccounts = v->awsAccounts; 
  }

  b->PVFSInit = (b->PVFSInit == v->PVFSInit) ? 
                     b->PVFSInit :
                     v->PVFSInit;

  return b;
}

static const char* orangefs_s3_setBucketRoot(cmd_parms *cmd, void *cfg,
                                             const char *bucket_root)
{
  server_rec *s = cmd->server;
  orangefs_s3_config *conf = 
    (orangefs_s3_config *)ap_get_module_config(s->module_config, 
                                               &orangefs_s3_module);

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, "orangefs_s3_setBucketRoot:");
  }

  conf->bucket_root = apr_pstrdup(cmd->pool, bucket_root);

  return NULL;
}

static const char *set_PVFSInit(cmd_parms *cmd,
                                   void *config,
                                   const char *arg)
{
  orangefs_s3_config *conf = config;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"set_PVFSInit: %s",arg);
  }

  conf = ap_get_module_config(cmd->server->module_config,&orangefs_s3_module);

  conf->PVFSInit = (char *)arg;

  return NULL;
}

static const char* orangefs_s3_setOwnerID(cmd_parms *cmd, void *cfg,
                                          const char *ownerID)
{
  server_rec *s = cmd->server;
  orangefs_s3_config *conf = 
    (orangefs_s3_config *)ap_get_module_config(s->module_config, 
                                               &orangefs_s3_module);

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, "orangefs_s3_setOwnerID:");
  }

  conf->ownerID = apr_pstrdup(cmd->pool, ownerID);

  return NULL;
}

static const char* orangefs_s3_setDisplayName(cmd_parms *cmd, void *cfg,
                                              const char *displayName)
{
  server_rec *s = cmd->server;
  orangefs_s3_config *conf = 
    (orangefs_s3_config *)ap_get_module_config(s->module_config, 
                                               &orangefs_s3_module);

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, "orangefs_s3_setDisplayName:");
  }

  conf->displayName = apr_pstrdup(cmd->pool, displayName);

  return NULL;
}

static const char* orangefs_s3_addAWSAccount(cmd_parms *cmd, void *cfg,
			    const char *args)
{
  server_rec *s = cmd->server;
  orangefs_s3_config *conf = 
    (orangefs_s3_config *)ap_get_module_config(s->module_config, 
                                               &orangefs_s3_module);
  const char *ptr;
  orangefs_s3_aws_account *account;

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, "orangefs_s3_addAWSAccount:");
  }

  if (conf->awsAccounts == NULL)
    conf->awsAccounts = apr_hash_make(cmd->pool);

  account = apr_pcalloc(cmd->pool, sizeof(orangefs_s3_aws_account));

  account->access_id = 
    apr_pstrdup(cmd->pool, ap_getword_conf(cmd->pool, &args));
  account->secret_key = 
    apr_pstrdup(cmd->pool, ap_getword_conf(cmd->pool, &args));
  account->uid = NULL;
  account->gid = NULL;

  ptr = ap_getword_conf(cmd->pool, &args);
  if (ptr && strcmp(ptr, "") != 0) {
    account->uid = apr_pstrdup(cmd->pool, ptr);
  }

  ptr = ap_getword_conf(cmd->pool, &args);
  if (ptr && strcmp(ptr, "") != 0) {
    account->gid = apr_pstrdup(cmd->pool, ptr);
  }

  apr_hash_set(conf->awsAccounts, account->access_id, 
               APR_HASH_KEY_STRING, account);

  return NULL;
}

static const char* orangefs_s3_setTraceOn(cmd_parms *cmd, void *cfg)
{

  if (debug_orangefs_s3) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL, "orangefs_s3_setTraceOn:");
  }

  return NULL;
}

static void orangefs_s3_register_hooks(apr_pool_t *pool)
{
  ap_hook_check_user_id(orangefs_s3_authenticate_aws_user, 
                        NULL, NULL, APR_HOOK_MIDDLE);
  ap_hook_post_config(orangefs_s3_post_config, 
                      NULL, NULL, APR_HOOK_REALLY_FIRST);
  ap_hook_open_logs(orangefs_s3_open_logs, NULL, NULL, APR_HOOK_MIDDLE);
  ap_hook_handler(orangefs_s3_handler, NULL, NULL, APR_HOOK_MIDDLE);
  ap_hook_child_init(orangefs_s3_child_init, NULL, NULL, APR_HOOK_REALLY_FIRST);
}

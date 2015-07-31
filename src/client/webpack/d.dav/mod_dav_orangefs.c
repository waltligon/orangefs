/*
  To do list...

  - dav_orangefs_seek_stream - not yet implemented

  - map pvfs return codes to http return codes...

*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* pam enablement */
#include <security/pam_appl.h>
#include <syslog.h>

/* apache auth */
#include <mod_auth.h>

#include <mod_dav.h>
#include <http_config.h>
#include <http_log.h>
#include <http_core.h>
#include <apr_strings.h>
#include <apr_uuid.h>

#include <pvfs2.h>
#include <pvfs2-util.h>
#include <pvfs2-types.h>

/* getpwnam needs these... */
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

/* pam enablement */
static int auth_conv();
struct pam_conv pc={auth_conv,NULL};
pam_handle_t *ph;
char pass[100];

#define DEBUG_ORANGEFS_TRIGGER "/etc/orangeFSdebugTrigger"
#define DEBUG_ORANGEFS access(DEBUG_ORANGEFS_TRIGGER, F_OK) == 0
#define ORANGE_GET_HANDLER 1
/* PUTBUFSIZE = default PVFS_Request_contiguous Buffer size argument 
                when setting up PVFS_sys_write for PUT. If you change
                the value of PUTBUFSIZE, find the TAKE1 comment
                below and change it too...
 */
#define PUTBUFSIZE 1048576
/* READBUFSIZE = default PVFS_Request_contiguous Buffer size argument 
                 when setting up PVFS_sys_read for GET or COPY. If you 
                 change the value of READBUFSIZE, find the TAKE1 comment
                 below and change it too...
 */
#define READBUFSIZE 1048576
#define KEYBUFSIZ 256
/* these are reserved property names, code in dav_orangefs_propdb_store
   won't let you set them with PROPPATCH. */
#define DAVLOCK_PROPERTY "orangefs_lock"
#define LOCKNULL_PROPERTY "orangefs_locknull"
/* end reserved property name list. */

#define ON "on"
#define OFF "off"

#define NOBODY 65534

/* Configurable options that could be placed in httpd.conf would
   be rooted in server_conf or dir_conf...
*/
typedef struct {
  const char *PVFSInit;
  const char *uid;
  const char *gid;
  const char *perm;
  const char *certpath;
} dav_orangefs_server_conf;

typedef struct dav_orangefs_dir_conf {
  int putBufSize;
  int readBufSize;
} dav_orangefs_dir_conf;

/* list of properties associated with a resource */
struct allPNs {
  char *propertyName;
  struct allPNs *next;
};

/* ... if any error occurs during processing [of a PROPPATCH], all 
   executed instructions MUST be undone and a proper error result 
   returned. RFC4918 9.2

   this repository's version of the dav_deadprop_rollback opaque handle
   will help put stuff back like it was, or delete it altogether, if needed...
*/
struct dav_deadprop_rollback {
  char *propertyName;
  char *propertyValue;
  int new;
};

/*  list associating namespaces with their prefixes. */
struct prefixMap {
  int prefixNumber;
  char *mapsTo;
  struct prefixMap *next;
};

/* this repository's version of the dav_resource_private opaque handle... */
struct dav_resource_private {
  apr_finfo_t orangefs_finfo;
  PVFS_permissions perms;
  PVFS_uid uid;
  PVFS_gid gid;
  int removeWalk;
  int copyWalk;
  int locknull;
  char *mountPoint;
  char *Uri;
  char *DirName;
  char *BaseName;
  struct allPNs *this;
  request_rec *r;
  PVFS_object_ref *ref;
  PVFS_object_ref *parent_ref;
  PVFS_object_ref *recurse_ref;
  PVFS_credential *credential;
};

/* this repository's version of the dav_stream opaque handle... */
struct dav_stream {
  PVFS_object_ref *ref;
  int64_t offset;
  void *writeBuffer;
  int writeBufLen;
  void *overflowBuffer;
  int overflowBufLen;
  int putBufSize;
  void *tempBuf;
  apr_pool_t *pool;
  PVFS_credential *credential;
};

/* this repository's version of the dav_locktoken opaque handle...
   stolen from lock/locks.c 
*/
struct dav_locktoken {
    apr_uuid_t uuid;
};

/* this repository's version of the dav_db opaque handle... */
struct dav_db {
  char *file;
  apr_pool_t *pool;
  char *mountPoint;
  PVFS_credential *credential;
  const apr_array_header_t *namespaces;
  struct prefixMap *pm;
  struct allPNs *thisPN;
};

extern module AP_MODULE_DECLARE_DATA dav_orangefs_module;
static dav_error *
   dav_orangefs_walk(const dav_walk_params *, int, dav_response **);
static dav_error *dav_orangefs_delete_walker(dav_walk_resource *, int);
static dav_error *dav_orangefs_copy_walker(dav_walk_resource *, int);
const char *dav_orangefs_getetag(const dav_resource *);
void first_next_helper(struct allPNs *, dav_prop_name *, dav_db *);
int getLockHelper(dav_lock **, char *, const dav_resource *);
void lockStringToParts(char *, int, char **, time_t *, int *, char **);
static int are_they_the_same(const dav_resource *, const dav_resource *);
void dirnameBasename(char *, char *, char *);
int orangeRead(PVFS_object_ref *, PVFS_credential *, char *, 
               void *, int64_t, int64_t);
int orangeWrite(const void *, apr_size_t, apr_pool_t *, 
                PVFS_object_ref *, int64_t, PVFS_credential *); 
static dav_error *
  orangeRemove(char *,PVFS_object_ref,PVFS_credential *,apr_pool_t *);
int orangeMkdir(dav_resource *);
static dav_error *orangeCopy(const dav_resource *, dav_resource *);
void orangePropCopy(const dav_resource *, dav_resource *);
int orangeCreate(dav_resource *,PVFS_sysresp_create *);
int credInit(PVFS_credential **new, apr_pool_t *p, const char *certpath,
             const char *username, uid_t uid, gid_t gid);
void credCopy(PVFS_credential *, PVFS_credential **, apr_pool_t *);

static const dav_hooks_locks dav_hooks_locks_orangefs;
static const dav_hooks_repository dav_hooks_repository_orangefs;
static const dav_hooks_propdb dav_hooks_properties_orangefs;
static const dav_hooks_liveprop dav_hooks_liveprop_orangefs;

/* global debug flag. We'll set this in get_resource and that way we
   won't have to run access(2) on DEBUG_ORANGEFS_TRIGGER over and over
   again to find out if we're in debug mode...
*/
int debug_orangefs=0;

static const dav_provider dav_orangefs_provider =
{
  &dav_hooks_repository_orangefs,    /* mandatory hook for all providers */
  &dav_hooks_properties_orangefs,    /* mandatory hook for all providers */
  &dav_hooks_locks_orangefs,         /* optional locking hooks  */
  NULL,            /* versioning hooks                                   */
  NULL,            /* binding hooks                                      */
  NULL,            /* dasl hooks                                         */
  NULL             /* context                                            */
};

/* evaluate readability of resource */
int canRead(int fowner, int fgroup, int fperms, PVFS_credential *creds) {
  int i;
  if (fowner == creds->userid) return((fperms&0400)>>8);
  for (i = 0; i < creds->num_groups; i++)
    if (fgroup == creds->group_array[i]) return((fperms&0040)>>5);
  return((fperms&0004)>>2);
}

/* evaluate writablilty of resource */
int canWrite(int fowner, int fgroup, int fperms, PVFS_credential *creds) {
  int i;
  if (fowner == creds->userid) return((fperms&0200)>>7);
  for (i = 0; i < creds->num_groups; i++)
    if (fgroup == creds->group_array[i]) return((fperms&0020)>>4);
  return((fperms&0002)>>1);
}

static dav_error *dav_orangefs_get_resource(request_rec *r,
                                            const char *root_dir,
                                            const char *label,
                                            int use_checked_in,
                                            dav_resource **result_resource)
{
  dav_resource *resource;
  dav_resource_private *orangefsInfo;
  char *thisResource;
  int thisResourceHasTrailingSlash=0;
  char *redirectionURI;
  int rc;
  dav_orangefs_server_conf *conf;

  if (DEBUG_ORANGEFS) {
    DBG3("dav_orangefs_get_resource: %s, request:%s: pid:%d:",
         r->unparsed_uri,r->the_request,getpid()); 
    debug_orangefs=1;
  }

  /* Storage for info for this resource. */
  orangefsInfo = apr_pcalloc(r->pool,sizeof(*orangefsInfo));

  /* initialize resource's permission attributes to nobody, nobody and
     nothing. We're going to set them to the right thing when we look
     it up, but something as important as permission attributes 
     probably ought to have an "on purpose" initialization value.
     See dav_orangefs_create_server_config() and set_DAVpvfsDefaultPerms()
     to understand where the default permission attributes come from.
  */

  conf = 
   (dav_orangefs_server_conf *)ap_get_module_config(r->server->module_config,
                                                    &dav_orangefs_module);
  orangefsInfo->uid=atoi(conf->uid);
  orangefsInfo->gid=atoi(conf->gid);
  orangefsInfo->perms=atoi(conf->perm);

ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
"dav_orangefs_get_resource: uid:%s: gid:%s: perm:%s:",conf->uid,conf->gid,
conf->perm);

  /* request_rec is defined in httpd.h. 
     dav_resource is defined in main/mod_dav.h
     dav_orangefs_get_resource's purpose is to fill in result_resource.
  */
  resource = apr_pcalloc(r->pool,sizeof(*resource));
  resource->type = DAV_RESOURCE_TYPE_REGULAR;
  resource->hooks = &dav_hooks_repository_orangefs;
  resource->pool = r->pool;
  resource->info = orangefsInfo;

  orangefsInfo->r = r;
  orangefsInfo->mountPoint = apr_pstrdup(r->pool,root_dir);

  thisResource = apr_pstrdup(r->pool,r->unparsed_uri);

  /* A Request-URI that specifies an extant directory should end in a slash.
     modules/mappers/mod_dir.c redirects ones that don't, so we will too.
     We'll nuke trailing slashes, but remember if they were there... 
     (RFC4918 8.3 URL Handling) mumbles a little bit about this.
  */
  if (thisResource[strlen(thisResource)-1] == '/') {
    thisResourceHasTrailingSlash=1;
    thisResource[strlen(thisResource)-1] = '\0';
  }

  /* Obtain resource's orangefs info... it is not an error condition 
     if the resource is not found... */
  rc=orangeAttrs("stat",thisResource,r->pool,orangefsInfo,NULL,NULL);
  if (rc) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "dav_orangefs_get_resource: could not stat %s",thisResource);
  }

  if (debug_orangefs) {
    DBG3("dav_orangefs_get_resource: uid:%d:  gid:%d:  locknull:%d:",
         orangefsInfo->credential->userid,
         orangefsInfo->credential->group_array[0],
         orangefsInfo->locknull);
    DBG3("dav_orangefs_get_resource: fuid:%d:  fgid:%d:  fperms:%o:",
         orangefsInfo->uid,orangefsInfo->gid,orangefsInfo->perms);

  }

  switch(orangefsInfo->orangefs_finfo.filetype) {

    case APR_NOFILE:
      resource->exists=0;
      break;

    /* about "collection"...
               ...
       **     collection = ? (1 if collection)
               ...
           int collection;     /* 0 => file;...

       the above excerpts are from near the typedef of dav_resource in
       main/mod_dav.h ...
    */

    case APR_REG:
      resource->exists=1;
      resource->collection=0;
      break;

    case APR_DIR:
      resource->exists=1;
      resource->collection=1;
      r->finfo.filetype=APR_DIR;
      r->filename = apr_pstrcat(r->pool,r->filename,"/",NULL);
      break;

    default:
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
                   "dav_orangefs_get_resource: can't get here");
      /* dav_new_error was randomly changed to have a new ap_status_t.
       * The mailing list post is from 2002, the actual change happened in
       * 2009. We will support both.
       * http://mail-archives.apache.org/mod_mbox/httpd-dev/200211.mbox/%3C20021101033848.B29006@lyra.org%3E */
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(r->pool,HTTP_BAD_REQUEST,0,
        "dav_orangefs_get_resource: unsupported resource type");
#else
      return dav_new_error(r->pool,HTTP_BAD_REQUEST,0,0,
        "dav_orangefs_get_resource: unsupported resource type");
#endif
  }

  if ((resource->exists) && 
      (resource->collection == 1) && 
      (!thisResourceHasTrailingSlash)) {
    /* Extant resource, is dir, has no slash, redirect... */
    redirectionURI = apr_pstrcat(r->pool,thisResource,"/",NULL);
    apr_table_setn(r->headers_out,"Location",
                   ap_construct_url(r->pool,redirectionURI,r));
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(r->pool,HTTP_MOVED_PERMANENTLY,0,
      "dav_orangefs_get_resource: Request-URI is dir, has no trailing slash");
#else
    return dav_new_error(r->pool,HTTP_MOVED_PERMANENTLY,0,0,
      "dav_orangefs_get_resource: Request-URI is dir, has no trailing slash");
#endif
  }

  resource->uri = thisResource;

  /* if we're doing a PUT and thisResource has the LOCKNULL_PROPERTY,
     we need to set up the resource_rec and the resource so it will
     do right in dav_get_resource_state. Check out the comments in 
     dav_get_resource_state (main/util_lock.c)...
  */
  if (!strncmp(r->the_request,"PUT",3) &&
       orangefsInfo->locknull) {
    *r->path_info = '\0';
    resource->exists=0;
  }

  *result_resource = resource;
  return NULL;
}

static dav_error * dav_orangefs_get_parent_resource(
                     const dav_resource *resource,
                     dav_resource **result_parent)
{
  dav_resource_private *parent_drp;
  dav_resource *parent_resource;
  int rc;
  int i;

  if (debug_orangefs) {
      DBG2("dav_orangefs_get_parent_resource:%s: pid:%d:",
           resource->uri,getpid());
  }

  if (!strcmp(resource->uri,resource->info->mountPoint)) {
    *result_parent = NULL;
    return NULL;
  }


  /* Orangefs is a network filesystem, it is mounted somewhere,
     if there's no DirName, something's busted...
  */
  if (!resource->info->DirName) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
        return dav_new_error(resource->pool,HTTP_CONFLICT,0,
                 apr_psprintf(resource->pool,
                 "dav_orangefs_get_parent_resource: no DirName for :%s: ???",
                 resource->uri));
#else
        return dav_new_error(resource->pool,HTTP_CONFLICT,0,0,
                 apr_psprintf(resource->pool,
                 "dav_orangefs_get_parent_resource: no DirName for :%s: ???",
                 resource->uri));
#endif
  }


  /* Create private resource context descriptor */
  parent_drp = apr_pcalloc(resource->pool, sizeof(*parent_drp));
  parent_drp->mountPoint =
    apr_pstrdup(resource->pool,resource->info->mountPoint);

  /* we should have a credential, stick it in the parent drp and
     it won't have to be generated when orangeAttrs is called...
  */
  credCopy(resource->info->credential, 
           &(parent_drp->credential), 
           resource->pool);
  parent_resource = apr_pcalloc(resource->pool, sizeof(*parent_resource));
  parent_resource->type = DAV_RESOURCE_TYPE_REGULAR;
  parent_resource->hooks = &dav_hooks_repository_orangefs;
  parent_resource->pool = resource->pool;
  parent_resource->info = parent_drp;
  parent_resource->collection = 1;
  parent_resource->exists = 1;
  parent_resource->uri = apr_pcalloc(resource->pool,1);
  parent_resource->uri = apr_pstrcat(resource->pool,
                                     resource->info->DirName,
                                     NULL);

  if (rc = orangeAttrs("stat",resource->info->DirName,resource->pool,
                   parent_drp,NULL,NULL)) {
    /* kind of funny to be unable to resolve someone's parent...
       flush the name cache and try again...
    */
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "dav_orangefs_get_parent_resource: flushing name cache...");
    PINT_ncache_finalize();
    PINT_ncache_initialize();
    rc = orangeAttrs("stat",resource->info->DirName,resource->pool,
                      parent_drp,NULL,NULL);
  }

  if (rc) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "dav_orangefs_get_parent_resource: "
      "can't stat %s, %d",resource->info->DirName,rc);

    
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(resource->pool,HTTP_NOT_FOUND,0,NULL);
#else
    return dav_new_error(resource->pool,HTTP_NOT_FOUND,0,0,NULL);
#endif
  }

  *result_parent = parent_resource;
  return NULL;
}

static int dav_orangefs_is_parent_resource(const dav_resource *res1,
                                           const dav_resource *res2)
{

  if (debug_orangefs) {
    DBG2("dav_orangefs_is_parent_resource: :%s: :%s:",res1->uri, res2->uri);
  }

  return are_they_the_same(res1,res2);
}

static int dav_orangefs_is_same_resource(const dav_resource *res1,
                                         const dav_resource *res2)
{

  if (debug_orangefs) {
    DBG2("dav_orangefs_is_same_resource: :%s: :%s:",res1->uri, res2->uri);
  }

  return are_they_the_same(res1,res2);
}

static dav_error * dav_orangefs_open_stream(const dav_resource *resource,
                                            dav_stream_mode mode,
                                            dav_stream **stream)
{
  PVFS_sysresp_create *resp_create;
  int rc;
  int exists=resource->exists;
  dav_orangefs_dir_conf *dconf;

  if (debug_orangefs) {
      DBG2("dav_orangefs_open_stream:%s: pid:%d:",resource->uri,getpid());
  }

  if (!canRead(resource->info->uid,resource->info->gid,resource->info->perms,
               resource->info->credential)){
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(resource->pool,HTTP_FORBIDDEN,0,
      "dav_orangefs_open_stream: permission denied");
#else
    return dav_new_error(resource->pool,HTTP_FORBIDDEN,0,0,
      "dav_orangefs_open_stream: permission denied");
#endif
  }

  dconf = ap_get_module_config(resource->info->r->per_dir_config, 
                               &dav_orangefs_module);

  switch (mode) {

    case DAV_MODE_WRITE_TRUNC:

      *stream = apr_pcalloc(resource->pool,sizeof(**stream));
      (*stream)->ref = apr_pcalloc(resource->pool,sizeof(PVFS_object_ref));
      (*stream)->ref->handle = resource->info->ref->handle;
      (*stream)->ref->fs_id = resource->info->ref->fs_id;
      (*stream)->offset = 0;
      (*stream)->writeBuffer = apr_pcalloc(resource->pool,dconf->putBufSize);
      (*stream)->writeBufLen = 0;
      (*stream)->overflowBuffer = 
        apr_pcalloc(resource->pool,dconf->putBufSize);
      (*stream)->overflowBufLen = 0;
      (*stream)->putBufSize = dconf->putBufSize;
      (*stream)->tempBuf = apr_pcalloc(resource->pool,dconf->putBufSize);
      (*stream)->pool = resource->pool;
      (*stream)->credential = resource->info->credential;

      /* if the resource is a locknull resource, we need to reset
         the locknull flags, because the resource is about to become
         real...
       */
      if (resource->info->locknull) {
        if (orangeAttrs("remove",resource->uri,resource->pool,
                         resource->info,LOCKNULL_PROPERTY,NULL)) {
           ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
             "dav_orangefs_open_stream: unable to remove "
             "LOCKNULL_PROPERTY on %s",resource->uri);
        }
        exists = 1;
      }

      /* if the resource doesn't exist yet, then what we have is 
         a ref for the parent. Use the parent ref to create the
         file, and then hang onto the file's ref.
      */
      if (!exists) {

        resp_create = apr_pcalloc(resource->pool,sizeof(*resp_create));
    
        if (rc=orangeCreate((dav_resource *)resource,resp_create)) {

          ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"dav_orangefs_open_stream: "
                       "PVFS_sys_create failed on :%s:, rc:%d:",
                       resource->uri,rc);
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
          return dav_new_error(resource->pool,HTTP_INTERNAL_SERVER_ERROR,0,
                               "dav_orangefs_open_stream: "
                               "PVFS_sys_create failed");
#else
          return dav_new_error(resource->pool,HTTP_INTERNAL_SERVER_ERROR,0,0,
                               "dav_orangefs_open_stream: "
                               "PVFS_sys_create failed");
#endif
        }

        (*stream)->ref->handle = resp_create->ref.handle;
        (*stream)->ref->fs_id = resp_create->ref.fs_id;
      }

      break;

    case DAV_MODE_WRITE_SEEKABLE:
      DBG0("dav_orangefs_open_stream: mode is DAV_MODE_WRITE_SEEKABLE");
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(resource->pool,HTTP_INTERNAL_SERVER_ERROR,0,
                           "dav_orangefs_open_stream: "
                           "DAV_MODE_WRITE_SEEKABLE unsupported.");
#else
      return dav_new_error(resource->pool,HTTP_INTERNAL_SERVER_ERROR,0,0,
                           "dav_orangefs_open_stream: "
                           "DAV_MODE_WRITE_SEEKABLE unsupported.");
#endif
      break;

    default:
      DBG1("dav_orangefs_open_stream: mode is default: %d",mode);
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(resource->pool,HTTP_INTERNAL_SERVER_ERROR,0,
                           "dav_orangefs_open_stream: "
                           "default mode unsupported.");
#else
      return dav_new_error(resource->pool,HTTP_INTERNAL_SERVER_ERROR,0,0,
                           "dav_orangefs_open_stream: "
                           "default mode unsupported.");
#endif
      break;

  }

  return NULL;
}

static dav_error * dav_orangefs_close_stream(dav_stream *stream, int commit)
{
  if (debug_orangefs) {
      DBG1("dav_orangefs_close_stream: commit:%d:",commit);
  } 


  if ((stream->writeBufLen != 0) && (stream->overflowBufLen != 0)) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(stream->pool,HTTP_INTERNAL_SERVER_ERROR,0,
                         "dav_orangefs_close_stream: Both the overflow buffer "
                         "and the write buffer contained bytes!");
#else
    return dav_new_error(stream->pool,HTTP_INTERNAL_SERVER_ERROR,0,0,
                         "dav_orangefs_close_stream: Both the overflow buffer "
                         "and the write buffer contained bytes!");
#endif
  } else if (stream->overflowBufLen != 0) {
    if (orangeWrite(stream->overflowBuffer,
                    stream->overflowBufLen,
                    stream->pool,stream->ref,
                    stream->offset,stream->credential) < 0)
    {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(stream->pool,HTTP_INTERNAL_SERVER_ERROR,0,
                           "dav_orangefs_close_stream: overflow: "
                           "An error occured while writing to a resource.");
#else
      return dav_new_error(stream->pool,HTTP_INTERNAL_SERVER_ERROR,0,0,
                           "dav_orangefs_close_stream: overflow: "
                           "An error occured while writing to a resource.");
#endif
    }
  } else if (stream->writeBufLen != 0) {
    if (orangeWrite(stream->writeBuffer,
                    stream->writeBufLen,
                    stream->pool,stream->ref,
                    stream->offset,stream->credential) < 0)
    {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(stream->pool,HTTP_INTERNAL_SERVER_ERROR,0,
                           "dav_orangefs_close_stream: final bytes: "
                           "An error occured while writing to a resource.");
#else
      return dav_new_error(stream->pool,HTTP_INTERNAL_SERVER_ERROR,0,0,
                           "dav_orangefs_close_stream: final bytes: "
                           "An error occured while writing to a resource.");
#endif
    }
  }

  if (!commit) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(stream->pool,HTTP_INTERNAL_SERVER_ERROR,0,
                         "dav_orangefs_close_stream: don't know how "
                         "to roll back yet...");
#else
    return dav_new_error(stream->pool,HTTP_INTERNAL_SERVER_ERROR,0,0,
                         "dav_orangefs_close_stream: don't know how "
                         "to roll back yet...");
#endif
  }

  return NULL;
}

/* buf contains the bytes to write to the orangefs file, bufsize
   is the length (number of bytes) of buf... dav_stream is an
   opaque handle (typedef struct dav_stream dav_stream;) defined in 
   dav/main/mod_dav.h, it exists for us to stash whatever
   kind of special juju we might need for writing on our particular
   repository... 

   If the file to write is small enough, it is all in buf and we only
   show up here once. If the file is big enough that Chunked encoding
   kicks in, we show up here for each chunk.

   We'll buffer up the bytes that show up here. If we never overflow
   the buffer, we'll never even write from here, we'll just write out
   the buffer from close_stream. If we show up here enough to fill up 
   the buffer one or more times, we'll write out each filled buffer 
   from here, and deal with any unwritten bytes in close_stream.
*/
static dav_error * dav_orangefs_write_stream(dav_stream *stream,
                                             const void *buf, 
                                             apr_size_t bufsize)
{
  clock_t start,end;
  double cpu_time_used;
  int totalBytes, extraBytes, topOffBytes;

  if (debug_orangefs) {
    DBG1("dav_orangefs_write_stream: pid:%d:",getpid());
    start = clock();
  }

  if ((stream->writeBufLen != 0) && (stream->overflowBufLen != 0)) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(stream->pool,HTTP_INTERNAL_SERVER_ERROR,0,
                         "Both the overflow buffer and the write buffer "
                         "contained bytes in write_stream!");
#else
    return dav_new_error(stream->pool,HTTP_INTERNAL_SERVER_ERROR,0,0,
                         "Both the overflow buffer and the write buffer "
                         "contained bytes in write_stream!");
#endif
  } else if (stream->overflowBufLen != 0) {
    memset(stream->writeBuffer,0,stream->putBufSize);
    memcpy(stream->writeBuffer,stream->overflowBuffer,stream->overflowBufLen);
    memset(stream->overflowBuffer,0,stream->putBufSize);
    stream->writeBufLen = stream->overflowBufLen; 
    stream->overflowBufLen = 0;
  }

  if (((stream->writeBufLen)+bufsize) <= (stream->putBufSize)) {
    memcpy((stream->writeBuffer)+(stream->writeBufLen),buf,bufsize);
    stream->writeBufLen += bufsize;
  } else {
    totalBytes = (stream->writeBufLen)+bufsize;
    extraBytes = totalBytes-(stream->putBufSize); 
    topOffBytes = bufsize-extraBytes;

    if ((stream->writeBufLen)+topOffBytes != (stream->putBufSize)) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(stream->pool,HTTP_INTERNAL_SERVER_ERROR,0,
                           apr_psprintf(stream->pool,
                           "buffer calculations in write_stream have "
                           "gotten all cattywhomppered! totalBytes:%d: "
                           "extraBytes:%d: topOffBytes:%d: putBufSize:%d:",
                           totalBytes,extraBytes,topOffBytes,
                           (stream->putBufSize)));
#else
      return dav_new_error(stream->pool,HTTP_INTERNAL_SERVER_ERROR,0,0,
                           apr_psprintf(stream->pool,
                           "buffer calculations in write_stream have "
                           "gotten all cattywhomppered! totalBytes:%d: "
                           "extraBytes:%d: topOffBytes:%d: putBufSize:%d:",
                           totalBytes,extraBytes,topOffBytes,
                           (stream->putBufSize)));
#endif
    }

    memcpy((stream->writeBuffer)+stream->writeBufLen,buf,topOffBytes);
    stream->writeBufLen = stream->putBufSize;

    memcpy(stream->overflowBuffer,buf+topOffBytes,extraBytes);
    stream->overflowBufLen = extraBytes;
    
  }

  if (stream->writeBufLen == (stream->putBufSize)) {
    if (orangeWrite(stream->writeBuffer,stream->putBufSize,
                    stream->pool,stream->ref,
                    stream->offset,stream->credential) < 0)
    {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(stream->pool,HTTP_INTERNAL_SERVER_ERROR,0,
                           "An error occured while writing to a resource.");
#else
      return dav_new_error(stream->pool,HTTP_INTERNAL_SERVER_ERROR,0,0,
                           "An error occured while writing to a resource.");
#endif
    }
    memset(stream->writeBuffer,0,stream->putBufSize);
    stream->writeBufLen = 0;

    stream->offset+=(stream->putBufSize);
  }

  if (debug_orangefs) {
    end = clock();
    DBG2("dav_orangefs_write_stream: cpu_time_used:%f: bufsize:%d:",
        ((double)(end-start))/CLOCKS_PER_SEC,bufsize); 
    DBG3("dav_orangefs_write_stream: stream->writeBufLen:%d: "
         "stream->overflowBufLen:%d: bufsiz:%d:",stream->writeBufLen,
         stream->overflowBufLen, bufsize);
  }

  return NULL;
}

static dav_error *dav_orangefs_seek_stream(dav_stream *stream,apr_off_t abs_pos)
{
  if (debug_orangefs) {
      DBG0("dav_orangefs_seek_stream: ------------- unimplemented -------");
  }

  return NULL;
}

static dav_error *dav_orangefs_set_headers(request_rec *r,
                                           const dav_resource *resource)
{
  if (debug_orangefs) {
      DBG0("dav_orangefs_set_headers");
  }

  if (!resource->exists) {
    return NULL;
  }

  ap_update_mtime(r, resource->info->orangefs_finfo.mtime*1000000);

  ap_set_last_modified(r);
  
  apr_table_setn(r->headers_out,"ETag",dav_orangefs_getetag(resource));

  apr_table_setn(r->headers_out,"Accept-Ranges","bytes");

  /* we won't set a content length header. What with the way we
     implemented dav_orangefs_deliver, stuff about content length
     and whether to use Chunked Encoding will work out without us
     having to handle it...
  */

  /* What is the best way to decide how to set the content_type header? */
  if ((resource->info->orangefs_finfo.filetype == APR_DIR) ||
      (!strcmp((resource->uri)+strlen(resource->uri)-4,"html")) ||
      (!strcmp((resource->uri)+strlen(resource->uri)-3,"htm")))
  {
    r->content_type = "text/html";
  } else {
    r->content_type = "text/plain";
  }

  r->no_local_copy = 1;

  return NULL; 
}

/* orangefs specific GET handler... */
static dav_error *dav_orangefs_deliver(const dav_resource *resource,
                                       ap_filter_t *output)
{
  int rc=1;
  PVFS_Request mem_req;
  int64_t offset=0;
  PVFS_sysresp_io resp_io;
  void *buffer;
  int bytesRead;
  int pvfs_dirent_incount;
  PVFS_ds_position token;
  PVFS_sysresp_readdir resp_readdir;
  int max_dirents_returned=25;
  int i;
  char *title;
  dav_resource_private orangefsInfo;
  char *currentObject;
  char *isDir;
  char *isIndex;
  dav_orangefs_dir_conf *dconf;

  if (debug_orangefs) {
      DBG2("dav_orangefs_deliver: resource:%s: pid:%d:",
           resource->uri,getpid());
  }

  /* Since there's potential this routine might modify the data in the 
     private handle (see comments below) we'll make a local drp and
     partially fill it in with some stuff we can reuse from resource->info.
  */
  orangefsInfo.mountPoint =
    apr_pstrdup(resource->pool,resource->info->mountPoint);

  /* we should have a credential, stick it in the parent drp and
     it won't have to be generated when orangeAttrs is called...
  */
  credCopy(resource->info->credential, 
           &(orangefsInfo.credential), 
           resource->pool);

  /* you can follow through the code in Apache and in mod_dir to
     see what happens (in the APR world) when someone GETs a 
     directory: a kind of look-ahead has to happen so that the decision 
     to either emit "Index of" html or, if the directory has an 
     index.html file, process that.

     To oversimplify, what happens is that the request_rec is "fixed up" 
     so that it looks kind of like the GET was pointed at the 
     /dir/index.html, instead of at the /dir...

     A whole series of these "fixups" can happen for a lot of different
     reasons, but you can follow the one I'm describing with:

            fixup_dir modules/mappers/mod_dir.c
            ap_sub_req_lookup_uri server/request.c
            ap_sub_req_method_uri server/request.c

     For this repository, instead of messing with the request_rec, we'll 
     just look ahead to see if there is an index.html, and if so, swap 
     out the dir file-handle for the index.html file-handle. And we can use
     the return code from the index.html probe to channel us away from the
     "GET a directory" code into the "GET a file" code, if there is
     an index.html file...
   */
  if (resource->info->orangefs_finfo.filetype == APR_DIR) {
    isIndex = apr_pcalloc(resource->pool,1);
    isIndex = apr_pstrcat(resource->pool,
                          resource->uri,
                          "/index.html",
                          NULL);
    rc = orangeAttrs("stat",isIndex,resource->pool,&orangefsInfo,NULL,NULL);
    if (!rc) {
     resource->info->ref->handle = orangefsInfo.ref->handle;
     resource->info->ref->fs_id = orangefsInfo.ref->fs_id;
    }
  }
  
  if ((resource->collection == 1) && (rc != 0)) { 
    /* GET a directory. */

    /* emit some "Index of" boiler-plate stuff... */
    title=resource->info->r->parsed_uri.path;
    ap_rvputs(resource->info->r, DOCTYPE_HTML_3_2,
                  "<html>\n"
                  "  <head>\n"
                  "    <title>Index of ", title, "</title>\n", 
                  "  </head>\n"
                  "  <body>"
                  " <h1> Index of ", title, "</h1>", "\n  <ul>",
                  NULL);

    ap_rvputs(resource->info->r,
                "\n  <li> ",
                "<a href=\"..\"> Parent Directory</a></li>",
                NULL);

    pvfs_dirent_incount = max_dirents_returned;
    token=0;

    do {
  
  
      memset(&resp_readdir,0,sizeof(PVFS_sysresp_readdir));

      if (debug_orangefs) {
       ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
         "dav_orangefs_deliver: PVFS_sys_readdir: handle:%d: fs_id:%d: "
         "token:%d: pid:%d:",
         resource->info->ref->handle,resource->info->ref->fs_id,token,getpid());
      }

      /* get a list of this directory's contents... */
      if ((rc = PVFS_sys_readdir(
                  (PVFS_object_ref)*resource->info->ref,
                  (!token ? PVFS_READDIR_START : token),
                  pvfs_dirent_incount,
                  resource->info->credential,
                  &resp_readdir,
                  NULL)) < 0) {
        ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
          "dav_orangefs_deliver: PVFS_sys_readdir, rc:%d:\n",rc);
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
        return dav_new_error(resource->pool,HTTP_NOT_FOUND,0,NULL);
#else
        return dav_new_error(resource->pool,HTTP_NOT_FOUND,0,0,NULL);
#endif
      }
 
      for (i=0;i<resp_readdir.pvfs_dirent_outcount;i++) {
  
        /* figure out if the current object in the list is a 
           directory or not, so we can draw a slash next to directories
           in the "Index of" output...
         */
        currentObject = apr_pcalloc(resource->pool,1);
        currentObject = apr_pstrcat(resource->pool,currentObject,
                                    resource->uri,

                                    "/",
                                    resp_readdir.dirent_array[i].d_name,
                                    NULL);
  
        rc = orangeAttrs("stat",currentObject,
                         resource->pool,&orangefsInfo,NULL,NULL);
        if (rc) {
          ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
            "dav_orangefs_deliver: can't stat %s",currentObject);
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
          return dav_new_error(resource->pool,HTTP_NOT_FOUND,0,NULL);
#else
          return dav_new_error(resource->pool,HTTP_NOT_FOUND,0,0,NULL);
#endif
        }
        if (orangefsInfo.orangefs_finfo.filetype == APR_DIR) {
          isDir = "/";
        } else {
          isDir = " ";
        }
  
       /* print the next item in the Index list... */
        ap_rvputs(resource->info->r, 
                  "\n  <li> ",
                  "<a href=\"",resp_readdir.dirent_array[i].d_name,isDir,"\"> ",
                  resp_readdir.dirent_array[i].d_name,isDir,"</a></li>",
                  NULL);
      }
  
      token=resp_readdir.token;
  
      if (resp_readdir.pvfs_dirent_outcount < pvfs_dirent_incount) {
        break;
      }
      
      /* free blob of memory allocated by readdir... */
      if (resp_readdir.pvfs_dirent_outcount) {
        free(resp_readdir.dirent_array);
      }
  
    } while (resp_readdir.pvfs_dirent_outcount != 0);

    /* finish up "Index of" boiler-plate html... */
    ap_rvputs(resource->info->r,"\n  </ul>\n </body>\n</html>",NULL);

    return NULL;
  } else {
    /* GET a file */

    if (!canRead(resource->info->uid,resource->info->gid,resource->info->perms,
                 resource->info->credential)){
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(resource->pool,HTTP_FORBIDDEN,0,
        "dav_orangefs_deliver: permission denied");
#else
      return dav_new_error(resource->pool,HTTP_FORBIDDEN,0,0,
        "dav_orangefs_deliver: permission denied");
#endif
    }

    /* obtain read buffer size... */
    dconf = ap_get_module_config(resource->info->r->per_dir_config, 
                                 &dav_orangefs_module);

    buffer = apr_pcalloc(resource->pool,dconf->readBufSize);
    bytesRead = 0;

    /* priming read... */
    if ((bytesRead = 
           orangeRead(resource->info->ref,resource->info->credential,
                      (char *)resource->uri,
                      buffer,dconf->readBufSize,offset)) < 0 ) 
    {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(resource->pool,HTTP_NOT_FOUND,0,NULL);
#else
      return dav_new_error(resource->pool,HTTP_NOT_FOUND,0,0,NULL);
#endif
    }
  
    /* "while bytesread > 0" causes orangeRead to be visited 
       one extra time... how can we tell we're done when we get
       the last few byte of a file without making that one last
       visit to the empty well? Is it safe to say that if
       we don't fill up the buffer, then that was it?
     */
    while (bytesRead > 0) {
      offset+=bytesRead;
      ap_rwrite((const void *) buffer,bytesRead,resource->info->r); 

      memset(buffer,0,dconf->readBufSize);
      if ((bytesRead = 
             orangeRead(resource->info->ref,resource->info->credential,
                        (char *)resource->uri,
                        buffer,dconf->readBufSize,offset)) < 0 ) 
      {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
        return dav_new_error(resource->pool,HTTP_NOT_FOUND,0,NULL);
#else
        return dav_new_error(resource->pool,HTTP_NOT_FOUND,0,0,NULL);
#endif
      }
    }

  }

  return NULL;
}

static dav_error *dav_orangefs_create_collection(dav_resource *resource)
{
  int rc;

  if (debug_orangefs) {
    DBG2("dav_orangefs_create_collection:%s: pid:%d:",resource->uri,getpid());
  }

  if ((rc = orangeMkdir(resource))) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(resource->pool,HTTP_MULTI_STATUS,0,
                         apr_psprintf(resource->pool,
                         "dav_orangefs_create_collection: could not "
                         "create %s rc=%d",resource->uri,rc));
#else
    return dav_new_error(resource->pool,HTTP_MULTI_STATUS,0,0,
                         apr_psprintf(resource->pool,
                         "dav_orangefs_create_collection: could not "
                         "create %s rc=%d",resource->uri,rc));
#endif
  }

  return NULL;
}

/* make a copy of src at dst, including the dead properties... */
static dav_error *dav_orangefs_copy_resource(const dav_resource *src,
                                             dav_resource *dst,
                                             int depth,
                                             dav_response **response)
{
  int rc;
  dav_orangefs_dir_conf *dconf;
  int i;

  if (debug_orangefs) {
    DBG3("dav_orangefs_copy_resource: %s -> %s  pid:%d:",
         src->uri,dst->uri,getpid());
    DBG1("src->info->ref->handle:%d",src->info->ref->handle);
    DBG1("dst->info->ref->handle:%d",dst->info->ref->handle);
    if (src->info->parent_ref) {
      DBG1("src->info->parent_ref->handle:%d",src->info->parent_ref->handle);
    }
    if (dst->info->parent_ref) {
      DBG1("dst->info->parent_ref->handle:%d",dst->info->parent_ref->handle);
    }
  }

  /* If dst exists prior to execution of the COPY request, the DAV core
     will have called remove_resource and gotten rid of it by the time
     we get here. Since dst existed when the COPY request began to be
     processed, we found and remembered a ref for it, but since dst
     is gone at this point, the ref we have for it is bogus.

     We need to feed a proper parent ref for dst to PVFS_sys_create to 
     get the job done here.

     If dst existed before COPY was executed:

       - use dst->info->parent_ref...

     If dst did not exist before COPY was executed:  

       - use dst->info->ref...

     dst->info->parent_ref will be NULL if dst did not exist prior to
     COPY being called...

  */
  if (dst->info->parent_ref) {
    dst->info->ref = dst->info->parent_ref;
  }

  if (src->collection) {
    /* a collection/directory */
    dav_walk_params params = { 0 };
    dav_error *err = NULL;
    dav_response *multi_status;

    src->info->copyWalk = 1;

    params.func = dav_orangefs_copy_walker;
    params.pool = src->pool;
    params.root = src;
    params.walk_ctx = (void *)dst; 

    /* create the dir */
    if ((rc = orangeMkdir(dst))) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(dst->pool,HTTP_MULTI_STATUS,0,
                           apr_psprintf(dst->pool,
                           "dav_orangefs_copy_resource: could not "
                           "create %s rc=%d",dst->uri,rc));
#else
      return dav_new_error(dst->pool,HTTP_MULTI_STATUS,0,0,
                           apr_psprintf(dst->pool,
                           "dav_orangefs_copy_resource: could not "
                           "create %s rc=%d",dst->uri,rc));
#endif
    }

    /* get info for newly created dst... */
    dst->info = apr_pcalloc(dst->pool,sizeof(*dst->info));
    dst->info->mountPoint = apr_pstrdup(src->pool,src->info->mountPoint);

    credCopy(src->info->credential, &(dst->info->credential), src->pool);

    rc = orangeAttrs("stat",dst->uri,dst->pool,dst->info,
                 NULL,NULL);
    if (rc) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "dav_orangefs_copy_resource: can't stat %s",dst->uri);
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(dst->pool,HTTP_NOT_FOUND,0,NULL);
#else
      return dav_new_error(dst->pool,HTTP_NOT_FOUND,0,0,NULL);
#endif
    }


    /* fill the dir */
    if ((err = dav_orangefs_walk(&params,DAV_INFINITY,&multi_status)) != NULL){
      return err;
    }

    /* copy over the properties of the dir we made right before the walk... */
    orangePropCopy(src,dst);

  } else {
    /* a file */
    return orangeCopy(src,dst);
  }

  return NULL;

}

static dav_error *dav_orangefs_move_resource(
  dav_resource *src,
  dav_resource *dst,
  dav_response **response)
{
  PVFS_object_ref src_parent_ref;
  PVFS_object_ref dst_parent_ref;
  PVFS_fs_id src_lookup_fs_id;
  PVFS_fs_id dst_lookup_fs_id;
  char src_orange_path[PVFS_NAME_MAX];
  char dst_orange_path[PVFS_NAME_MAX];
  PVFS_sysresp_lookup src_resp_lookup;
  PVFS_sysresp_lookup dst_resp_lookup;
  int rc;

   if (debug_orangefs) {
     DBG1("dav_orangefs_move_resource: pid:%d:",getpid());
   }

   /* get the dst parent ref... */
   memset(&dst_orange_path,0,PVFS_NAME_MAX);
   if ((rc=PVFS_util_resolve(dst->info->DirName,&dst_lookup_fs_id,
                             dst_orange_path,PVFS_NAME_MAX))) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
     return dav_new_error(dst->info->r->pool,
                          HTTP_INTERNAL_SERVER_ERROR, 0,
                          apr_psprintf(dst->info->r->pool,"PVFS_util_resolve "
                          "failed for %s, rc:%d:",dst->info->DirName,rc));
#else
     return dav_new_error(dst->info->r->pool,
                          HTTP_INTERNAL_SERVER_ERROR, 0,0,
                          apr_psprintf(dst->info->r->pool,"PVFS_util_resolve "
                          "failed for %s, rc:%d:",dst->info->DirName,rc));
#endif

   }

   if (debug_orangefs) {
     ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
     "dav_orangefs_move_resource: PVFS_sys_lookup: fs_id:%d: "
     "path:%s: pid:%d:",
     dst_lookup_fs_id,dst_orange_path,getpid());
   }

   if ((rc=PVFS_sys_lookup(dst_lookup_fs_id,dst_orange_path,
                           dst->info->credential,&dst_resp_lookup,
                           PVFS2_LOOKUP_LINK_FOLLOW,PVFS_HINT_NULL))) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
     return dav_new_error(dst->info->r->pool,
                          HTTP_INTERNAL_SERVER_ERROR, 0,
                          apr_psprintf(dst->info->r->pool,"PVFS_sys_lookup "
                          "failed for %s, rc:%d:",dst->info->DirName,rc));
#else
     return dav_new_error(dst->info->r->pool,
                          HTTP_INTERNAL_SERVER_ERROR, 0,0,
                          apr_psprintf(dst->info->r->pool,"PVFS_sys_lookup "
                          "failed for %s, rc:%d:",dst->info->DirName,rc));
#endif
   }

   if (debug_orangefs) {
     ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
     "dav_orangefs_move_resource: PVFS_sys_rename: src.BaseName:%s: "
     "parent_ref.handle:%d:  dst.BaseName:%s:   dst.ref.handle:%d:  pid:%d:",
     src->info->BaseName,src->info->parent_ref->handle,dst->info->BaseName,
     dst_resp_lookup.ref.handle,getpid());
   }

  if (!canWrite(src->info->uid,src->info->gid,src->info->perms,
                src->info->credential)){
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(src->info->r->pool,HTTP_FORBIDDEN,0,
      "dav_orangefs_move_resource: permission denied");
#else
    return dav_new_error(src->info->r->pool,HTTP_FORBIDDEN,0,0,
      "dav_orangefs_move_resource: permission denied");
#endif
  }

   /* move the resource... */
   if ((rc = PVFS_sys_rename(src->info->BaseName,*src->info->parent_ref,
                             dst->info->BaseName,dst_resp_lookup.ref,
                             dst->info->credential,PVFS_HINT_NULL))) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
     return dav_new_error(dst->info->r->pool,
                          HTTP_INTERNAL_SERVER_ERROR, 0,
                          apr_psprintf(dst->info->r->pool,"PVFS_sys_rename "
                          "failed src:%s: dst:%s: rc:%d:",
                          src->info->Uri,dst->info->Uri,rc));
#else
     return dav_new_error(dst->info->r->pool,
                          HTTP_INTERNAL_SERVER_ERROR, 0,0,
                          apr_psprintf(dst->info->r->pool,"PVFS_sys_rename "
                          "failed src:%s: dst:%s: rc:%d:",
                          src->info->Uri,dst->info->Uri,rc));
#endif
   }

   /* no multistatus response... */
   *response = NULL;

   return NULL;

}

static dav_error *dav_orangefs_remove_resource(dav_resource *resource,
                                               dav_response **response)
{
  int rc;

  if (debug_orangefs) {
    DBG2("dav_orangefs_remove_resource:%s: pid:%d:", resource->uri,getpid());
  }

  if (!canWrite(resource->info->uid,resource->info->gid,resource->info->perms,
                resource->info->credential)){
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(resource->pool,HTTP_FORBIDDEN,0,
      "dav_orangefs_remove_resource: permission denied");
#else
    return dav_new_error(resource->pool,HTTP_FORBIDDEN,0,0,
      "dav_orangefs_remove_resource: permission denied");
#endif
  }

  if (resource->collection) {
    /* a collection/directory */
    dav_walk_params params = { 0 };
    dav_error *err = NULL;
    dav_response *multi_status;

    resource->info->removeWalk = 1;

    params.func = dav_orangefs_delete_walker;
    params.pool = resource->pool;
    params.root = resource;

    if ((err = dav_orangefs_walk(&params,DAV_INFINITY,&multi_status)) != NULL){
      return err;
    }

    /* when we return from the walk, the dir is empty, now it can
       be removed...
    */
    return orangeRemove((char*)resource->info->BaseName,
                        *resource->info->parent_ref,
                        resource->info->credential,
                        resource->pool);
  } else {
    /* a file */
    return orangeRemove((char*)resource->info->BaseName,
                        *resource->info->parent_ref,
                        resource->info->credential,
                        resource->pool);

  }

  return NULL;
}

static dav_error *dav_orangefs_walk(const dav_walk_params *params, 
                                     int depth,
                                     dav_response **response)
{                              
  dav_walk_resource walkResource = { 0 };  
  dav_error *err = NULL;
  PVFS_sysresp_readdir resp_readdir;
  PVFS_ds_position token;
  int pvfs_dirent_incount;
  int max_dirents_returned=25;
  int rc, i=0;
  char *newResource;
  dav_walk_params newParams = { 0 };
  dav_resource *newDavResource;
  dav_resource *tmp_dr;
  dav_resource *dstResource;
  dav_response *newResponse = NULL;
  dav_resource_private *orangefsInfo;
  char *thisDirectory;
  int isRemoveWalk=0;
  int isCopyWalk=0;
  PVFS_object_ref recurse_ref;
  PVFS_object_ref readDir_ref;
  clock_t start,end;
  double cpu_time_used;

  if (debug_orangefs) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "dav_orangefs_walk: type:%d: uri:%s: removeWalk:%d: "
      "copyWalk:%d: depth:%d: pid:%d:",
      params->walk_type,
      params->root->uri,
      params->root->info->removeWalk,
      params->root->info->copyWalk,
      depth,
      getpid());
    start = clock();
  }

  isRemoveWalk = params->root->info->removeWalk;
  isCopyWalk = params->root->info->copyWalk;

  walkResource.walk_ctx = params->walk_ctx;
  walkResource.pool = params->pool;

  /* if we're dealing with a directory, we should make
     sure the uri field has a trailing slash...
   */
  if (params->root->collection) {
    tmp_dr = apr_pcalloc(params->pool,sizeof(struct dav_resource));
    tmp_dr->type = params->root->type;
    tmp_dr->exists = params->root->exists;
    tmp_dr->collection = params->root->collection;
    tmp_dr->versioned = params->root->versioned;
    tmp_dr->baselined = params->root->baselined;
    tmp_dr->working = params->root->working;
    tmp_dr->uri = apr_pcalloc(params->pool,1);
    tmp_dr->uri = apr_pstrcat(params->pool,params->root->uri,"/",NULL);
    tmp_dr->info = params->root->info;
    tmp_dr->hooks = params->root->hooks;
    tmp_dr->pool = params->root->pool;
    walkResource.resource = tmp_dr;
  } else {
    walkResource.resource = params->root;
  }

  /* func is dav_propfind_walker when we're looking for properties, 
     dav_validate_walker and then dav_lock_walker when we're making locks, 
     dav_unlock_walker when we're removing locks...

     func list:

         dav_orangefs_copy_walker
         dav_orangefs_delete_walker
         dav_propfind_walker
         dav_label_walker (used for versioning...)
         dav_lock_walker
         dav_unlock_walker
         dav_inherit_walker
         dav_validate_walker

  */ 

  err = (*params->func)
        (&walkResource,params->root->info->orangefs_finfo.filetype);

  if (err != NULL) {
    return err;
  }

  /* we're done if the current resource is a file or if depth == 0... */
  if (depth == 0 || params->root->collection != 1) {
    if (walkResource.response) {
      *response = walkResource.response;
    } else {
      *response = NULL;
    }
    if (debug_orangefs) {
      end = clock();
      DBG2("dav_orangefs_walk: cpu_time_used:%f:  depth:%d:",
        ((double)(end-start))/CLOCKS_PER_SEC,depth); 
    }
    return NULL;
  }

  /* If the current resource is a directory and the depth is more than 0,
     read-dir it and get the props on all the files, and recurse if there's
     any directories...

     We decide (max_dirents_returned) how many objects (file/dir names)
     PVFS_sys_readdir will return each time it is called, and PVFS_sys_readdir
     communicates via resp_readdir.token which is both a cursor into
     the enumeration of file/dir names and a flag that lets us know when
     we're done.

     walkResource's value changes with each file/directory that we encounter,
     but the ref argument to PVFS_sys_readdir needs to remain constant
     while we enumerate a directory's contents...
  */
  pvfs_dirent_incount = max_dirents_returned;
  token=0;
  readDir_ref.handle = (PVFS_handle)walkResource.resource->info->ref->handle;
  readDir_ref.fs_id = (PVFS_fs_id)walkResource.resource->info->ref->fs_id;
  do {

    memset(&resp_readdir,0,sizeof(PVFS_sysresp_readdir));

    if (debug_orangefs) {
     ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
       "dav_orangefs_walk: PVFS_sys_readdir: handle:%d: fs_id:%d: "
       "token:%d: pid:%d:",
       readDir_ref.handle,readDir_ref.fs_id,token,getpid());
    }

    /* The way the DAV-core/Apache handles errors here is a little weird. 
       The MacOS client, for example, fires off a PROPFIND for the
       directory which delivers us to the readdir below. If the readdir 
       fails a log message is written out, the connection is aborted, and 
       no response is sent back to the client for the PROPFIND. The MacOS 
       client responds to the situation by continuously pumping out PROPFINDS 
       until the log file on the Apache server fills up (or the universe 
       becomes unhinged in some other undesirable way). The same thing happens 
       with the apr repository, the "fault" is in Apache's (understandable) 
       inability to respond to these kinds of requests when they fail, and
       mostly in the MacOS's insistence on retaliating by launching a 
       denial-of-service attack on the Apache server.

       I think this thread from dev.httpd.apache.org sums it up...
   
        mod_dav streamy error handling
        http://web.archiveorange.com/archive/v/zvbaICW109SemnvoVejk

       Anyhow, if we get a bad return code from the readdir, we'll
       log a message, but not return an error.
    */
    if ((rc = PVFS_sys_readdir(
                readDir_ref,
                (!token ? PVFS_READDIR_START : token),
                pvfs_dirent_incount,
                walkResource.resource->info->credential,
                &resp_readdir,
                NULL)) < 0) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "dav_orangefs_walk: PVFS_sys_readdir, rc:%d:\n",rc);
/*      Remember ifdefs for dav_new_error 2.2 support. */
//      return dav_new_error(params->pool,HTTP_FORBIDDEN,0,NULL);
        return NULL;
    }

    /* any given directory might contain more objects than can be
       dealt with on a single pass, we only want to establish the
       current directory's name on the first pass...
    */
    if (i == 0) {
      thisDirectory = apr_pcalloc(params->pool,1);
      thisDirectory = apr_pstrcat(params->pool,thisDirectory,
                                  walkResource.resource->uri,NULL);
    }

    for (i=0;i<resp_readdir.pvfs_dirent_outcount;i++) {

      newResource = apr_pcalloc(params->pool,1);
      newResource = apr_pstrcat(params->pool,newResource,
                                thisDirectory,NULL);

      /* this seems like it is always putting an extra slash
         between directory names, doesn't hurt anything, /but//it//is//ugly.
         It doesn't seem like it should hurt anything to take it out,
         litmus still runs through everything with it gone. We'll
         leave it here for a while as a clue in case we just broke
         some unanticipated thing... take it (and this comment) out later...
       */
//      newResource = apr_pstrcat(params->pool,newResource,"/",NULL);

      newResource = apr_pstrcat(params->pool,newResource,
                                resp_readdir.dirent_array[i].d_name,NULL);

      orangefsInfo = apr_pcalloc(params->pool,sizeof(*orangefsInfo));
      orangefsInfo->mountPoint =
        apr_pstrdup(params->pool,walkResource.resource->info->mountPoint);

      credCopy(walkResource.resource->info->credential, 
               &(orangefsInfo->credential),
               params->pool);

      /* get a copy of the request_rec so we can look up the dir conf
         if/when needed...
       */
      orangefsInfo->r = walkResource.resource->info->r; 

      /* Obtain orangefs info for resource we just found with readdir... */
      rc = orangeAttrs("stat",newResource,params->pool,orangefsInfo,
                       NULL,NULL);

      /* remember if this is a remove or copy walk... */
      orangefsInfo->removeWalk = isRemoveWalk;
      orangefsInfo->copyWalk = isCopyWalk;

      if (rc) {
        ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
          "dav_orangefs_walk: can't stat orangeFs!");
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
        return dav_new_error(params->pool,HTTP_NOT_FOUND,0,NULL);
#else
        return dav_new_error(params->pool,HTTP_NOT_FOUND,0,0,NULL);
#endif
      }

      /* build a new dav_resource struct for the resource we just found
         with readdir...
       */
      newDavResource = apr_pcalloc(params->pool,sizeof(struct dav_resource));

      /* reset some fields to match the new resource... */
      if (orangefsInfo->orangefs_finfo.filetype == APR_DIR) {
        newDavResource->collection = 1;
      } else {
        newDavResource->collection = 0;
      }
      newDavResource->uri = newResource;

      newDavResource->info       = orangefsInfo;

      /* probably OK just to copy in all this other stuff... */
      newDavResource->type       = walkResource.resource->type;
      newDavResource->exists     = walkResource.resource->exists;
      newDavResource->versioned  = walkResource.resource->versioned;
      newDavResource->baselined  = walkResource.resource->baselined;
      newDavResource->working    = walkResource.resource->working;
      newDavResource->hooks      = walkResource.resource->hooks;
      newDavResource->pool       = walkResource.resource->pool;
      walkResource.resource = newDavResource;

      /* when on a copy walk, we need to build a resource handle
         for dst...
       */
      if (isCopyWalk) {
        dstResource = apr_pcalloc(params->pool,sizeof(struct dav_resource));
        dstResource->collection = newDavResource->collection;
        dstResource->uri = apr_pcalloc(params->pool,1);
        dstResource->uri = 
          apr_pstrcat(params->pool,dstResource->uri,
                      ((dav_resource*)(params->walk_ctx))->uri,NULL);
        dstResource->uri = 
          apr_pstrcat(params->pool,dstResource->uri,"/",NULL);
        dstResource->uri = 
          apr_pstrcat(params->pool,dstResource->uri,
                      resp_readdir.dirent_array[i].d_name,NULL);
        dstResource->info = apr_pcalloc(params->pool,sizeof(*orangefsInfo));
        credCopy(orangefsInfo->credential, 
                 &(dstResource->info->credential),
                 params->pool);
        dstResource->info->removeWalk = orangefsInfo->removeWalk;
        dstResource->info->copyWalk = orangefsInfo->copyWalk;
        dstResource->info->mountPoint =
          apr_pstrdup(params->pool,params->root->info->mountPoint);
        dstResource->info->ref = 
          ((dav_resource*)(walkResource.walk_ctx))->info->ref;
        dstResource->info->Uri = apr_pstrdup(params->pool,dstResource->uri);
        dstResource->info->DirName = 
          apr_pcalloc(params->pool,PVFS_NAME_MAX);
        dstResource->info->BaseName = 
          apr_pcalloc(params->pool,PVFS_NAME_MAX);
        dirnameBasename((char *)dstResource->uri,
                        dstResource->info->DirName,
                        dstResource->info->BaseName);
        dstResource->pool = walkResource.resource->pool;
        dstResource->type = walkResource.resource->type;
        walkResource.walk_ctx = (void *)dstResource;
      }

      if (orangefsInfo->orangefs_finfo.filetype == APR_REG) {

        /* for files, call the walk-specific helper func... */
        err = (*params->func)(&walkResource,APR_REG);

        if (err != NULL) {
          return err;
        }
        
      } else if (orangefsInfo->orangefs_finfo.filetype == APR_DIR) {

        /* For dirs, recurse... */
        newParams.walk_type = params->walk_type;
        newParams.func      = params->func;
        newParams.walk_ctx  = params->walk_ctx;
        newParams.pool      = params->pool;
        newParams.root      = newDavResource;
        newParams.lockdb    = params->lockdb;

        if (isCopyWalk) {
          /* create the dir */
          if ((rc = orangeMkdir((dav_resource *)walkResource.walk_ctx))) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
            return dav_new_error(params->pool,HTTP_MULTI_STATUS,0,
                              apr_psprintf(params->pool,
                              "dav_orangefs_copy_resource: could not "
                              "create %s rc=%d",
                              ((dav_resource*)(walkResource.walk_ctx))->uri,
                              rc));
#else
            return dav_new_error(params->pool,HTTP_MULTI_STATUS,0,0,
                              apr_psprintf(params->pool,
                              "dav_orangefs_copy_resource: could not "
                              "create %s rc=%d",
                              ((dav_resource*)(walkResource.walk_ctx))->uri,
                              rc));
#endif
          }
          /* fixup ref to reflect the new directory we just created and
             are about to recurse down into... remember the old ref,
             so we can use it when we return from recursion...
             use the copy-walk walk context for the recurse...
          */
          
          recurse_ref.handle = dstResource->info->ref->handle;
          recurse_ref.fs_id = dstResource->info->ref->fs_id;
          dstResource->info->ref->handle = 
            ((dav_resource*)(walkResource.walk_ctx))->info->recurse_ref->handle;
          dstResource->info->ref->fs_id = 
            ((dav_resource*)(walkResource.walk_ctx))->info->recurse_ref->fs_id;
          newParams.walk_ctx  = (void *)dstResource;
        }

        dav_orangefs_walk(&newParams, depth-1, &newResponse);

        if (isCopyWalk) {
          /* copy over the properties of the dir we made right before 
             the walk...
          */
          orangePropCopy(walkResource.resource,
                         (dav_resource *)walkResource.walk_ctx);
          /* put pre-recurse ref back and continue on... */
          dstResource->info->ref->handle = recurse_ref.handle;
          dstResource->info->ref->fs_id = recurse_ref.fs_id;
        }

        if (isRemoveWalk) {
          err=orangeRemove((char*)newDavResource->info->BaseName,
                           *newDavResource->info->parent_ref,
                           newDavResource->info->credential,
                           newDavResource->pool);

          if (err != NULL) {
            return err;
          }
          
        }

      }

    }

    token=resp_readdir.token;

    if (resp_readdir.pvfs_dirent_outcount < pvfs_dirent_incount) {
      break;
    }

    /* free blob of memory allocated by readdir... */
    if (resp_readdir.pvfs_dirent_outcount) {
      free(resp_readdir.dirent_array);
    }

  } while (resp_readdir.pvfs_dirent_outcount != 0);

  if ((walkResource.response) && (newResponse)) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
    "dav_orangefs_walk: not supposed to have two responses!");
  }

  if (newResponse) {
    *response = newResponse;
  } else {
    *response = walkResource.response;
  }

  if (debug_orangefs) {
    end = clock();
    DBG2("dav_orangefs_walk: cpu_time_used:%f: depth:%d:",
      ((double)(end-start))/CLOCKS_PER_SEC,depth); 
  }

  return NULL; 
}   

static dav_error 
  *dav_orangefs_delete_walker(dav_walk_resource *walkResource, int type)
{
  if (debug_orangefs) {
    DBG2("dav_orangefs_delete_walker:%s: pid:%d:",
          walkResource->resource->uri,getpid());
  }

  if (!walkResource->resource->collection) {
    return orangeRemove((char*)walkResource->resource->info->BaseName,
                        *walkResource->resource->info->parent_ref,
                        walkResource->resource->info->credential,
                        walkResource->resource->pool);
  }

  return NULL;
}

static dav_error
  *dav_orangefs_copy_walker(dav_walk_resource *walkResource, int type)
{
  if (debug_orangefs) {
    DBG3("dav_orangefs_copy_walker: %s -----> %s    pid:%d:",
         walkResource->resource->uri,
         ((dav_resource*)(walkResource->walk_ctx))->uri,
         getpid());
  }

  if (!walkResource->resource->collection) {
    return orangeCopy(walkResource->resource, 
                      (dav_resource *)walkResource->walk_ctx);
  }

  return NULL;
}

/* dav_orangefs_getetag:  Stolen from dav_fs_etag: Stolen from ap_make_etag.
   etag - Section 13.3.3 of RFC2616 and Section 8.6 of RFC4918
   I think the orangefs handle for a file/directory is unique 
   across the filesystem, so it should be a good etag. Perhaps
   a better etag would include both handle and fs_id?
 */
const char *dav_orangefs_getetag(const dav_resource *resource)
{

  if (debug_orangefs) {
    DBG1("dav_orangefs_getetag:%s",resource->uri);
  }

  if (!resource->exists) {
    return apr_pstrdup(resource->pool, "");
  }

  return apr_psprintf(resource->pool,"%lu-%d",
                      resource->info->ref->handle,resource->info->ref->fs_id);
                      
}

static const dav_hooks_repository dav_hooks_repository_orangefs =
{
  ORANGE_GET_HANDLER,
  dav_orangefs_get_resource,
  dav_orangefs_get_parent_resource,
  dav_orangefs_is_same_resource,
  dav_orangefs_is_parent_resource,
  dav_orangefs_open_stream,
  dav_orangefs_close_stream,
  dav_orangefs_write_stream,
  dav_orangefs_seek_stream,
  dav_orangefs_set_headers, 
  dav_orangefs_deliver,
  dav_orangefs_create_collection,
  dav_orangefs_copy_resource,
  dav_orangefs_move_resource,
  dav_orangefs_remove_resource,
  dav_orangefs_walk,
  dav_orangefs_getetag
};

/* we don't have a database to open, we keep properties in
   extended attributes, but propdb_open gets called by the 
   DAV core at the start of property-related requests, and we 
   can collect and store private/orangefs-related information needed to 
   process the request here.
 */
static dav_error *dav_orangefs_propdb_open(apr_pool_t *pool,
                                       const dav_resource *resource,
                                       int ro,
                                       dav_db **pdb)
{
  if (debug_orangefs) {
    DBG0("dav_orangefs_propdb_open:");
  }

  /* dav_db is an opaque handle (typedef struct dav_db dav_db;) defined
     in main/mod_dav.h and is defined locally in fs/dbm.c and catacomb/props.c,
     and we have our own local to this repository... 
  */
  *pdb = apr_pcalloc(pool,sizeof(struct dav_db));

  /* keep the resource name (file or directory name) handy... */
  (*pdb)->file = apr_pstrdup(pool,resource->uri);

  (*pdb)->pool=pool;

  (*pdb)->mountPoint = apr_pstrdup(pool,resource->info->mountPoint);

  /* reuse the credential... */
  credCopy(resource->info->credential,&((*pdb)->credential),pool);

  /* initialize list we'll use later when associating namespaces with their
     prefixes.
  */
  (*pdb)->pm = (struct prefixMap *)apr_pcalloc(pool,sizeof(struct prefixMap));
  (*pdb)->pm->prefixNumber=0;
  (*pdb)->pm->mapsTo='\0';
  (*pdb)->pm->next='\0';

  return NULL;
}

static void dav_orangefs_propdb_close(dav_db *db) {

  if (debug_orangefs) {
    DBG0("dav_orangefs_propdb_close");
  }

}

/* In "the DAV core" (specifically, in main/props.c, dav_get_props(...)) 
   *db_hooks->output_value is called for each dead property in a propfind
   request. After the first time that *db_hooks->output_value is called,
   *db_hooks->define_namespaces is called once to "fill up xi" with
   all the XML namespaces that will be needed to create a valid response 
   element for the request. The DAV fs repository module's define-namespaces
   hook accomplishes its task by enumerating every namespace in the
   resources' METADATA berkeley-db entry into xi. The resulting response 
   element contains a mapping for every possible namespace associated with 
   the resource, and is valid if there's only one property in the 
   propfind (or whatever) request, or if all the resources' properties 
   are in the request, or anything in between.
  
   In the general case, it might not be so cheap for a repository
   to enumerate all the possible namespaces.
  
   We make define_namespaces mostly into a no-op, and dav_xmlns_add
   is called from output_value for just the needed namespaces.
*/
static dav_error *dav_orangefs_propdb_define_namespaces(dav_db *db,
                           dav_xmlns_info *xi)
{

  if (debug_orangefs) {
    DBG0("dav_orangefs_propdb_define_namespaces");
  }

  /* a propfind/propname doesn't have to call output_value, but
     it will flow through here,  xmlns:D="DAV:" is good to have
     on the response element...
  */
  dav_xmlns_add(xi, "D", "DAV:");

  return NULL;
}

static dav_error *dav_orangefs_propdb_map_namespaces(dav_db *db,
                            const apr_array_header_t *namespaces,
                                     dav_namespace_map **mapping)
{

  if (debug_orangefs) {
    DBG0("dav_orangefs_propdb_map_namespaces");
  }

  /* store a copy of the provided namespaces structure in our private handle, 
     we might need it later...
  */
  db->namespaces=namespaces;

  /* A namespace name is a URI.
    
     WebDAV property names are qualified XML names (pairs of XML namespace
     name and local name). - RFC4918-19... if a property name has an 
     associated namespace, we store it as-is as part of the property name. 
     Other repositories (like dav-fs) encode the namespace name into the 
     property name, and use the encoding as part of a "prefix".
     When you're looking at the XML that makes up a PROPFIND request, for
     example, you can expect to see prefixes on the property name/value
     elements and the mappings, or XML namespace declarations 
     (xmlns:prefix="URI"), on the multistatus and response elements.
     Potentially the properties of a resource can have a one-to-many 
     relationship of URIs-to-prefixes. Most repositories (including this one)
     don't preserve prefixes supplied by clients, but use internally generated 
     prefixes. 
    
     Other DAV repositories (like dav-fs) fill in the "mapping" 
     structure that is sent into map_namespaces. The values stored
     in the "mapping" structure in dav-fs are used in
     the process of associating internal namespace prefixes and
     actual namespace URIs via a hash table. We don't use the "mapping"
     structure or hash table, so we don't do much here in map_namespaces.
  */

  return NULL;
}

/* gets called for each property/value that needs to be returned */
static dav_error *dav_orangefs_propdb_output_value(dav_db *db,
                                                   const dav_prop_name *name,
                                                   dav_xmlns_info *xi,
                                                   apr_text_header *phdr,
                                                   int *found)
{
  char *propertyName;
  char *value='\0';
  int rc=0;
  struct prefixMap *thisPM;
  char *element;
  char *prefix;
  char *prefixIndex;
  int i, remainder;
  dav_resource_private *orangefsInfo;

  if (debug_orangefs) {
    DBG2("dav_orangefs_propdb_output_value: name:%s: value:%s:",
          name->name,value);
  }

  /* run whichever (if any) namespace that needs to be mapped in the
     response element for this particular property through dav_xmlns_add.
     We can tell if we're dealing with an already mapped namespace,
     and only run namespaces through dav_xmlns_add once for a particular
     request.
   */
  thisPM=db->pm;
  while (thisPM->mapsTo != '\0') {
    if (!strcmp(name->ns,thisPM->mapsTo)) break;
    thisPM=thisPM->next;
  }
  if (thisPM->mapsTo == '\0') {
    thisPM->mapsTo = apr_pcalloc(db->pool,strlen(name->ns)+1);
    strcpy(thisPM->mapsTo,name->ns);
    thisPM->next=
      (struct prefixMap *)apr_pcalloc(db->pool,sizeof(struct prefixMap));
    thisPM->next->prefixNumber=thisPM->prefixNumber+1;
    thisPM->next->mapsTo='\0';
    thisPM->next->next='\0';
    /* if there is no namespace, avoid putting xmlns:nsX="" on the
       response line... this seems right, and it makes litmus happy...
     */
    if (*thisPM->mapsTo != '\0') {
      dav_xmlns_add(xi,
                    apr_psprintf(db->pool,"ns%d",thisPM->prefixNumber),
                    thisPM->mapsTo);
    }
  }
  /* seems like this happens each time through, if it even needs
     to happen at all, it would be nice if it only happened once
     on each request...
  */
  dav_xmlns_add(xi, "D", "DAV:");

  /* construct the property name so that we can look up its value, the
     property name  will look like one of these...
       "localName"
       "localName namespace"
  */
  propertyName = apr_pcalloc(db->pool,strlen(name->name)+strlen(name->ns)+2);
  strcpy(propertyName,name->name);
  if (name->ns[0] != '\0') {

    /* there's a namespace designation... */
    strcat(propertyName," ");
    strcat(propertyName,name->ns);
    for (i=0,remainder=thisPM->prefixNumber;remainder>1;i++) {
      remainder=remainder/10;
    }
    prefix=apr_pcalloc(db->pool,i+1+4);
    sprintf(prefix,"ns%d:",thisPM->prefixNumber);
    
  } else {

    /* no namespace... */
    prefix=apr_pcalloc(db->pool,1);
  }

  orangefsInfo = apr_pcalloc(db->pool,sizeof(*orangefsInfo));
  orangefsInfo->mountPoint = apr_pstrdup(db->pool,db->mountPoint);
  credCopy(db->credential,&(orangefsInfo->credential),db->pool);

  /* get the property value. */
  value = apr_pcalloc(db->pool,BUFSIZ);
  rc=orangeAttrs("get",db->file,db->pool,orangefsInfo,propertyName,value);
  if (rc) {
    if (debug_orangefs) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "dav_orangefs_propdb_output_value: failed to get property value"
        " for %s on %s, pid:%d: rc:%d:", propertyName, db->file,getpid(),rc);
    }
  }

  if (rc) {
    *found = 0;
    return NULL;
  }
  *found = 1;

  if (value[0] == '\0') {
    element = apr_psprintf(db->pool,"<%s%s/>\n",prefix,name->name);
  } else {
    element = apr_psprintf(db->pool,"<%s%s>%s</%s%s>\n",
                           prefix,name->name,value,prefix,name->name);
  }

  apr_text_append(db->pool,phdr,element);

  return NULL;

}

/* When storing (and later retrieving) properties, we'll have to deal
   with namespaces. 
  
   XML namespaces disambiguate WebDAV property names and XML elements.
   Any WebDAV user or application can define a new namespace in order to
   create custom properties or extend WebDAV XML syntax. RFC4918-21.2
  
   WebDAV property names are qualified XML names (pairs of XML namespace
   name and local name)... RFC4918-9
  
   This repository realizes DAV resources as files or directories and
   custom resource properties as extended attributes of files or directories.
   For example, data associated with a (non-collection) resource is 
   stored in a file, and the resource's properties are are stored as 
   extended attributes of the file. 
   
   XML namespace names of custom properties are combined with with 
   the property name to create the name of the extended attribute 
   which stores the property's value.
   
   Since DAV can't "see into" complex properties, (such as the "Authors" 
   property in the proppatch example in RFC4918 9.2.2) they will be stored 
   as XML blobs.
  
*/
static dav_error *dav_orangefs_propdb_store(dav_db *db,
                                             const dav_prop_name *name,
                                             const apr_xml_elem *elem,
                                             dav_namespace_map *mapping)
{
  char *propertyName;
  char *propertyValue;
  int rc;
  const char *text='\0';
  char *prefix;
  int i;
  dav_resource_private *orangefsInfo;

  if (debug_orangefs) {
    /* elem->ns is a negative number if there is no namespace */
    DBG1("dav_orangefs_propdb_store: ns:%d:",elem->ns);
  }

  /* We want to take the property's namespace and its local name
     and mash them together to create the name of the OrangeFS extended 
     attribute where we'll store the property's value.
     Here's an enumeration of the possibilities for how the extended 
     attribute names will be constructed. 
       "localName"
       "localName namespace"
     We can get the local name from name->name.
     We can get the namespace (if any) from name->ns.

     "... it is not possible to define the same property twice on a
      single resource, as this would cause a collision in the resource's
      property namespace. "   RFC 4918 4.4
   
  */
  propertyName = apr_pcalloc(db->pool,1);

  /* name->name and elem->name should be the same. */
  propertyName = apr_pstrcat(db->pool,propertyName,name->name,NULL);

  if (!strcmp(propertyName,DAVLOCK_PROPERTY) || 
      !strcmp(propertyName,LOCKNULL_PROPERTY)) {
    if (name->ns[0] == '\0') {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(db->pool,HTTP_INTERNAL_SERVER_ERROR,0,
                           apr_psprintf(db->pool,"dav_orangefs_propdb_store: "
                           "%s is a reserved property name.",
                           propertyName));
#else
      return dav_new_error(db->pool,HTTP_INTERNAL_SERVER_ERROR,0,0,
                           apr_psprintf(db->pool,"dav_orangefs_propdb_store: "
                           "%s is a reserved property name.",
                           propertyName));
#endif
    }
  }

  /* name->ns is the namespace URI, or a NULL string.  */
  if (name->ns[0] != '\0') {
    propertyName = apr_pstrcat(db->pool,propertyName," ",NULL);
    propertyName = apr_pstrcat(db->pool,propertyName,name->ns,NULL);
  }

  propertyValue = apr_pcalloc(db->pool,1);

  /* if *elem.first_child != NULL then we have a "multivalued" property.
     (gdb) p elem->first_child
     $39 = (struct apr_xml_elem *) 0x69f128       <---- multivalued
     (gdb) p elem->first_child
     $40 = (struct apr_xml_elem *) 0x0            <---- not multivalued
     just try to cram the multivalued XML away as the value.
     apr_xml_to_text can get us the XML as a text value...
  */ 
  if (elem->first_child) {
    /* must be a "multi-valued" property (like the "Authors" property in the 
       proppatch example in RFC4918 9.2.2) - it will be stored as an XML blob.
    */
    apr_xml_to_text(db->pool,elem,APR_XML_X2T_INNER,
                   (apr_array_header_t *)db->namespaces,NULL,&text,NULL);
    propertyValue = apr_pstrcat(db->pool,propertyValue,text,NULL);
  } else {
    /* simple property... */
    if (elem->first_cdata.first) {
      propertyValue = apr_pstrcat(db->pool,propertyValue,
                                  elem->first_cdata.first->text,NULL);
    }
  }

  orangefsInfo = apr_pcalloc(db->pool,sizeof(*orangefsInfo));
  orangefsInfo->mountPoint = apr_pstrdup(db->pool,db->mountPoint);
  credCopy(db->credential,&(orangefsInfo->credential),db->pool);

  rc=orangeAttrs("set",db->file,db->pool,orangefsInfo,
                 propertyName,propertyValue);
  if (rc) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "dav_orangefs_propdb_store: failed to set %s property on %s",
      propertyName, db->file);
    // this is an example of a place where we should be able to
    // map pvfs errors to http errors in order to return a more
    // meaningful error.
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(db->pool,HTTP_FORBIDDEN,0,
      "dav_orangefs_propdb_store: permission denied");
#else
    return dav_new_error(db->pool,HTTP_FORBIDDEN,0,0,
      "dav_orangefs_propdb_store: permission denied");
#endif
  }

  return NULL;
}

static dav_error *dav_orangefs_propdb_remove(dav_db *db, 
                                             const dav_prop_name *name)
{
  char *propertyToDelete;
  dav_resource_private *orangefsInfo;

  if (debug_orangefs) {
    DBG2("dav_orangefs_propdb_remove: remove %s from %s",name->name,db->file);
  }

  propertyToDelete = apr_pstrdup(db->pool,name->name);

  if (name->ns[0] != '\0') {
    propertyToDelete = apr_pstrcat(db->pool,propertyToDelete,
                                            " ",
                                            name->ns,
                                            NULL);
  }

  orangefsInfo = apr_pcalloc(db->pool,sizeof(*orangefsInfo));
  orangefsInfo->mountPoint = apr_pstrdup(db->pool,db->mountPoint);
  credCopy(db->credential,&(orangefsInfo->credential),db->pool);

  if (orangeAttrs("remove",db->file,db->pool,orangefsInfo,
                   propertyToDelete,NULL)) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(db->pool,HTTP_INTERNAL_SERVER_ERROR,0,
                         apr_psprintf(db->pool,"dav_orangefs_propdb_remove: "
                         "could not remove %s from %s",
                         name->name,db->file));
#else
    return dav_new_error(db->pool,HTTP_INTERNAL_SERVER_ERROR,0,0,
                         apr_psprintf(db->pool,"dav_orangefs_propdb_remove: "
                         "could not remove %s from %s",
                         name->name,db->file));
#endif
  }

  return NULL;
}

static int dav_orangefs_propdb_exists(dav_db *db, const dav_prop_name *name)
{

  if (debug_orangefs) {
    DBG0("dav_orangefs_propdb_exists: -------- unimplemented ----------");
  }

  return 0;
}

/*  fill in pname with the property's name and its real (not prefix)
    namespace name.
  
    (gdb) p *pname
    $21 = {ns = 0x6ebb7b "aaaURI", name = 0x6e9936 "aaaname"}
  
*/
static dav_error * dav_orangefs_propdb_first_name(dav_db *db, 
                                                  dav_prop_name *pname)
{
  dav_resource_private *orangefsInfo;
  int rc;
  struct allPNs *head, *this;
  int i;
  char *thisLocalName, *thisNamespaceName;

  if (debug_orangefs) {
    DBG1("dav_orangefs_propdb_first_name: uid:%d:",db->credential->userid);
  }

  /* trigger for "done"... */
  pname->ns = NULL;

  orangefsInfo = apr_pcalloc(db->pool,sizeof(*orangefsInfo));
  orangefsInfo->mountPoint =
        apr_pstrdup(db->pool,db->mountPoint);
  orangefsInfo->this=NULL;
  credCopy(db->credential,&(orangefsInfo->credential),db->pool);

  /* WebDAV property names are qualified XML names (pairs of XML namespace
     name and local name). - RFC4918-19... Extended attribute names contain
     both the property-name's local name and its namespace name (if any),
     and look like this:
       "localName" 
       "localName namespace"

     Make a list of all the property (extended attribute) names for this
     resource.
  */
  if (orangeAttrs("enum",db->file,db->pool,orangefsInfo,NULL,NULL)) {
    /* something (probably permissions) went awry on the enum.. */
    return NULL;
  }

  /* there are no dead properties associated with this resource. */
  if (!orangefsInfo->this->propertyName) {
    return NULL;
  }

  /* work out the local name and the namespace name of the current property */
  head = this = orangefsInfo->this;

  first_next_helper(this,pname,db);

  return NULL;
}

static dav_error * dav_orangefs_propdb_next_name(dav_db *db, 
                                                  dav_prop_name *pname)
{
  struct allPNs *this;
  char *thisLocalName, *thisNamespaceName;
  int i;

  if (debug_orangefs) {
    DBG0("dav_orangefs_propdb_next_name");
  }

  /* trigger for "done"... */
  pname->ns = NULL;

  /* take up where we left off, at the next name... */
  this=db->thisPN;

  first_next_helper(this,pname,db);

  return NULL;
}

/* this helper routine supplements dav_orangefs_propdb_first_name and
   dav_orangefs_propdb_next_name... if propertyname is not null,
   fill out pname with localname and namespace name.
*/
void first_next_helper(struct allPNs *this, dav_prop_name *pname, dav_db *db) 
{
  char *thisLocalName, *thisNamespaceName;
  int i;

  if (debug_orangefs) {
    DBG0("orangefs: first_next_helper");
  }

  if (this->propertyName[0] != '\0') {

    thisLocalName = apr_pcalloc(db->pool,1);
    thisLocalName = apr_pstrcat(db->pool,thisLocalName,this->propertyName,NULL);

    thisNamespaceName = apr_pcalloc(db->pool,1);

    /* see if the property name has a namespace name embedded in it. */
    for (i=0;thisLocalName[i] != '\0';i++) {
      if (thisLocalName[i] == ' ') {
        thisLocalName[i] = '\0';
        thisNamespaceName =
          apr_pstrcat(db->pool,thisNamespaceName,thisLocalName+(i+1),NULL);
      }
    }

    /* return requested information... */
    pname->name = thisLocalName;
    pname->ns = thisNamespaceName;
  }

  db->thisPN = this->next;
}

/* This is where you remember the properties you're changing so that
   you can "roll it back" if some subsequent operation in the 
   request fails... proppatch can drive this hook...
 */
static dav_error *dav_orangefs_propdb_get_rollback(dav_db *db,
                     const dav_prop_name *name,
                     dav_deadprop_rollback **prollback)
{
  char *propertyName;
  char *value;
  dav_resource_private *orangefsInfo;
  struct dav_deadprop_rollback *this;

  if (debug_orangefs) {
    if (name->ns[0]) {
      DBG2("dav_orangefs_propdb_get_rollback: \"%s %s\"", name->name,name->ns);
    } else {
      DBG1("dav_orangefs_propdb_get_rollback: \"%s\"", name->name);
    }
  }

  *prollback = apr_pcalloc(db->pool,sizeof(**prollback));

  propertyName = apr_pcalloc(db->pool,1);

  /* if there's no namespace, we store "name->name", if there is
     a namespace, we store "name->name name->ns"...
  */
  propertyName = apr_pstrcat(db->pool,propertyName,name->name,NULL);
  if (name->ns) {
    propertyName = apr_pstrcat(db->pool,propertyName," ",name->ns,NULL);
  }

  /* if propertyName is already associated with this resource, remember
     it's value, in case we have to do a rollback. If it is a new 
     property, we don't have to remember the value, but we have to 
     remember that it is new.
   */
  orangefsInfo = apr_pcalloc(db->pool,sizeof(*orangefsInfo));
  orangefsInfo->mountPoint =
    apr_pstrdup(db->pool,db->mountPoint);
  credCopy(db->credential,&(orangefsInfo->credential),db->pool);
  value = apr_pcalloc(db->pool,BUFSIZ);
  if (!orangeAttrs("get",db->file,db->pool,orangefsInfo,propertyName,value)) {
    (*prollback)->propertyName = apr_pstrcat(db->pool,propertyName,NULL);
    (*prollback)->propertyValue = apr_pstrcat(db->pool,value,NULL);
    (*prollback)->new = 0;
  } else {
    (*prollback)->propertyName = apr_pstrcat(db->pool,propertyName,NULL);
    (*prollback)->new = 1;
  }

  return NULL;
}

static dav_error *dav_orangefs_propdb_apply_rollback(
                     dav_db *db,
                     dav_deadprop_rollback *rollback)
{
  dav_resource_private *orangefsInfo;

  if (debug_orangefs) {
    DBG2("dav_orangefs_propdb_apply_rollback:%s: new:%d:", 
         rollback->propertyName,rollback->new);
  }

  orangefsInfo = apr_pcalloc(db->pool,sizeof(*orangefsInfo));
  orangefsInfo->mountPoint =
    apr_pstrdup(db->pool,db->mountPoint);
  credCopy(db->credential,&(orangefsInfo->credential),db->pool);

  /* one thing that might make a PROPPATCH fail is if someone
     tries to directly set a reserved property name, like DAVLOCK_PROPERTY.

     If there was already a lock on the resource, this won't remove the
     lock, since rollback->new would be 0 in such a case...
  */
  if (rollback->new) {
    orangeAttrs("remove",db->file,db->pool,orangefsInfo,
                 rollback->propertyName,NULL);
  } else {
    orangeAttrs("set",db->file,db->pool,orangefsInfo,
                 rollback->propertyName,rollback->propertyValue);
  }

  return NULL;
}

static const dav_hooks_propdb dav_hooks_properties_orangefs =
{
  dav_orangefs_propdb_open,
  dav_orangefs_propdb_close,
  dav_orangefs_propdb_define_namespaces,
  dav_orangefs_propdb_output_value,
  dav_orangefs_propdb_map_namespaces,
  dav_orangefs_propdb_store,
  dav_orangefs_propdb_remove,
  dav_orangefs_propdb_exists,
  dav_orangefs_propdb_first_name,
  dav_orangefs_propdb_next_name,
  dav_orangefs_propdb_get_rollback,
  dav_orangefs_propdb_apply_rollback,
  NULL
};

static const char * const dav_orangefs_namespace_uris[] =
{
  "DAV:",
  "http://orangefs.org/dav/props/",
  NULL
};

enum {
  DAV_DAV_URI,       /* DAV: */
  ORANGEFS_DAV_URI   /* http://orangefs.org/dav/props/ */
};
  
static const dav_liveprop_spec dav_orangefs_props[] =
{
  /* standard DAV properties */
  {
    DAV_DAV_URI,
    "creationdate",
    DAV_PROPID_creationdate,
    0
  },
  {
    DAV_DAV_URI,
    "getcontentlength",
    DAV_PROPID_getcontentlength,
    0
  },
  {
    DAV_DAV_URI,
    "getetag",
    DAV_PROPID_getetag,
    0
  },
  {
    DAV_DAV_URI,
    "getlastmodified",
    DAV_PROPID_getlastmodified,
    0
  },
  {
    DAV_DAV_URI,
    "getcontenttype",
    DAV_PROPID_getcontenttype,
    0
  },

  { 0 }
};

static const dav_liveprop_group dav_orangefs_liveprop_group =
{
  dav_orangefs_props,
  dav_orangefs_namespace_uris,
  &dav_hooks_liveprop_orangefs
};

/* PVFSInit is a TAKE1 server config directive which defaults to
   "on". PVFSInit is looked at in dav_orangefs_init_handler() to
   help decide whether or not to run PVFS_util_init_defaults(). 
   It is bad for a single process to run PVFS_util_init_defaults() more
   than once, so when there is more than one orangefs module loaded into
   apache, PVFSInit can be set to a particular module's name (the module
   whose LoadModule directive comes first in httpd.conf).
*/
static const char *set_PVFSInit(cmd_parms *cmd,
                                void *config,
                                const char *arg)
{
  dav_orangefs_server_conf *conf = config;

  if (DEBUG_ORANGEFS) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"set_PVFSInit: %s",arg);
  }

  conf = ap_get_module_config(cmd->server->module_config,&dav_orangefs_module);

  conf->PVFSInit = (char *)arg;

  return NULL;
}

/* When we enter dav_orangefs_get_resource() to look up information
   about a resource, we initialize the resource's permissions to
   uid=nobody, gid=nobody and perms=0000. If we can't resolve
   user "nobody" or group "nobody" using getpwnam, we just set
   the resource's initial uid and gid to 65534. If you want uid
   and/or gid and/or perms to be intialized some other way, you
   can use this TAKE3 server config directive. All of this is just
   an almost-paranoid precaution, since we look up the resource's
   true permissions before we leave dav_orangefs_get_resource().

   Use numeric, not symbolic, uids and gids if you decide to set
   DAVpvfsDefaultPerms in your httpd.conf:

        DAVpvfsDefaultPerms 10019 10020 000

*/
static const char *set_DAVpvfsDefaultPerms(cmd_parms *cmd,
                                           void *config,
                                           const char *arg1,
                                           const char *arg2,
                                           const char *arg3)
{
  dav_orangefs_server_conf *conf = (dav_orangefs_server_conf *)config;

  if (DEBUG_ORANGEFS) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "set_DAVpvfsDefaultPerms: %s %s %s",arg1,arg2,arg3);
  }

  conf = ap_get_module_config(cmd->server->module_config,&dav_orangefs_module);

  conf->uid = arg1;
  conf->gid = arg2;
  conf->perm = arg3;

  return NULL;
}

static const char *set_certpath(cmd_parms *cmd, void *config,
                                const char *arg1)
{
  dav_orangefs_server_conf *conf = (dav_orangefs_server_conf *)config;
  conf = ap_get_module_config(cmd->server->module_config,&dav_orangefs_module);
  conf->certpath = apr_pstrdup(cmd->pool, arg1);
  return 0;
}

static const command_rec dav_orangefs_cmds[] =
{
  AP_INIT_TAKE1("PVFSInit", set_PVFSInit,NULL,RSRC_CONF,
                "Set to this module's name to specify that this module "
                "should initialize PVFS. (default is On.)"),
  AP_INIT_TAKE3("DAVpvfsDefaultPerms",set_DAVpvfsDefaultPerms,NULL,RSRC_CONF,
                "set resource's default permission attributes. (default is "
                "uid=nobody, gid=nobody, perms=0)"),
  AP_INIT_TAKE1("PutBufSize", ap_set_int_slot,
                (void *)APR_OFFSETOF(dav_orangefs_dir_conf, putBufSize),
                 ACCESS_CONF, "PVFS_Request_contiguous Buffer size argument "
                 "when setting up PVFS_sys_write for PUT (default: 1048576)"),
  AP_INIT_TAKE1("ReadBufSize", ap_set_int_slot,
                (void *)APR_OFFSETOF(dav_orangefs_dir_conf, readBufSize),
                 ACCESS_CONF, "PVFS_Request_contiguous Buffer size argument "
                 "when setting up PVFS_sys_read for GET or COPY "
                 "(default: 1048576)"),
  AP_INIT_TAKE1("DAVpvfsCertPath", set_certpath, 0, ACCESS_CONF,
                "The path to read user keys and certificates from."),

  {NULL}
};

/* set default values for dir configs */
static void *dav_orangefs_create_dir_config(apr_pool_t *p, char *dir)
{
  dav_orangefs_dir_conf *conf;

  if (debug_orangefs) {
    DBG1("dav_orangefs_create_dir_config:%s:",dir);
  }

  conf = (dav_orangefs_dir_conf *)apr_pcalloc(p,sizeof(*conf));
  conf->putBufSize = PUTBUFSIZE;
  conf->readBufSize = READBUFSIZE;
  return conf;
}

/* override default dir config values with config file settings, if 
   present...
 */
static void *dav_orangefs_merge_dir_config(apr_pool_t *p,
                                       void *base, void *overrides)
{   
  dav_orangefs_dir_conf *parent = base;
  dav_orangefs_dir_conf *child = overrides;
  dav_orangefs_dir_conf *conf;

  if (debug_orangefs) {
    DBG0("dav_orangefs_merge_dir_config");
  }

  conf = (dav_orangefs_dir_conf *)apr_pcalloc(p, sizeof(*conf));

  conf->putBufSize = (parent->putBufSize == child->putBufSize) ?
                     (parent->putBufSize) :
                     (child->putBufSize); 

  conf->readBufSize = (parent->readBufSize == child->readBufSize) ?
                      (parent->readBufSize) :
                      (child->readBufSize); 

  return conf;
}   

static void *dav_orangefs_create_server_config(apr_pool_t *p,server_rec *s)
{
  dav_orangefs_server_conf *conf;
  struct passwd *pwd;
  struct group *gp;
  char *uid, *gid, *perm;

  if (DEBUG_ORANGEFS) {
    DBG0("dav_orangefs_create_server_config");
  }

  /* PVFSInit is a flag that helps us decide whether or not to 
     run PVFS_util_init_defaults()...
  */
  conf = (dav_orangefs_server_conf *)apr_pcalloc(p,sizeof(*conf));
  conf->PVFSInit = apr_pstrdup(p,ON);

  /* uid, gid and perm are used to initialze a resource's corresponding
     values prior to their being looked up...
  */
  uid=apr_pcalloc(p,BUFSIZ);
  gid=apr_pcalloc(p,BUFSIZ);
  perm=apr_pcalloc(p,BUFSIZ);

  /* if we can't figure out who nobody is, a lot of unixes think
     nobody is 65534... if we don't have a nobody, and don't like
     65534, we can set DAVpvfsDefaultPerms as a server conf in 
     httpd.conf... but nobody or 65534 is our default value.
  */
  if ((pwd=getpwnam("nobody")) != NULL) {
    sprintf(uid,"%d",pwd->pw_uid);
  } else {
    sprintf(uid,"%d",NOBODY);
  }

  if ((gp=getgrnam("nobody")) != NULL) {
    sprintf(gid,"%d",gp->gr_gid);
  } else {
    sprintf(gid,"%d",NOBODY);
  }

  /* There's nothing to try to look up for perm, we'll set it to
     zero - perm points to a null-string buffer, so we can just
     strcat on a "0"...
  */
  strcat(perm,"0");

  conf->uid=uid;
  conf->gid=gid;
  conf->perm=perm;
  conf->certpath=0;

  return conf;
}

static void *dav_orangefs_merge_server_config(apr_pool_t *p, 
                                              void *server1_conf,
                                              void *server2_conf)
{
  dav_orangefs_server_conf *conf =
    (dav_orangefs_server_conf *)apr_pcalloc(p,sizeof(*conf));
  dav_orangefs_server_conf *s1conf = 
    (dav_orangefs_server_conf *) server1_conf;
  dav_orangefs_server_conf *s2conf =
    (dav_orangefs_server_conf *) server2_conf;

  if (DEBUG_ORANGEFS) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "dav_orangefs_merge_server_config: s1:%s: s2:%s: pid:%d:",
      s1conf->PVFSInit,s2conf->PVFSInit,getpid());
  }

  conf->PVFSInit = 
    (s1conf->PVFSInit == s2conf->PVFSInit) ? 
      s1conf->PVFSInit :
      s2conf->PVFSInit;

  conf->uid = (s1conf->uid == s2conf->uid) ?  s1conf->uid : s2conf->uid;

  conf->gid = (s1conf->gid == s2conf->gid) ?  s1conf->gid : s2conf->gid;

  conf->perm = (s1conf->perm == s2conf->perm) ?  s1conf->perm : s2conf->perm;

  return (void *) conf;
}


static int dav_orangefs_init_handler(apr_pool_t *p,
                                     apr_pool_t *plog,
                                     apr_pool_t *ptemp,
                                     server_rec *s )
{
  int rc;
  dav_orangefs_server_conf *conf;
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
  int nameLen=strlen(dav_orangefs_module.name);
  int fragLen=nameLen-strlen("mod_")-strlen(".c");
  char *name=apr_pcalloc(p,nameLen+strlen("_module"));

  if (nameLen > 6 &&
      !strncmp(dav_orangefs_module.name,"mod_",4) &&
      !strncmp((dav_orangefs_module.name)+nameLen-2,".c",2)) {
    strcpy(name,(dav_orangefs_module.name)+4);
    name[fragLen]='\0';
    strcat(name,"_module");
  } else {
    strcpy(name,"off");
  }

  conf = ap_get_module_config(s->module_config,&dav_orangefs_module);

  if (DEBUG_ORANGEFS) {
    if (conf->PVFSInit) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "dav_orangefs_init_handler:%s:%s:",conf->PVFSInit,name);
    } else {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "dav_orangefs_init_handler:NO PVFSINIT:");
    }
  }

  if (conf->PVFSInit) {
    if (!strcasecmp(conf->PVFSInit,name) ||
        !strcasecmp(conf->PVFSInit,"on")) {
      if (rc = PVFS_util_init_defaults()) {
        ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
          "dav_orangefs_init_handler:PVFS_util_init_defaults "
          "returned:%d:, pid:%d:, PVFSInit:%s:",
          rc,getpid(),conf->PVFSInit);
      }
    }
  }

  /* VERSION is set by developer in Makefile.am */
  ap_add_version_component(p,VERSION);
  return 0;
}

int dav_orangefs_find_liveprop(const dav_resource *resource,
                         const char *ns_uri, const char *name,
                         const dav_hooks_liveprop **hooks)
{
  int namespace;
  int i;

  if (debug_orangefs) {
    DBG1("dav_orangefs_find_liveprop: %s",resource->uri);
  }

  /* look for this namespace URI in the list of our namespace URIs */
  for (namespace=0;dav_orangefs_namespace_uris[namespace]!=NULL;++namespace){
    if (!strcmp(ns_uri,dav_orangefs_namespace_uris[namespace])) {
      break;
    }
  }

  /* punt if not ours */
  if (dav_orangefs_namespace_uris[namespace] == NULL) {
    return 0;
  }

  /* look for this property in the list of properties we support */
  for (i=0;dav_orangefs_props[i].name!=NULL;i++){
    if (namespace == dav_orangefs_props[i].ns && 
        !strcmp(name,dav_orangefs_props[i].name)) {
      *hooks = &dav_hooks_liveprop_orangefs;
      return dav_orangefs_props[i].propid;
    }
  }

  /* namespace matches ours, but not a property we support */
  return 0;
}

static dav_prop_insert dav_orangefs_insert_prop(const dav_resource *resource,
                                                int propid, 
                                                dav_prop_insert what,
                                                apr_text_header *phdr)
{
  const char *s;
  const dav_liveprop_spec *info;
  int global_ns;
  char value[BUFSIZ];
  apr_time_exp_t tms;
  dav_resource_private *orangefsInfo;
  int rc;

  if (debug_orangefs) {
    DBG0("dav_orangefs_insert_prop");
  }

  memset(&value,0,BUFSIZ);

  switch (propid) {
  case DAV_PROPID_creationdate:
    /* ctime is what other repositories use, so we will too...
       pvfs stores time_t, apr_time_exp_gmt uses apr_time_t...
         time_t = number of seconds since epoch
         apr_time_t = number of micro-seconds since epoch
    */
    apr_time_exp_gmt(&tms,resource->info->orangefs_finfo.ctime*1000000);
    sprintf(value, "%.4d-%.2d-%.2dT%.2d:%.2d:%.2dZ",
            tms.tm_year + 1900, tms.tm_mon + 1, tms.tm_mday,
            tms.tm_hour, tms.tm_min, tms.tm_sec);
    break;

  case DAV_PROPID_getcontentlength:

    /* Usually an orangeAttrs "stat" uses an attribute mask that
       avoids calculating file length, since file length is an expensive
       attribute to calculate - all orangefs servers must be visited - so
       unlike other live properties, we don't have contentlength on
       hand. We'll make a special call here to orangeAttrs with the "get size" 
       flag set, which will trigger orangeAttrs to set a special 
       attribute mask that causes file length to be returned. You can't 
       get here without someone explicitly asking for file length.
    */ 
    orangefsInfo = apr_pcalloc(resource->pool,sizeof(*orangefsInfo));
    orangefsInfo->mountPoint =
      apr_pstrdup(resource->pool,resource->info->mountPoint);
    credCopy(resource->info->credential,
             &(orangefsInfo->credential),
             resource->pool);

    rc = orangeAttrs("stat",resource->uri,resource->pool,
                     orangefsInfo,NULL,"get size");
    if (rc) {
      /* see dav/main/mod_dav.h... there's not really a "I tried, but
         something was busted" return code... the ap_log_error will
         leave a tried-but-busted-clue in syslog...
      */
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "dav_orangefs_insert_prop: orangeAttrs failed looking up "
        "content length for %s",resource->uri);
      return DAV_PROP_INSERT_NOTDEF;
    }
    sprintf(value,"%"APR_OFF_T_FMT,orangefsInfo->orangefs_finfo.size);
    break;

  case DAV_PROPID_getetag:
    strcpy(value,dav_orangefs_getetag(resource));
    break;

  case DAV_PROPID_getlastmodified:
    /* pvfs stores time_t, apr_time_exp_gmt uses apr_time_t...
         time_t = number of seconds since epoch
         apr_time_t = number of micro-seconds since epoch
    */
    apr_time_exp_gmt(&tms,resource->info->orangefs_finfo.mtime*1000000);
    sprintf(value,"%s, %.2d %s %d %.2d:%.2d:%.2d GMT",
            apr_day_snames[tms.tm_wday],
            tms.tm_mday, apr_month_snames[tms.tm_mon],
            tms.tm_year + 1900,
            tms.tm_hour, tms.tm_min, tms.tm_sec);
    break;

  case DAV_PROPID_getcontenttype:
    if (resource->info->orangefs_finfo.filetype == APR_DIR) {
      sprintf(value,"%s","httpd/unix-directory");
    }
    break;

  default:
    return DAV_PROP_INSERT_NOTDEF;
  }

  /* dav_get_liveprop_info sez it will map the provider-local namespace 
     into a global namespace index...
  */
  global_ns = dav_get_liveprop_info(propid,&dav_orangefs_liveprop_group,&info);

  if (what == DAV_PROP_INSERT_VALUE) {
    s=apr_psprintf(resource->pool,"<lp%d:%s>%s</lp%d:%s>"DEBUG_CR,
                   global_ns,info->name,value,global_ns,info->name);
  } 
  else if (what == DAV_PROP_INSERT_NAME) {
    s=apr_psprintf(resource->pool,"<lp%d:%s/>"DEBUG_CR,global_ns,info->name);
  } 
  else {
    s=apr_psprintf(resource->pool,"<D:supported-live-property D:name=\"%s\" "
                   "D:namespace=\"%s\"/>"DEBUG_CR,
                   info->name,dav_orangefs_namespace_uris[info->ns]);
  }
  apr_text_append(resource->pool,phdr,s);

  /* "we inserted what was asked for" */
  return what;
}

static int dav_orangefs_is_writable(const dav_resource *resource, int propid)
{
  const dav_liveprop_spec *info;

  (void)dav_get_liveprop_info(propid,&dav_orangefs_liveprop_group,&info);
  return info->is_writable;
}


static const dav_hooks_liveprop dav_hooks_liveprop_orangefs =
{
  dav_orangefs_insert_prop,
  dav_orangefs_is_writable,
  dav_orangefs_namespace_uris,
  NULL, // patch_validate
  NULL, // patch_exec
  NULL, // patch_commit
  NULL  // patch_rollback
};

/* stolen from fs/lock.c */
static const char *dav_orangefs_get_supportedlock(const dav_resource *resource)
{
  if (debug_orangefs) {
      DBG0("dav_orangefs_get_supportedlock");
  }

  static const char supported[] = DEBUG_CR
    "<D:lockentry>" DEBUG_CR
    "<D:lockscope><D:exclusive/></D:lockscope>" DEBUG_CR
    "<D:locktype><D:write/></D:locktype>" DEBUG_CR
    "</D:lockentry>" DEBUG_CR;
//    "</D:lockentry>" DEBUG_CR
//    "<D:lockentry>" DEBUG_CR
//    "<D:lockscope><D:shared/></D:lockscope>" DEBUG_CR
//    "<D:locktype><D:write/></D:locktype>" DEBUG_CR
//    "</D:lockentry>" DEBUG_CR;

  return supported;
}

/* stolen from fs/lock.c */
static dav_error * dav_orangefs_parse_locktoken(apr_pool_t *p,
                                                const char *char_token,
                                                dav_locktoken **locktoken_p)
{
  dav_locktoken *locktoken;

  if (debug_orangefs) {
      DBG0("dav_orangefs_parse_locktoken");
  }

  if (ap_strstr_c(char_token, "opaquelocktoken:") != char_token) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(p, HTTP_BAD_REQUEST, DAV_ERR_LOCK_UNK_STATE_TOKEN,
                         "The lock token uses an unknown State-token "
                         "format and could not be parsed.");
#else
    return dav_new_error(p, HTTP_BAD_REQUEST, DAV_ERR_LOCK_UNK_STATE_TOKEN,0,
                         "The lock token uses an unknown State-token "
                         "format and could not be parsed.");
#endif
  }

  char_token += 16;

  locktoken = apr_pcalloc(p,sizeof(*locktoken));

  if (apr_uuid_parse(&locktoken->uuid, char_token)) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(p, HTTP_LOCKED, DAV_ERR_LOCK_PARSE_TOKEN,
                         "The opaquelocktoken has an incorrect format "
                         "and could not be parsed.");
#else
    return dav_new_error(p, HTTP_LOCKED, DAV_ERR_LOCK_PARSE_TOKEN,0,
                         "The opaquelocktoken has an incorrect format "
                         "and could not be parsed.");
#endif
  }

  *locktoken_p = locktoken;

  return NULL;
}

static int dav_orangefs_compare_locktoken(
    const dav_locktoken *lt1,
    const dav_locktoken *lt2)
{
    return memcmp(&(lt1)->uuid, &(lt2)->uuid, sizeof((lt1)->uuid));
}

static const char *dav_orangefs_format_locktoken(
    apr_pool_t *p,
    const dav_locktoken *locktoken)
{
  char buf[APR_UUID_FORMATTED_LENGTH + 1];

  if (debug_orangefs) {
      DBG0("dav_orangefs_format_locktoken");
  }

  apr_uuid_format(buf, &locktoken->uuid);
  return apr_pstrcat(p, "opaquelocktoken:", buf, NULL);
}

static dav_error * dav_orangefs_open_lockdb(request_rec *r, int ro, int force,
                                            dav_lockdb **lockdb)
{

  if (debug_orangefs) {
      DBG0("dav_orangefs_open_lockdb");
  }

  *lockdb = apr_pcalloc(r->pool,sizeof(*lockdb));
  (*lockdb)->hooks = &dav_hooks_locks_orangefs;
  (*lockdb)->ro = ro;

  if (force) {
    /* do some stuff? */
  }

  return NULL;
}

static void dav_orangefs_close_lockdb(dav_lockdb *lockdb)
{
  if (debug_orangefs) {
      DBG0("dav_orangefs_close_lockdb");
  }

}

static dav_error * dav_orangefs_remove_locknull_state(dav_lockdb *lockdb,
                     const dav_resource *resource)
{
  if (debug_orangefs) {
      DBG0("dav_orangefs_remove_locknull_state");
  }

}

static dav_error * dav_orangefs_create_lock(dav_lockdb *lockdb,
                                      const dav_resource *resource,
                                      dav_lock **lock)
{
  dav_locktoken *locktoken;
  dav_lock *lock_buf;
  dav_resource_private *orangefsInfo;
  int rc;

  if (debug_orangefs) {
      DBG1("dav_orangefs_create_lock: %s", resource->uri);
  }

  lock_buf = apr_pcalloc(resource->pool,sizeof(*lock_buf));
  locktoken = apr_pcalloc(resource->pool,sizeof(*locktoken));

  lock_buf->locktoken = locktoken;
  apr_uuid_get(&locktoken->uuid);

  *lock = lock_buf;

  return NULL;
}

/* lots of hits on this routine... */
static dav_error * dav_orangefs_get_locks(dav_lockdb *lockdb,
                                         const dav_resource *resource,
                                         int calltype, dav_lock **locks)
{
  int rc;
  char *lockstr='\0';
  dav_lock *lock = NULL;
  dav_resource_private *orangefsInfo;

  if (debug_orangefs) {
    DBG1("dav_orangefs_get_locks: %s",resource->uri);
  }

  orangefsInfo = (dav_resource_private *)apr_pmemdup(resource->pool,
		(const void *)resource->info,sizeof(*orangefsInfo));

  lockstr = apr_pcalloc(resource->pool,BUFSIZ);

  /* check to see if there's a direct lock, unless this resource
     does not exist yet (locknull resource)
  */
  if (resource->info->orangefs_finfo.filetype == APR_NOFILE) {
    rc = 1;
  } else {
    rc=orangeAttrs("get",resource->uri,resource->pool,orangefsInfo,
                   DAVLOCK_PROPERTY,lockstr);
  }

  if (!rc) {
    if (getLockHelper(&lock,lockstr,resource)) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(resource->pool,HTTP_BAD_REQUEST,
                           DAV_ERR_LOCK_PARSE_TOKEN,
                           apr_psprintf(resource->pool,"locktoken for "
                           "%s could not be parsed",resource->info->Uri));
#else
      return dav_new_error(resource->pool,HTTP_BAD_REQUEST,
                           DAV_ERR_LOCK_PARSE_TOKEN,0,
                           apr_psprintf(resource->pool,"locktoken for "
                           "%s could not be parsed",resource->info->Uri));
#endif
    }
    if (debug_orangefs) {
      DBG1("dav_orangefs_get_locks: %s has a direct lock",resource->uri);
    }

  } else if ((resource->info->orangefs_finfo.filetype == APR_REG) ||
             (resource->info->orangefs_finfo.filetype == APR_NOFILE)) {
    /* if we're dealing with a file or a locknull resource, check to see 
       if there's an indirect lock - that is, check to see if the parent 
       directory has a lock of depth=1 or more...
    */

    rc=orangeAttrs("get",resource->info->DirName,resource->pool,orangefsInfo,
                   DAVLOCK_PROPERTY,lockstr);
    if (!rc) {
      if (getLockHelper(&lock,lockstr,resource)) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
        return dav_new_error(resource->pool,HTTP_BAD_REQUEST,
                             DAV_ERR_LOCK_PARSE_TOKEN,
                             apr_psprintf(resource->pool,"locktoken for "
                             "%s could not be parsed",resource->info->Uri));
#else
        return dav_new_error(resource->pool,HTTP_BAD_REQUEST,
                             DAV_ERR_LOCK_PARSE_TOKEN,0,
                             apr_psprintf(resource->pool,"locktoken for "
                             "%s could not be parsed",resource->info->Uri));
#endif
      }
      if (debug_orangefs) {
        DBG1("dav_orangefs_get_locks: %s has an indirect lock",resource->uri);
      }
    }
  }

  *locks = lock;

  return NULL;
}

int getLockHelper(dav_lock **lock, char *lockstr, 
                          const dav_resource *resource) 
{
  dav_lock *lk;
  char *locktoken=NULL;
  time_t timeout=0;
  int depth=0;
  char *owner=NULL;

  if (debug_orangefs) {
    DBG1("getLockHelper: %s",resource->uri);
  }

  /* obtain the different logical parts embedded in the lock property */
  if (lockstr) {
    lockStringToParts(lockstr,strlen(lockstr),
                      &locktoken,&timeout,&depth,&owner);
  }

  if (timeout > time(0)) {
    /* found unexpired direct lock... */
    lk = apr_pcalloc(resource->pool,sizeof(*lk));
    lk->rectype = DAV_LOCKREC_DIRECT;
    lk->scope =  DAV_LOCKSCOPE_EXCLUSIVE;
    lk->type = DAV_LOCKTYPE_WRITE;
    lk->timeout = timeout;
    lk->depth = depth;
    if (owner[0]!='X') lk->owner = owner;
    lk->locktoken = 
      apr_pcalloc(resource->pool,sizeof(*lk->locktoken));
    if (apr_uuid_parse((apr_uuid_t *)&lk->locktoken->uuid,locktoken)) {
      return 1;
    }
    /* leave auth_user null for now... */
    *lock = lk;

  } else {
    /* found an expired direct lock, get rid of it... */
    orangeAttrs("remove",resource->info->Uri,resource->pool,resource->info,
                 DAVLOCK_PROPERTY,NULL);
  }

  return 0;
}

static dav_error * dav_orangefs_find_lock(dav_lockdb *lockdb,
                                          const dav_resource *resource,
                                          const dav_locktoken *locktoken,
                                          int partial_ok,
                                          dav_lock **lock)
{
  if (debug_orangefs) {
      DBG0("dav_orangefs_find_lock");
  }

  return NULL;
}

static dav_error * dav_orangefs_has_locks(dav_lockdb *lockdb,
                                          const dav_resource *resource,
                                          int *locks_present)
{
  int rc;
  char *value='\0';
  dav_resource_private *orangefsInfo;

  if (debug_orangefs) {
      DBG0("dav_orangefs_has_locks");
  }

  orangefsInfo = (dav_resource_private *)apr_pmemdup(resource->pool,
		(const void *)resource->info,sizeof(*orangefsInfo));

  value = apr_pcalloc(resource->pool,BUFSIZ);
  rc=orangeAttrs("get",resource->uri,resource->pool,
                 orangefsInfo,DAVLOCK_PROPERTY,value);
  
  if (rc) {
    *locks_present = FALSE;
  } else {
    *locks_present = TRUE;
  }

  return NULL;
}

/* make_indirect=0    direct lock..
   make_indirect=1    indirect lock...

   resource->info->orangefs_finfo.filetype      1    file
   resource->info->orangefs_finfo.filetype      2    dir
*/
static dav_error * dav_orangefs_append_locks(dav_lockdb *lockdb,
                                       const dav_resource *resource,
                                       int make_indirect,
                                       const dav_lock *lock)
{
  char *lockStr;
  char *buf;
  int rc;
  PVFS_sysresp_create *resp_create;
  dav_resource_private *orangefsInfo;

  if (debug_orangefs) {
      DBG3("dav_orangefs_append_locks: "
           "lock record type:%d: make_indirect:%d: filetype:%d:",
           lock->rectype,make_indirect,
           resource->info->orangefs_finfo.filetype);
  }

  /* nothing to do if we're dealing with an indirect lock on a file... */
  if ((make_indirect) && (resource->info->orangefs_finfo.filetype == 1)) {
    return NULL;
  }

  if (lock->scope == DAV_LOCKSCOPE_SHARED) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(resource->pool,HTTP_UNPROCESSABLE_ENTITY,
                         DAV_ERR_LOCK_SAVE_LOCK,
                         apr_psprintf(resource->pool,"shared lock on "
                         "%s failed, shared locks not supported",
                         resource->uri));
#else
    return dav_new_error(resource->pool,HTTP_UNPROCESSABLE_ENTITY,
                         DAV_ERR_LOCK_SAVE_LOCK,0,
                         apr_psprintf(resource->pool,"shared lock on "
                         "%s failed, shared locks not supported",
                         resource->uri));
#endif
  }

  lockStr = apr_pcalloc(resource->pool,BUFSIZ);

  buf = apr_pcalloc(resource->pool,APR_UUID_FORMATTED_LENGTH + 1);
  apr_uuid_format(buf, &lock->locktoken->uuid);

  if (lock->owner) {
    sprintf(lockStr,"%s %d %d %s",
            buf,(int)lock->timeout,lock->depth,lock->owner);
  } else {
    sprintf(lockStr,"%s %d %d %s",buf,(int)lock->timeout,lock->depth,"X");
  }

  orangefsInfo = apr_pcalloc(resource->pool,sizeof(*orangefsInfo));
  orangefsInfo->mountPoint =
    apr_pstrdup(resource->pool,resource->info->mountPoint);

  credCopy(resource->info->credential,&(orangefsInfo->credential),resource->pool);

  if ((resource->info->orangefs_finfo.filetype == APR_NOFILE) &&
      (orangeAttrs("stat",resource->uri,resource->pool,orangefsInfo,0,0))) {
    /* Unless resource->uri got created sometime during this request,
       it must be a locknull resource, create it and stamp it.
    */
    resp_create = apr_pcalloc(resource->pool,sizeof(*resp_create));
    if (rc=orangeCreate((dav_resource *)resource,resp_create)) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(resource->pool,HTTP_INTERNAL_SERVER_ERROR,
                           DAV_ERR_LOCK_SAVE_LOCK,
                           apr_psprintf(resource->pool,
                           "dav_orangefs_append_locks: creation of "
                           "unmapped URL :%s: failed, rc:%d:",
                           resource->uri,rc));
#else
      return dav_new_error(resource->pool,HTTP_INTERNAL_SERVER_ERROR,
                           DAV_ERR_LOCK_SAVE_LOCK,0,
                           apr_psprintf(resource->pool,
                           "dav_orangefs_append_locks: creation of "
                           "unmapped URL :%s: failed, rc:%d:",
                           resource->uri,rc));
#endif
    }

    rc=orangeAttrs("set",resource->uri,resource->pool,orangefsInfo,
                    LOCKNULL_PROPERTY,"");
    if (rc) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "dav_orangefs_append_locks: unable to set LOCKNULL_PROPERTY on %s",
      resource->uri);
    }

  }

  rc=orangeAttrs("set",resource->uri,resource->pool,orangefsInfo,
                  DAVLOCK_PROPERTY,lockStr);
  if (rc) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(resource->pool,HTTP_BAD_REQUEST,
                         DAV_ERR_LOCK_SAVE_LOCK,
                         apr_psprintf(resource->pool,"locktoken for "
                         "%s could not be saved",resource->uri));
#else
    return dav_new_error(resource->pool,HTTP_BAD_REQUEST,
                         DAV_ERR_LOCK_SAVE_LOCK,0,
                         apr_psprintf(resource->pool,"locktoken for "
                         "%s could not be saved",resource->uri));
#endif
  }

  return NULL;
}

static dav_error * dav_orangefs_remove_lock(dav_lockdb *lockdb,
                                            const dav_resource *resource,
                                            const dav_locktoken *locktoken)
{
  dav_resource_private *orangefsInfo;

  if (debug_orangefs) {
      DBG0("dav_orangefs_remove_lock");
  }

  orangefsInfo = (dav_resource_private *)apr_pmemdup(resource->pool,
		(const void *)resource->info,sizeof(*orangefsInfo));

  if (locktoken != NULL) {
    orangeAttrs("remove",resource->uri,resource->pool,
                orangefsInfo,DAVLOCK_PROPERTY,NULL);
  }

  /* 
    "A resource created with a LOCK is empty but otherwise behaves in
     every way as a normal resource...

     A locked empty resource... SHOULD NOT disappear when its lock goes 
     away (clients must therefore be responsible for cleaning up their 
     own mess, as with any other operation or any non-empty resource)."

                                                           RFC 4918 7.3 

    In the deprecated Lock-Null model, "the server removes the lock-null 
    resource entirely ... if its lock goes away before it is converted to
    a regular resource."

    Important DAV applications, such as dav2fs, seem to have been implemented
    with the deprecated Lock-Null model. Using vi, for example, under dav2fs 
    is a problem for repositories that try to stick exclusively on the 
    RFC 4918 model. The vi "swap" files (filename.swp and filename.swpx, etc)
    are created with LOCKs to their unmapped URLs, and they are removed
    with UNLOCKs. The UNLOCKs don't remove the "swap" files when the
    repository sticks to the RFC 4918 model, causing vi to freak out
    about multiple active edit sessions to the same file, as well as 
    other problems...

    So... 

      - RFC 4918 says SHOULD NOT, not MUST NOT...

      - we'd be dopes to favor pedantry over interoperation with
        popular DAV clients...

    We're going to nuke LOCKNULL resources on UNLOCK... we could probably
    make this configurable instead of hard-coded if it needed to be.
  */
 if (resource->info->locknull) {
    orangeRemove((char*)resource->info->BaseName,
                        *resource->info->parent_ref,
                        resource->info->credential,
                        resource->pool);
  }

  return NULL;
}

static dav_error *dav_orangefs_refresh_locks(dav_lockdb *lockdb,
                                             const dav_resource *resource,
                                             const dav_locktoken_list *ltl,
                                             time_t new_time,
                                             dav_lock **locks)
{
  dav_resource_private *orangefsInfo;
  char *locktoken=NULL;
  time_t timeout=0;
  int depth=0;
  char *owner=NULL;
  char *lockstr;
  int rc;
  dav_lock *newlock;

  if (debug_orangefs) {
    DBG1("dav_orangefs_refresh_locks :%s:",resource->uri);
  }

  *locks = NULL;

  lockstr = apr_pcalloc(resource->pool,PVFS_MAX_XATTR_VALUELEN);

  orangefsInfo = apr_pcalloc(resource->pool,sizeof(*orangefsInfo));
  orangefsInfo->mountPoint =
    apr_pstrdup(resource->pool,resource->info->mountPoint);

  credCopy(resource->info->credential,&(orangefsInfo->credential),resource->pool);

  if (!(rc=orangeAttrs("get",resource->uri,resource->pool,
                  orangefsInfo,DAVLOCK_PROPERTY,lockstr))) {
    
    lockStringToParts(lockstr,strlen(lockstr),
                      &locktoken,&timeout,&depth,&owner);

    lockstr = apr_pcalloc(resource->pool,PVFS_MAX_XATTR_VALUELEN);

    sprintf(lockstr,"%s %d %d %s",locktoken,new_time,depth,owner);

    newlock = apr_pcalloc(resource->pool,sizeof(*newlock));
    newlock->rectype = DAV_LOCKREC_DIRECT;
    newlock->scope =  DAV_LOCKSCOPE_EXCLUSIVE;
    newlock->type = DAV_LOCKTYPE_WRITE;
    newlock->timeout = new_time;
    newlock->depth = depth;
    if (owner[0]!='X') {
      newlock->owner = owner;
    }
    newlock->locktoken = 
      apr_pcalloc(resource->pool,sizeof(*newlock->locktoken));
    if (apr_uuid_parse((apr_uuid_t *)&newlock->locktoken->uuid,locktoken)) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(resource->pool,HTTP_BAD_REQUEST,
                           DAV_ERR_LOCK_PARSE_TOKEN,
                           apr_psprintf(resource->pool,"refresh locktoken for "
                           "%s could not be parsed",resource->uri));
#else
      return dav_new_error(resource->pool,HTTP_BAD_REQUEST,
                           DAV_ERR_LOCK_PARSE_TOKEN,0,
                           apr_psprintf(resource->pool,"refresh locktoken for "
                           "%s could not be parsed",resource->uri));
#endif
    }

    newlock->next = *locks;
    *locks = newlock;

    rc=orangeAttrs("set",resource->uri,resource->pool,orangefsInfo,
                    DAVLOCK_PROPERTY,lockstr);

  }

  return NULL;

}

/* the DAV core thinks that the object associated with "start_resource"
   is locked, either directly or indirectly... lookup_resource's job is to
   identify the object responsible for the lock and return a filled out
   dav_resource structure for it.

   If start_resource is associated with /a/b/c/d, then d, c, b and a 
   are probed for a DAVLOCK_PROPERTY property corresponding to 
   locktoken. This "backwards probing" stops when the resource with the
   desired DAVLOCK_PROPERTY is located. If "d" has the lock,
   then start_resource is the dav_resource we need to return, otherwise
   a dav_resource structure for the resource holding the lock must
   be filled out and returned.

   The fs repository doesn't implement lookup_resource. The DAV core's
   dav_get_direct_resource subroutine checks to see if lookup_resource
   is implemented for whichever repository is being used. If lookup_resource
   is not implemented, dav_get_direct_resource does the job with
   the repository's find_lock and get_parent_resource hooks and
   a big loop...
   
*/
static dav_error *dav_orangefs_lookup_resource(dav_lockdb *lockdb,
                                        const dav_locktoken *locktoken,
                                        const dav_resource *start_resource,
                                        const dav_resource **resource)
{

  char dirName[PVFS_NAME_MAX];
  char baseName[PVFS_NAME_MAX];
  char thisResourceName[PVFS_NAME_MAX];
  dav_resource *thisDavResource;
  int rc;
  char *lockstr;
  char *mylocktoken='\0';
  char thelocktoken[APR_UUID_FORMATTED_LENGTH + 1];
  time_t timeout=0;
  int depth=0;
  char *owner=NULL;
  dav_resource_private *orangefsInfo;

  if (debug_orangefs) {
      DBG0("dav_orangefs_lookup_resource");
  }

  *resource = NULL;

  apr_uuid_format(thelocktoken,&locktoken->uuid);

  lockstr = apr_pcalloc(start_resource->pool,PVFS_MAX_XATTR_VALUELEN);

  thisDavResource = (dav_resource *)start_resource;
  memset(thisResourceName,0,PVFS_NAME_MAX);
  strcpy(thisResourceName,thisDavResource->uri);
  baseName[0] = 'x'; 
  while (baseName[0] != '\0') {

    orangefsInfo = apr_pcalloc(thisDavResource->pool,sizeof(*orangefsInfo));
    orangefsInfo->mountPoint =
      apr_pstrdup(thisDavResource->pool,thisDavResource->info->mountPoint);
    credCopy(thisDavResource->info->credential,
             &(orangefsInfo->credential),
             thisDavResource->pool);

    rc=orangeAttrs("get",thisResourceName,thisDavResource->pool,
                   orangefsInfo,DAVLOCK_PROPERTY,lockstr);
    if (!rc) {
      /* obtain the different logical parts embedded in the 
         lock property we just found...
       */
      lockStringToParts(lockstr,strlen(lockstr),
                        &mylocktoken,&timeout,&depth,&owner);

      if (timeout > time(0)) {
        /* found an unexpired lock... */
        if (!strcmp(thelocktoken,mylocktoken)) {
          /* got a locktoken match, this is the lock we're looking for... */
          if (!strcmp(thisResourceName,start_resource->uri)) { 
            /* direct lock */
            *resource = start_resource;
            break;
          } else { 
            /* indirect lock... */
            thisDavResource = 
              apr_pcalloc(thisDavResource->pool,sizeof(*thisDavResource));
            thisDavResource->type = DAV_RESOURCE_TYPE_REGULAR;
            thisDavResource->exists = 1;
            thisDavResource->collection = 1;
            thisDavResource->uri = 
              apr_pstrdup(thisDavResource->pool,thisResourceName);
            thisDavResource->hooks = start_resource->hooks;
            thisDavResource->pool = start_resource->pool;
            thisDavResource->info = orangefsInfo;
            *resource = thisDavResource;
            break;
          }
        }
      } else {
        /* found an expired lock, get rid of it... */
        orangeAttrs("remove",thisResourceName,thisDavResource->pool,
                    NULL,DAVLOCK_PROPERTY,NULL);
      }
    }
    dirnameBasename(thisResourceName,dirName,baseName);
    memset(thisResourceName,0,PVFS_NAME_MAX);
    strcpy(thisResourceName,dirName);
  }

  return NULL;
}

static const dav_hooks_locks dav_hooks_locks_orangefs = 
{
  dav_orangefs_get_supportedlock,
  dav_orangefs_parse_locktoken,
  dav_orangefs_format_locktoken,
  dav_orangefs_compare_locktoken,
  dav_orangefs_open_lockdb,
  dav_orangefs_close_lockdb,
  dav_orangefs_remove_locknull_state,
  dav_orangefs_create_lock,
  dav_orangefs_get_locks,
  dav_orangefs_find_lock,
  dav_orangefs_has_locks,
  dav_orangefs_append_locks,
  dav_orangefs_remove_lock,
  dav_orangefs_refresh_locks,
  dav_orangefs_lookup_resource,
  NULL /* ctx */
};

void dav_orangefs_insert_all_liveprops(request_rec *r, 
                                       const dav_resource *resource,
                                       dav_prop_insert what, 
                                       apr_text_header *phdr)
{
  int i;
  int liveprops[] = { DAV_PROPID_creationdate,
                      DAV_PROPID_getcontentlength,
                      DAV_PROPID_getlastmodified,
                      DAV_PROPID_getetag,
                      DAV_PROPID_getcontenttype,
                      0 };

  if (debug_orangefs) {
    DBG0("dav_orangefs_insert_all_liveprops");
  }

  /* bail if not this repository */
  if (resource->hooks != &dav_hooks_repository_orangefs) {
    return;
  } 

  if (!resource->exists) {
    return;
  }

  for (i=0;liveprops[i]!=0;i++) {
    (void) dav_orangefs_insert_prop(resource,liveprops[i],what,phdr);
  }

  return;
}

/*
 * This module can be an apache auth module if we configure it
 * to be one in httpd.conf. When we are the auth module, we
 * will check the user/password with PAM. When we get to this
 * function we have the user and password entered into our http auth box.
 */

static authn_status check_password(request_rec *r,
                                   const char *user,
                                   const char *password)
{
  int rc;

  if (debug_orangefs) {
    DBG2("orangefs: user:%s: password:%s:",user,password);
  }

  strcpy(pass,password);

  if ((rc = pam_start("httpd",user,&pc,&ph))) {
    DBG1("pam_start failed, rc:%d:\n",rc);
    rc = AUTH_DENIED;
    goto out;
  }

  if ((rc = pam_authenticate(ph,0))) {
    DBG1("pam_authenticate failed, rc:%d:",rc);
    rc = AUTH_DENIED;
    goto out;
  }

  pam_end(ph,rc);

  rc = AUTH_GRANTED;

out:

  return rc;

}

static int auth_conv(int num_msg, struct pam_message **msg,
                    struct pam_response **response, void *appdata_ptr)
{
  /*
   * this memory gets freed way down in pam_end, it would
   * hose things up if we got this memory from an apr pool.
   */
  *response =
     (struct pam_response *) malloc(sizeof (struct pam_response));

  if(*response == (struct pam_response *)0) {
    DBG0("pam conv: malloc failed!");
    return PAM_BUF_ERR;
  }

  (*response)->resp = strdup(pass);
  (*response)->resp_retcode = 0;
  return PAM_SUCCESS;
}

/*
 * The DAV module, when used as an auth module, doesn't support digest mode,
 * so we only have check_password here.
 */
static const authn_provider authn_this_module_provider =
{
  &check_password,
};

static void register_hooks(apr_pool_t *p) {

  if (debug_orangefs) {
    DBG0("orangefs: register_hooks");
  }

  /* apache auth */
  ap_register_provider(p, AUTHN_PROVIDER_GROUP, "this_module", "0",
                         &authn_this_module_provider);

  ap_hook_post_config(dav_orangefs_init_handler,NULL,NULL,APR_HOOK_LAST);

  dav_hook_find_liveprop(dav_orangefs_find_liveprop,NULL,NULL,APR_HOOK_MIDDLE);
  dav_hook_insert_all_liveprops(dav_orangefs_insert_all_liveprops,NULL,NULL,
                                APR_HOOK_MIDDLE );

  /* register the namespace URIs */
  dav_register_liveprop_group(p,&dav_orangefs_liveprop_group);

  /* PROVIDER_NAME is set by developer in Makefile.am */
  dav_register_provider(p,PROVIDER_NAME,&dav_orangefs_provider);
}

module DAV_DECLARE_DATA dav_orangefs_module = {
  STANDARD20_MODULE_STUFF,
  dav_orangefs_create_dir_config,
  dav_orangefs_merge_dir_config,
  dav_orangefs_create_server_config,
  dav_orangefs_merge_server_config,
  dav_orangefs_cmds,
  register_hooks,
};

/* --------------------------  utilities ------------------------*/

/* orangeAttrs is the swiss army knife for attributes. When a dav
   function needs a parent ref or a ref for a file or directory, 
   or stat attributes (file size, creation date, ...), or to manipulate
   arbitrarily-created extended attributes, orangeAttrs does the work.
 
   There are numerous places in orangeAttrs where a series of 
   orangeFS "system" calls need to be made in order to arrive at
   the desired state. Tests show that when the same part of the 
   filesystem is being manipulated/changed by more than one process, 
   intermediate results during the execution of these series could 
   become void before the completion of the series, leading to a failure 
   of any one of the processes to arrive at the desired state. 
 
   When these series fail, a single retry is triggered. These retries
   are almost never triggered (except in the case of "get", see below)
   and when they are triggered, they usually prevent the failure of
   reasonable requests. 
 
   There are several other helper functions that resolve to 
   important non-attribute related orangeFS "system" calls, such
   as orangeRead, orangeWrite, orangeRemove and orangeCopy. 
   Tests, as yet, don't turn up any of these important calls failing
   because of void attributes. Since file locks are implemented with 
   extended attributes, it seems to be the case that once a DAV process 
   successfully navigates orangeAttrs, atomic operations needed for
   file creation, deletion and modification are protected by DAV locks.
 
   RFC 4918 doesn't require DAV servers to implement locking, so
   there could be some cowboy DAV clients out there that don't use
   locking... the best of luck to them... <g>
*/

int orangeAttrs(char *action, char *resource, apr_pool_t *pool,
                dav_resource_private *drp, char *xName, char *xValue) {

  int rc;
  char *orangefs_path;
  PVFS_fs_id orange_fs_id;
  PVFS_credential *credential;
  PVFS_sysresp_lookup resp_lookup;
  PVFS_sysresp_lookup resp_lookup2;
  char keyName[BUFSIZ];
  char valBuf[BUFSIZ];
  PVFS_ds_keyval key;
  PVFS_ds_keyval val={0};
  PVFS_sysresp_getattr getattr_response;
  struct allPNs *head, *thisPN;
  int lastSlashPos=0;
  char dirName[PVFS_NAME_MAX];
  char baseName[PVFS_NAME_MAX];
  int i;
  int attributeMask=PVFS_ATTR_SYS_ALL_NOSIZE;
  int parentRefRetryCounter=0;
  int ref1RetryCounter=0;
  int ref2RetryCounter=0;
  int commonRetryCounter=0;
  struct passwd *pw=NULL;
  uid_t uid;
  gid_t gid;
  PVFS_object_ref pRef;
  PVFS_object_ref ref;
  dav_orangefs_server_conf *conf;

  /* Fail hard if we don't have a drp. */
  if (drp == 0) {
    DBG0("orangefs: orangeAttrs have no drp!");
    abort();
  }

  if (debug_orangefs) {
    if (xName && xValue) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "orangeAttrs: resource:%s: action:%s: xName:%s: xValue:%s:",
      resource,action,xName,xValue);
    } else if (xName) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "orangeAttrs: resource:%s: action:%s: xName:%s:",
      resource,action,xName);
    } else if (xValue) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "orangeAttrs: resource:%s: action:%s: xValue:%s:",
      resource,action,xValue);
    } else {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "orangeAttrs: resource:%s: action:%s:",resource,action);
    }
  }

  if (!resource) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "orangeAttrs: can't process a null resource...");
    return(-1);
  }

  dirnameBasename(resource,dirName,baseName);

  /* if dirName is shorter than drp->mountPoint, we must be doing something
     with the mountpoint. The mountpoint might be right at the root,
     or it might be any number of directories deep...
  */
  if ((drp) && (drp->mountPoint)) {
    if (strlen(dirName) < strlen(drp->mountPoint)) {
      memset(dirName,0,PVFS_NAME_MAX);
      strcpy(dirName,drp->mountPoint);
      memset(baseName,0,PVFS_NAME_MAX);
    }
  } 

  /* If we have a drp when we get here, and it is not filled out,
     we want to put our credential in it. If we don't have a drp
     when we get here, we'll have to generate some credential.
     If the request record's user field isn't NULL, it probably points
     to an authenticated user, so we'll need to look for an appropriate
     UID/GID pair to put in the credential...
  */

  if ((drp) && (drp->credential)) {
    /* already have a credential */
    credCopy(drp->credential,&credential,pool);
  } else if (drp) {
    conf = (dav_orangefs_server_conf *)ap_get_module_config(
           drp->r->server->module_config, &dav_orangefs_module);
    if (conf == 0)
      return EACCES;
    if ((drp->r) && (drp->r->user)) {
      pw = getpwnam(drp->r->user);
      if (pw) {
        /* local auth */
        if (credInit(&credential,
                     pool,
                     conf->certpath,
                     drp->r->user,
                     pw->pw_uid,
                     pw->pw_gid))
          return EACCES;
        credCopy(credential, &(drp->credential), pool);
      } else if (apr_table_get(drp->r->subprocess_env,
                              "AUTHENTICATE_UIDNUMBER")) {
        /* ldap auth */
        uid = strtoimax((char *)apr_table_get(drp->r->subprocess_env,
                                              "AUTHENTICATE_UIDNUMBER"),
                        0, 0);
        gid = strtoimax((char *)apr_table_get(drp->r->subprocess_env,
                                              "AUTHENTICATE_GIDNUMBER"),
                        0, 0);
        if (credInit(&credential, pool, conf->certpath, drp->r->user,
                     uid, gid))
          return EACCES;
        credCopy(credential, &(drp->credential), pool);
      } else {
        /*
         * Somehow this guy got authenticated and we can't
         * find his uid/gid. Set him to "nobody", or whatever
         * we ended up setting as the default in the server
         * config...
         */
        uid = strtoimax(conf->uid, 0, 0);
        gid = strtoimax(conf->gid, 0, 0);
        if (credInit(&credential, pool, conf->certpath, drp->r->user,
                     uid, gid))
          return EACCES;
        credCopy(credential, &(drp->credential), pool);
      }
    } else {
      if (credInit(&credential, pool, conf->certpath, "nobody",
                                      strtoimax(conf->uid, 0, 0),
                                      strtoimax(conf->gid, 0, 0)))
        return EACCES;
      credCopy(credential, &(drp->credential), pool);
    }
  }

  tryAgain: /* see comments below. */

  orangefs_path = apr_pcalloc(pool,PVFS_NAME_MAX);

  memset(&resp_lookup,0,sizeof(resp_lookup));

  /* get the parent ref... */

  rc = PVFS_util_resolve(dirName,&orange_fs_id,orangefs_path,PVFS_NAME_MAX);
  if (rc != 0) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "orangeAttrs: PVFS_util_resolve failed on %s, rc:%d:",dirName,rc);
    return(rc);
  }
  /* OrangeFS does not like empty paths. */
  if (*orangefs_path == 0) {
    orangefs_path[0] = '/';
    orangefs_path[1] = 0;
  }

  if (debug_orangefs) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "orangeAttrs: PVFS_sys_lookup: fs_id:%d: orangefs_path:%s: pid:%d: ",
      orange_fs_id,orangefs_path,getpid());
  }

  rc = PVFS_sys_lookup(orange_fs_id,orangefs_path,credential,
                           &resp_lookup,PVFS2_LOOKUP_LINK_FOLLOW,NULL);
  if (rc) {
    parentRefRetryCounter++;
    if (parentRefRetryCounter == 1) {
      PINT_ncache_finalize();
      PINT_ncache_initialize();
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "orangeAttrs: PVFS_sys_lookup failed on %s, had to try again",
         orangefs_path);
      goto tryAgain;
    } else {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "orangeAttrs: PVFS_sys_lookup failed on %s, fs_id:%d:, rc:%d:",
       orangefs_path,orange_fs_id,rc);
    return(rc);
    }
  }

  /* remember the parent ref, full pathname, and dirName and baseName,
     and also the parents permission attributes in case the actual
     resource we're digging for doesn't exist yet.

     initialize the removeWalk and copyWalk flags... 
  */
  if (drp) {
    drp->ref = apr_pcalloc(pool,sizeof(*drp->ref));
    drp->ref->handle = resp_lookup.ref.handle;
    drp->ref->fs_id = resp_lookup.ref.fs_id;
    drp->ref->__pad1 = resp_lookup.ref.__pad1;

    drp->Uri = apr_pcalloc(pool,1);
    drp->Uri = apr_pstrcat(pool,drp->Uri,resource,NULL);

    drp->DirName = apr_pcalloc(pool,1);
    drp->DirName = apr_pstrcat(pool,drp->DirName,dirName,NULL);

    drp->BaseName = apr_pcalloc(pool,1);
    drp->BaseName = apr_pstrcat(pool,drp->BaseName,baseName,NULL);

    pRef.handle = drp->ref->handle;
    pRef.fs_id = drp->ref->fs_id;
    pRef.__pad1 = drp->ref->__pad1;
    memset(&getattr_response,0, sizeof(getattr_response));
    if ((rc = PVFS_sys_getattr(pRef,attributeMask,credential,
                               &getattr_response, NULL)) >= 0) {
      drp->perms=getattr_response.attr.perms;
      drp->uid=getattr_response.attr.owner;
      drp->gid=getattr_response.attr.group;
    } else {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "orangeAttrs: PVFS_sys_getattr 1 failed on handle %d, rc:%d:",
         pRef.handle,rc);
      return(rc);
    }

    drp->removeWalk = 0;
    drp->copyWalk = 0;
  }

  /* get a ref for the file/leaf-dir if there is one... */
  if (baseName[0] != '\0') {

    if (debug_orangefs) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "orangeAttrs: PVFS_sys_ref_lookup: fs_id:%d: "
      "baseName:%s:  handle:%d: pid:%d:",
       orange_fs_id,baseName,resp_lookup.ref.handle,getpid());
    }

    memset(&resp_lookup2,0,sizeof(resp_lookup2));
    rc = PVFS_sys_ref_lookup(orange_fs_id,(char*)baseName,resp_lookup.ref,
                             credential,&resp_lookup2,
                             PVFS2_LOOKUP_LINK_FOLLOW,PVFS_HINT_NULL);

    /* retry if not found, maybe some other process has made a change
       that rendered our cache invalid... 
    */
    if (rc) {
      ref1RetryCounter++;
      if (ref1RetryCounter == 1) {
        PINT_ncache_finalize();
        PINT_ncache_initialize();
        ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
          "orangeAttrs: PVFS_sys_ref_lookup failed on %s/%s, had to try again",
          dirName,baseName);
        goto tryAgain;
      }
    }

    resp_lookup.ref.handle = resp_lookup2.ref.handle;
    resp_lookup.ref.fs_id = resp_lookup2.ref.fs_id;

    /* if there was a file/leaf-dir, then it is "The Ref", and we'll
       remember the parent_ref as such...
    */

    if ((!rc) && (drp)) {
      drp->parent_ref = apr_pcalloc(pool,sizeof(*drp->parent_ref));
      drp->parent_ref->handle = drp->ref->handle;
      drp->parent_ref->fs_id = drp->ref->fs_id;
      drp->parent_ref->__pad1 = drp->ref->__pad1;

      drp->ref->handle = resp_lookup.ref.handle;
      drp->ref->fs_id = resp_lookup.ref.fs_id;
      drp->ref->__pad1 = resp_lookup.ref.__pad1;

      ref.handle = drp->ref->handle;
      ref.fs_id = drp->ref->fs_id;
      ref.__pad1 = drp->ref->__pad1;
      memset(&getattr_response,0, sizeof(getattr_response));
      if ((rc = PVFS_sys_getattr(ref,attributeMask,credential,
                                 &getattr_response, NULL)) >= 0) {
        drp->perms=getattr_response.attr.perms;
        drp->uid=getattr_response.attr.owner;
        drp->gid=getattr_response.attr.group;
      } else {
        ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
          "orangeAttrs: PVFS_sys_getattr 2 failed on handle %d, rc:%d:",
           ref.handle,rc);
        return(rc);
      }
  
    }

  }

  if (xName) {
    memset(keyName,0,BUFSIZ);
    strcpy(keyName,"user.pvfs2.");
    strcat(keyName,xName);
    key.buffer = keyName;
    key.buffer_sz =strlen(keyName)+1;
  } else {
    xName=apr_pstrdup(pool,"");
  }

  /* don't let them look at the properties if they can't read the file. */
  if ( (!strcmp(action,"get")) || (!strcmp(action,"enum")) ) {
    if (!canRead(drp->uid,drp->gid,drp->perms,
                 credential)){
      if (debug_orangefs) {
        ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
          "orangeAttrs: PVFS_sys_geteattr permission denied on handle %d",
           ref.handle);
        ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
          "orangeAttrs: drp->uid:%d: drp->gid:%d: drp->perms:%o: "
          "credential.uid:%d: credential.gid:%d:",
           drp->uid,drp->gid,drp->perms,credential->userid,
credential->group_array[0]);
      }
      return(HTTP_FORBIDDEN);
    }
  }

  if (!strcmp(action,"stat")) {

    /* attributeMask is set to avoid getting file size by default, since
       getting file size is an expensive operation (all orangefs servers
       must be visited). Usually, for a stat, xValue is NULL. Up in 
       dav_orangefs_insert_prop, when propid is set to getcontentlength,
       we make a special "stat" call to orangeAttrs with xValue set to 
       "get size" as a trigger to change the attributeMask to include 
       file size.
    */
    if (xValue) {
      attributeMask=PVFS_ATTR_SYS_ALL_NOHINT;
    }

    if (rc) {
      /* rc indicates that PVFS_sys_ref_lookup failed above...
         we're probably statting for a file that doesn't exist, which
         isn't an error in itself, but there's no need to look for its
         attributes... hold onto the raw file name for later...
       */
      if (drp) {
        drp->orangefs_finfo.filetype=APR_NOFILE;
      }
      return rc;
    }

    rc = getStatAttrs(&resp_lookup.ref,attributeMask,credential,
                       &getattr_response,drp,resource);

    if (rc) {
      /* getStatAttrs probably shouldn't have failed... on a real active
         system it is possible that the ref in this client process's cache
         is invalid because of some change made to the orangefs filesystem
         in some other process... running Litmus uncovered this, as it threw 
         enough requests at Apache (in a prefork environment) that they
         were handled by different processes. One of the processes 
         got a ref for a directory, then another process deleted and 
         recreated the directory. When the first process tried to use the
         ref, it was invalid. We can run finalize and init to clear out the
         ncache and give it one more try, and it will probably work...
      */
       ref2RetryCounter++;
       if (ref2RetryCounter == 1) {
         PINT_ncache_finalize();
         PINT_ncache_initialize();
         ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
           "orangeAttrs: getStatAttrs failed on %s, had to try again",resource);
         goto tryAgain;
       }

    }
    
  } else if (!strcmp(action,"set")) {

    /* rc was set above by PVFS_sys_ref_lookup. */
    if (rc) {
      commonRetryCounter++;
      if (commonRetryCounter == 1) {
        PINT_ncache_finalize();
        PINT_ncache_initialize();
        ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
          "orangeAttrs: PVFS_sys_ref_lookup failed on %s, had to try again",
           resource);
        goto tryAgain;
      } else {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "orangeAttrs: set: PVFS_sys_ref_lookup failed on %s, rc:%d:",
         resource,rc);
      return(rc);
      }
    }

    if (xValue == NULL) {
      val.buffer = NULL;
      val.buffer_sz = 0;
    } else {
      val.buffer = xValue;
      val.buffer_sz = strlen(xValue)+1;
    }

    if (debug_orangefs) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "orangeAttrs: PVFS_sys_seteattr: handle:%d: key:%s: val:%s: pid:%d: ",
      resp_lookup.ref.handle,key.buffer,val.buffer,getpid());
    }

    rc = PVFS_sys_seteattr(resp_lookup.ref,credential,&key,&val,0,NULL);
    if (rc) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "orangeAttrs: PVFS_sys_seteattr failed on %s, %s, rc:%d:",
        orangefs_path,xName,rc);
      return(rc);
    }

  } else if (!strcmp(action,"remove")) {

    if (rc) {
      commonRetryCounter++;
      if (commonRetryCounter == 1) {
        PINT_ncache_finalize();
        PINT_ncache_initialize();
        ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
          "orangeAttrs: PVFS_sys_ref_lookup failed on %s, had to try again",
           resource);
        goto tryAgain;
      } else {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "orangeAttrs: remove: PVFS_sys_ref_lookup failed on %s, rc:%d:",
         resource,rc);
      return(rc);
      }
    }

    if (debug_orangefs) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "orangeAttrs: PVFS_sys_deleattr: handle:%d: key:%s: pid:%d: ",
      resp_lookup.ref.handle,key.buffer,getpid());
    }

    rc = PVFS_sys_deleattr(resp_lookup.ref,credential,&key,NULL);
    if (rc) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "orangeAttrs: PVFS_sys_deleattr failed on %s, %s, rc:%d:",
        resource,xName,rc);
    }

  } else if (!strcmp(action,"get")) {

    /* In testing, most retries almost never happen, and when they do, it
       usually saves the day. Here in "get", lots of needless retries happen, 
       but the occasional retry prevents the improper failure of a
       request. Figuring out how to prevent needless retries here would
       be a big win here.
    */
  
    if (rc) {
      commonRetryCounter++;
      if (commonRetryCounter == 1) {
        PINT_ncache_finalize();
        PINT_ncache_initialize();
        ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
          "orangeAttrs: get: PVFS_sys_ref_lookup failed on %s while looking "
          "for %s, had to try again",resource,xName);
        goto tryAgain;
      } else {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "orangeAttrs: get: PVFS_sys_ref_lookup failed on %s, rc:%d:",
         resource,rc);
      return(rc);
      }
    }

    memset(valBuf,0,BUFSIZ);
    val.buffer = valBuf;
    val.buffer_sz = BUFSIZ;

    if (debug_orangefs) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "orangeAttrs: PVFS_sys_geteattr: handle:%d: key:%s: pid:%d: ",
      resp_lookup.ref.handle,key.buffer,getpid());
    }

    rc = PVFS_sys_geteattr(resp_lookup.ref,credential,&key,&val,NULL);
    if (rc) {
      commonRetryCounter++;
      if (commonRetryCounter == 1) {
        PINT_ncache_finalize();
        PINT_ncache_initialize();
        ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
          "orangeAttrs: PVFS_sys_geteattr failed on %s, had to try again",
           resource);
        goto tryAgain;
      } else { 
        ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
          "orangeAttrs: PVFS_sys_geteattr failed on %s, %s, rc:%d:",
          orangefs_path,xName,rc);
      }
    }
    /* need to set xValue even if PVFS_sys_geteattr fails... */
    strcpy(xValue,val.buffer);

  } else if (!strcmp(action,"enum")) {

    if (rc) {
      commonRetryCounter++;
      if (commonRetryCounter == 1) {
        PINT_ncache_finalize();
        PINT_ncache_initialize();
        ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
          "orangeAttrs: PVFS_sys_ref_lookup failed on %s, had to try again",
           resource);
        goto tryAgain;
      } else {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "orangeAttrs: enum: PVFS_sys_ref_lookup failed on %s, rc:%d:",
         resource,rc);
      return(rc);
      }
    }

    /* When we're enumerating through xattrs, there's a chicken and
       egg problem... we don't know how many there are going to be
       until we've looked... so we allocate enough space to obtain 
       10 (nkey) xattrs, and PVFS_sys_listeattr communicates through 
       "token" whether or not there's more xattrs left to look at after 
       each time we've looked at (up to) 10 of them...
     */
    PVFS_sysresp_listeattr resp_listeattr;
    char str_buf[PVFS_NAME_MAX] = {0};
    int nkey = 10;
    PVFS_ds_keyval *keyp;
    int i;
    PVFS_ds_position token = PVFS_ITERATE_START;

    keyp = apr_pcalloc(pool,sizeof(*keyp) * nkey);

    for (i=0;i<nkey;i++) {
      keyp[i].buffer_sz = KEYBUFSIZ;
      keyp[i].buffer = apr_pcalloc(pool,KEYBUFSIZ);
    }

    resp_listeattr.key_array = keyp;
    head = drp->this = thisPN = apr_pcalloc(pool,sizeof(*head));

    for (;;) {

      if (debug_orangefs) {
        ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "orangeAttrs: PVFS_sys_listeattr: handle:%d: token:%d: pid:%d:",
        resp_lookup.ref.handle,token,getpid());
      }

      rc = PVFS_sys_listeattr(resp_lookup.ref,token,nkey,
                              credential,&resp_listeattr,NULL);

      for (i=0;i<resp_listeattr.nkey;i++) {
        if (strncmp(keyp[i].buffer,"system.pvfs2.",strlen("system.pvfs2."))) {
          thisPN->propertyName = apr_pcalloc(pool,1);
          thisPN->propertyName = 
            apr_pstrcat(pool, 
                        thisPN->propertyName,
                        keyp[i].buffer+strlen("user.pvfs2."),
                        NULL);
          thisPN->next = apr_pcalloc(pool,sizeof(*head));
          thisPN->next->propertyName = apr_pcalloc(pool,1);
          thisPN = thisPN->next;
        }
      }

      if (resp_listeattr.token == PVFS_ITERATE_START-1) {
        break;
      } else {
        token = resp_listeattr.token;
      }

    }

  } else {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"orangeAttrs: can't get here");
  }

  return(rc);
}

/* helper routine for getting "stat" attributes (file size, last modification
   time, stuff like that...) "stat" attributes generally are "live properties".
*/
int getStatAttrs(PVFS_object_ref *ref,
                 int attributeMask,
                 PVFS_credential *credential,
                 PVFS_sysresp_getattr *getattr_response,
                 dav_resource_private *drp,
                 char *resource)
{
  int rc;
  char keyName[BUFSIZ];
  char valBuf[BUFSIZ];
  PVFS_ds_keyval key;
  PVFS_ds_keyval val={0};

  if (debug_orangefs) {
      DBG1("orangefs: getStatAttrs %s",resource);
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "getStatAttrs: PVFS_sys_getattr: handle:%d: pid:%d:",
        ref->handle,getpid());
  }

  /* take advantage of having the ref and fetch back the locknull
     resource twinkie to keep from having to make a special "get" call
     to orangeAttrs to get it if we need it...
  */
  memset(keyName,0,BUFSIZ);
  strcpy(keyName,"user.pvfs2.");
  strcat(keyName,LOCKNULL_PROPERTY);
  key.buffer = keyName;
  key.buffer_sz =strlen(keyName)+1;

  memset(valBuf,0,BUFSIZ);
  val.buffer = valBuf;
  val.buffer_sz = BUFSIZ;

  rc = PVFS_sys_geteattr(*ref,credential,&key,&val,NULL);
  if (!rc) {
    drp->locknull=1;
  }

  memset(getattr_response,0,sizeof(*getattr_response));
  rc = PVFS_sys_getattr(*ref,attributeMask,credential,getattr_response,NULL);
  if (rc == 0) {

    if (getattr_response->attr.mask & PVFS_ATTR_SYS_TYPE) {
      if (getattr_response->attr.objtype & PVFS_TYPE_METAFILE) {
        drp->orangefs_finfo.filetype=APR_REG; // regular file
      } else if (getattr_response->attr.objtype & PVFS_TYPE_DIRECTORY) {
        drp->orangefs_finfo.filetype=APR_DIR; // directory
      }
    }

    /* size, mtime and ctime can be used for the getcontentlength,
       creationdate and getlastmodified live properties respectively.
       An object's orangefs handle is used to make an etag for the resource.
       It might be that both the handle and the fs_id should be used?
       etag - Section 13.3.3 of RFC2616 and Section 8.6 of RFC4918
    */
    drp->orangefs_finfo.size=0;
    if (getattr_response->attr.mask & PVFS_ATTR_SYS_SIZE) {
      drp->orangefs_finfo.size = getattr_response->attr.size;
    }
  
    drp->orangefs_finfo.mtime=0;
    if (getattr_response->attr.mask & PVFS_ATTR_SYS_MTIME) {
      drp->orangefs_finfo.mtime = getattr_response->attr.mtime;
    }
  
    drp->orangefs_finfo.ctime=0;
    if (getattr_response->attr.mask & PVFS_ATTR_SYS_CTIME) {
      drp->orangefs_finfo.ctime = getattr_response->attr.ctime;
    }

    /* remember these for permissions checking... */
    drp->perms=getattr_response->attr.perms;
    drp->uid=getattr_response->attr.owner;
    drp->gid=getattr_response->attr.group;
  
  } else {

    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "getStatAttrs: PVFS_sys_getattr failed on %d, rc:%d:",ref->handle,rc);
  }

  return rc;
}

/* Take the character string from a lock property and convert its parts into
   their proper types...
*/
void lockStringToParts(char *lockstr, int length, char **locktoken, 
                       time_t *timeout, int *depth, char **owner) {
  int i;
  char *timeOutPointer;
  char *depthPointer;

  *locktoken = lockstr;
  for (i=0;i<length;i++) {
    if (lockstr[i] == ' ') break;
  }
  if (i >= length) {
    *locktoken = NULL;
  } else {
    lockstr[i++]='\0';
  }

  timeOutPointer = lockstr+i;
  for (i=0;i<length;i++) {
    if (lockstr[i] == ' ') break;
  }
  lockstr[i++]='\0';

  depthPointer = lockstr+i;
  for (i=0;i<length;i++) {
    if (lockstr[i] == ' ') break;
  }
  lockstr[i++]='\0';

  *timeout =  strtoimax(timeOutPointer,NULL,0);

  *depth = strtoimax(depthPointer,NULL,0);

  *owner = lockstr+i;

}

static int are_they_the_same(const dav_resource *res1,
                             const dav_resource *res2)
{

  if (!res1->info->ref) return 0;
  if (!res2->info->ref) return 0;

  if ((res1->info->ref->handle == res2->info->ref->handle) &&
      (res1->info->ref->fs_id == res2->info->ref->fs_id)) {
    return 1;
  } else {
    return 0;
  }
}

/* deconstruct the contents of "resource" into a directory path and
   a base name. If it turns out that the only slash in "resource"
   is the first one, there is no base name.

   resource=/mountpoint            dirName=/mountpoint       baseName=NULL
   resource=/mountpoint/file       dirName=/mountpoint       baseName=file
   resource=/mountpoint/dir/file   dirName=/mountpoint/dir   baseName=file

*/
void dirnameBasename(char *resource, char *dirName, char *baseName) {
  int i;
  int lastSlashPos=0;

  if (debug_orangefs) {
      DBG1("dirnameBasename: %s",resource);
  }

  for (i=0;resource[i] != '\0';i++) {
    if (resource[i] == '/') {
      lastSlashPos=i;
    }
  }

  memset(dirName,0,PVFS_NAME_MAX);
  memset(baseName,0,PVFS_NAME_MAX);
  strcpy(dirName,resource);
  if (lastSlashPos != 0) {
    /* there is a file name... */
    dirName[lastSlashPos] = '\0';
    strcpy(baseName,resource+lastSlashPos+1);
  }
}

/* read bytes from an orangefs file... */
int orangeRead(PVFS_object_ref *ref,PVFS_credential *credential, 
               char *uri, void *buffer, int64_t bufsize, int64_t offset)
{
  PVFS_Request mem_req;
  PVFS_sysresp_io resp_io;
  int bytesRead;
  int rc;

  if (debug_orangefs) {
    DBG1("orangeRead: buffersize:%d:",bufsize);
  }


  if ((rc=PVFS_Request_contiguous(bufsize,PVFS_BYTE,&mem_req))) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "orangeRead: PVFS_Request_contiguous failed, rc=:%d:",rc);
    return -1;
  }

  if (debug_orangefs) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "orangeRead: PVFS_sys_read: handle:%d: pid:%d:",
        ref->handle,getpid());
  }

  if ((rc=PVFS_sys_read(*(ref),PVFS_BYTE,offset,(void *)buffer,
                        mem_req,credential,&resp_io,
                        PVFS_HINT_NULL))) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
      "orangeRead: PVFS_sys_read failed at offset :%ld: "
      "on file :%s:, rc=:%d:",offset,uri,rc);
    PVFS_Request_free(&mem_req);
    return -1;
  }

  PVFS_Request_free(&mem_req);

  return(resp_io.total_completed);
}

/* write bytes to an orangefs file... */
int orangeWrite(const void *buf, apr_size_t bufsize, apr_pool_t *pool, 
                PVFS_object_ref *ref, int64_t offset,
                PVFS_credential *credential) 
{
  int rc;
  PVFS_Request mem_req;
  PVFS_sysresp_io resp_io;

  if (debug_orangefs) {
    DBG1("orangeWrite: buffersize:%d:",bufsize);
  }

  if ((rc=PVFS_Request_contiguous(bufsize,PVFS_BYTE,&mem_req))) {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"orangeWrite: "
      "PVFS_Request_contiguous, rc:%d:",rc);
    return -1;
  }

  if (debug_orangefs) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "orangeWrite: PVFS_sys_write: handle:%d: pid:%d:",
        ref->handle,getpid());
  }

  if ((rc=PVFS_sys_write(*(ref),PVFS_BYTE,(PVFS_offset)offset,(char *)buf,
                         mem_req,credential,&resp_io,PVFS_HINT_NULL)))
  {
    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"orangeWrite: "
      "PVFS_sys_write, rc:%d: handle:%d:",rc,ref->handle);
    PVFS_Request_free(&mem_req);
    return -1;
  }

  PVFS_Request_free(&mem_req);

  return(bufsize);
}

/* remove an orangefs file or (empty) directory... */
static dav_error *orangeRemove(char *object, 
                               PVFS_object_ref ref, 
                               PVFS_credential *credential,
                               apr_pool_t *pool)
{
  int rc;

  if (debug_orangefs) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "orangeRemove: PVFS_sys_remove: handle:%d: object:%s:  pid:%d:"
        "uid:%d:  gid:%d:",ref.handle,object,getpid(),credential->userid,
        credential->group_array[0]);
  }

  if ((rc=PVFS_sys_remove(object,ref,credential,NULL)))
  {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(pool,HTTP_MULTI_STATUS,0,
                         apr_psprintf(pool,"orangeRemove: "
                         "could not remove %s, rc:%d:",object,rc));
#else
    return dav_new_error(pool,HTTP_MULTI_STATUS,0,0,
                         apr_psprintf(pool,"orangeRemove: "
                         "could not remove %s, rc:%d:",object,rc));
#endif
  } else {
    return NULL;
  }
}

/* make an orangefs directory... */
int orangeMkdir(dav_resource *resource) {
  PVFS_sys_attr       attr;
  PVFS_sysresp_mkdir  resp_mkdir; 
  int rc;
  dav_resource_private *orangefsInfo;

  if (debug_orangefs) {
      DBG1("orangeMkdir: %s ",resource->uri);
  }

  memset(&attr,0,sizeof(attr));
  memset(&resp_mkdir,0,sizeof(resp_mkdir));

  attr.owner = resource->info->credential->userid;
  attr.group = resource->info->credential->group_array[0];
  attr.atime = time(NULL);
  attr.mtime = attr.atime;
  attr.ctime = attr.atime;
  attr.perms = S_IRWXU;
  attr.mask = (PVFS_ATTR_SYS_ALL_SETABLE);

  if (debug_orangefs) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "orangeMkdir: PVFS_sys_mkdir: BaseName:%s: handle:%d: pid:%d:",
        resource->info->BaseName,resource->info->ref->handle,getpid());
  }

  /* resource->info->ref is the parent-ref of the directory we are about to
     create...
  */
  rc=PVFS_sys_mkdir((char*)resource->info->BaseName,
                    *resource->info->ref,
                    attr,
                    resource->info->credential,
                    &resp_mkdir,
                    NULL);

  /* if we get a "No such file or directory" return code, then
     probably something has caused the ref we have to become invalid,
     we'll make one stab at getting a correct ref (if there is one)
     and re-running PVFS-sys_mkdir...
  */
  if (rc == -1073741826) {
      PINT_ncache_finalize();
      PINT_ncache_initialize();
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "orangeMkdir: PVFS_sys_mkdir failed, re-establish the ref and "
        "make one retry..."); 
      orangefsInfo = apr_pcalloc(resource->info->r->pool,sizeof(*orangefsInfo));
      orangefsInfo->mountPoint =
        apr_pstrdup(resource->pool,resource->info->mountPoint);
      orangeAttrs("stat",
                  resource->info->DirName,
                  resource->info->r->pool,orangefsInfo,NULL,NULL);
      rc=PVFS_sys_mkdir((char*)resource->info->BaseName,
                        *orangefsInfo->ref,
                        attr,
                        resource->info->credential,
                        &resp_mkdir,
                        NULL);
  }

  /* remember this ref, use it for going into and out of recursion... */
  resource->info->recurse_ref =
    apr_pcalloc(resource->pool,sizeof(*resource->info->recurse_ref));
  resource->info->recurse_ref->handle = resp_mkdir.ref.handle;
  resource->info->recurse_ref->fs_id = resp_mkdir.ref.fs_id;

  return rc;

}

/* copy an orangefs file from src to dst... */
static dav_error *orangeCopy(const dav_resource *src, dav_resource *dst) {
  int rc;
  int bytesRead;
  void *buffer;
  int64_t offset=0;
  PVFS_sysresp_create *resp_create;
  PVFS_object_ref *targetRef;
  dav_orangefs_dir_conf *dconf;

  if (debug_orangefs) {
    DBG2("orangeCopy: %s ---> %s",src->uri,dst->uri);
    DBG1("  dst->info->BaseName:%s:",dst->info->BaseName);
    if (dst->info->ref) {
      DBG1("  dst->info->ref->handle:%d:",dst->info->ref->handle);
    }
    if (dst->info->parent_ref) {
      DBG1("  dst->info->parent_ref->handle:%d:",dst->info->ref->handle);
    }
  }

  if (!canRead(src->info->uid,src->info->gid,src->info->perms,
               src->info->credential)){
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(src->pool,HTTP_FORBIDDEN,0,
      "orangeCopy: permission denied");
#else
    return dav_new_error(src->pool,HTTP_FORBIDDEN,0,0,
      "orangeCopy: permission denied");
#endif
  }

  dconf = ap_get_module_config(src->info->r->per_dir_config,
                               &dav_orangefs_module);

  resp_create = apr_pcalloc(dst->pool,sizeof(*resp_create));

  if (rc=orangeCreate(dst,resp_create)) {

    ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,"orangeCopy: "
                 "PVFS_sys_create failed on :%s:, rc:%d:",
                 dst->uri,rc);
    /* we need some kind of mapper that can map common to 
       orangeFS return codes to HTTP status codes...
     */
    if (rc == -1073741882) {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(dst->pool,HTTP_UNAUTHORIZED,0,
                           "orangeCopy: PVFS_sys_create failed");
#else
      return dav_new_error(dst->pool,HTTP_UNAUTHORIZED,0,0,
                           "orangeCopy: PVFS_sys_create failed");
#endif
    } else {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(dst->pool,HTTP_INTERNAL_SERVER_ERROR,0,
                           "orangeCopy: PVFS_sys_create failed");
#else
      return dav_new_error(dst->pool,HTTP_INTERNAL_SERVER_ERROR,0,0,
                           "orangeCopy: PVFS_sys_create failed");
#endif
    }
  }
  targetRef = apr_pcalloc(dst->pool,sizeof(PVFS_object_ref));
  targetRef->handle = resp_create->ref.handle;
  targetRef->fs_id = resp_create->ref.fs_id;

  buffer = apr_pcalloc(src->pool,dconf->readBufSize);
  bytesRead = 0;

  /* priming read... */
  if ((bytesRead = orangeRead(src->info->ref,src->info->credential,
                              (char *)src->uri,
                              buffer,dconf->readBufSize,offset)) < 0 )
  {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
    return dav_new_error(src->pool,HTTP_INTERNAL_SERVER_ERROR,0,NULL);
#else
    return dav_new_error(src->pool,HTTP_INTERNAL_SERVER_ERROR,0,0,NULL);
#endif
  }

  while (bytesRead > 0) {
    if (orangeWrite(buffer,bytesRead,dst->pool,targetRef,
                    offset,dst->info->credential) < 0)
    {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(src->pool,HTTP_INTERNAL_SERVER_ERROR,0,NULL);
#else
      return dav_new_error(src->pool,HTTP_INTERNAL_SERVER_ERROR,0,0,NULL);
#endif
    }
    offset+=bytesRead;

    memset(buffer,0,dconf->readBufSize);
    if ((bytesRead = orangeRead(src->info->ref,src->info->credential,
                                (char *)src->uri,
                                buffer,dconf->readBufSize,offset)) < 0 )
    {
#if AP_SERVER_MAJORVERSION_NUMBER == 2 && AP_SERVER_MINORVERSION_NUMBER <= 2
      return dav_new_error(src->pool,HTTP_INTERNAL_SERVER_ERROR,0,NULL);
#else
      return dav_new_error(src->pool,HTTP_INTERNAL_SERVER_ERROR,0,0,NULL);
#endif
    }
  }

  /* copy over the properties... */
  orangePropCopy(src,dst);

  return NULL;
}

void orangePropCopy(const dav_resource *src, dav_resource *dst) {
  struct allPNs *this;
  char *propertyName;
  char *value='\0';
  PVFS_handle handleCopy;

  if (debug_orangefs) {
    DBG2("orangePropCopy: :%s: ---> :%s:",src->uri,dst->uri);
  }

  value = apr_pcalloc(src->pool,BUFSIZ);
  if (orangeAttrs("enum",(char *)src->uri,src->pool,src->info,NULL,NULL)) {
    /* The enum didn't do right for some reason, probably permissions...  */
    return;
  }
  this = src->info->this;

  if (this->propertyName) {
    while (this->propertyName[0] != '\0') {
      orangeAttrs("get",(char *)src->uri,src->pool,src->info,
                  this->propertyName,value);
  
      if (debug_orangefs) {
        DBG2("orangePropCopy: propertyName:%s: value:%s:",
             this->propertyName,value);
      }

      /* avoid copying DAVLOCK_PROPERTYs and LOCKNULL_PROPERTYs... */
      if (strcmp(this->propertyName,DAVLOCK_PROPERTY) && 
          strcmp(this->propertyName,LOCKNULL_PROPERTY)) {
        /* if we get down in here orangeAttrs is going to manipulate
           the drp in such a way that dst->info->ref will be changed to
           reflect the newly created file instead of the newly
           created directory we're filling, and there will be a
           train wreck as we try to continue on... we'll just hang
           onto the unmangled handle and restore it when we return
           from orangeAttrs...
         */
        handleCopy = dst->info->ref->handle;
        orangeAttrs("set",(char *)dst->uri,dst->pool,dst->info,
                    this->propertyName,value);
        dst->info->ref->handle = handleCopy;
      }

      this = this->next;
      memset(value,0,BUFSIZ);
    }
  }

}

int orangeCreate(dav_resource *resource,PVFS_sysresp_create *resp_create) {
  PVFS_sys_attr *attributes;
  int rc;

  if (debug_orangefs) {
    DBG1("orangeCreate: :%s:",resource->uri);
  }

  attributes = apr_pcalloc(resource->pool,sizeof(*attributes));
  attributes->owner = resource->info->credential->userid;
  attributes->group = resource->info->credential->group_array[0];
  attributes->atime = time(NULL);
  attributes->mtime = attributes->atime;
  attributes->ctime = attributes->atime;
  attributes->mask = PVFS_ATTR_SYS_ALL_SETABLE;
  attributes->perms |= PVFS_U_EXECUTE+PVFS_U_WRITE+PVFS_U_READ;

  if (debug_orangefs) {
      ap_log_error(APLOG_MARK,APLOG_ERR,0,NULL,
        "orangeCreate: PVFS_sys_create: BaseName:%s: handle:%d: pid:%d:",
        resource->info->BaseName,resource->info->ref->handle,getpid());
  }

  rc=PVFS_sys_create((char*)resource->info->BaseName,
                     (PVFS_object_ref)*resource->info->ref,
                     *attributes,resource->info->credential,
                     0,resp_create,
                     PVFS_SYS_LAYOUT_DEFAULT,PVFS_HINT_NULL);

  if (debug_orangefs) {
    DBG2("orangeCreate: :%s: rc:%d:",resource->uri,rc);
  }

  return(rc);
}

/* Return 0 on success. */
int credInit(PVFS_credential **new, apr_pool_t *p, const char *certpath,
             const char *username, uid_t uid, gid_t gid)
{
  char uids[8], gids[8];
  dav_orangefs_server_conf *conf;
  *new = apr_pcalloc(p, sizeof(PVFS_credential));
  snprintf(uids, 8, "%hu", uid);
  snprintf(gids, 8, "%hu", gid);
  if (debug_orangefs)
    DBG2("credInit: uids:%s: gids:%s:",uids,gids);
  if (certpath)
    return PVFS_util_gen_credential(uids, gids, 0,
      apr_pstrcat(p, certpath, "/", username, "-key.pem"), 
      apr_pstrcat(p, certpath, "/", username, "-cert.pem"), *new);
  return PVFS_util_gen_credential(uids, gids, 0, 0, 0, *new);
}

void credCopy(PVFS_credential *src, PVFS_credential **new, apr_pool_t *p) {
  int i;

  if (debug_orangefs) {
    DBG1("credCopy: :%d:",src->userid);
  }
  *new = apr_pcalloc(p,sizeof(PVFS_credential));
  /* PVFS2.9 has a new function to do this. */
  PINT_copy_credential(src, *new);
}

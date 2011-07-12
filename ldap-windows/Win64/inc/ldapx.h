/*
 * $Novell: ldapx.h,v 1.35 2003/05/13 13:55:09 $
 ******************************************************************************
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
 ******************************************************************************
 */
#ifndef LDAPX_H
#define LDAPX_H

#include <ldap.h>
#include <ldap_ds_constants.h>
#include <ldap_events.h>

LDAP_BEGIN_DECL

/* LDAP Extension OIDS */
#define NLDAP_NDS_TO_LDAP_EXTENDED_REQUEST                 "2.16.840.1.113719.1.27.100.1"
#define NLDAP_NDS_TO_LDAP_EXTENDED_REPLY                   "2.16.840.1.113719.1.27.100.2"
#define NLDAP_SPLIT_PARTITION_EXTENDED_REQUEST             "2.16.840.1.113719.1.27.100.3"
#define NLDAP_SPLIT_PARTITION_EXTENDED_REPLY               "2.16.840.1.113719.1.27.100.4"
#define NLDAP_MERGE_PARTITION_EXTENDED_REQUEST             "2.16.840.1.113719.1.27.100.5"
#define NLDAP_MERGE_PARTITION_EXTENDED_REPLY               "2.16.840.1.113719.1.27.100.6"
#define NLDAP_ADD_REPLICA_EXTENDED_REQUEST                 "2.16.840.1.113719.1.27.100.7"
#define NLDAP_ADD_REPLICA_EXTENDED_REPLY                   "2.16.840.1.113719.1.27.100.8"
#define NLDAP_REFRESH_SERVER_REQUEST                       "2.16.840.1.113719.1.27.100.9"
#define NLDAP_REFRESH_SERVER_REPLY                         "2.16.840.1.113719.1.27.100.10"
#define NLDAP_REMOVE_REPLICA_EXTENDED_REQUEST              "2.16.840.1.113719.1.27.100.11"
#define NLDAP_REMOVE_REPLICA_EXTENDED_REPLY                "2.16.840.1.113719.1.27.100.12"
#define NLDAP_PARTITION_ENTRY_COUNT_EXTENDED_REQUEST       "2.16.840.1.113719.1.27.100.13"
#define NLDAP_PARTITION_ENTRY_COUNT_EXTENDED_REPLY         "2.16.840.1.113719.1.27.100.14"
#define NLDAP_CHANGE_REPLICA_TYPE_EXTENDED_REQUEST         "2.16.840.1.113719.1.27.100.15"
#define NLDAP_CHANGE_REPLICA_TYPE_EXTENDED_REPLY           "2.16.840.1.113719.1.27.100.16"
#define NLDAP_GET_REPLICA_INFO_EXTENDED_REQUEST            "2.16.840.1.113719.1.27.100.17"
#define NLDAP_GET_REPLICA_INFO_EXTENDED_REPLY              "2.16.840.1.113719.1.27.100.18"
#define NLDAP_LIST_REPLICAS_EXTENDED_REQUEST               "2.16.840.1.113719.1.27.100.19"
#define NLDAP_LIST_REPLICAS_EXTENDED_REPLY                 "2.16.840.1.113719.1.27.100.20"
#define NLDAP_RECEIVE_ALL_UPDATES_EXTENDED_REQUEST         "2.16.840.1.113719.1.27.100.21"
#define NLDAP_RECEIVE_ALL_UPDATES_EXTENDED_REPLY           "2.16.840.1.113719.1.27.100.22"
#define NLDAP_SEND_ALL_UPDATES_EXTENDED_REQUEST            "2.16.840.1.113719.1.27.100.23"
#define NLDAP_SEND_ALL_UPDATES_EXTENDED_REPLY              "2.16.840.1.113719.1.27.100.24"
#define NLDAP_REQUEST_PARTITION_SYNC_EXTENDED_REQUEST      "2.16.840.1.113719.1.27.100.25"
#define NLDAP_REQUEST_PARTITION_SYNC_EXTENDED_REPLY        "2.16.840.1.113719.1.27.100.26"
#define NLDAP_REQUEST_SCHEMA_SYNC_EXTENDED_REQUEST         "2.16.840.1.113719.1.27.100.27"
#define NLDAP_REQUEST_SCHEMA_SYNC_EXTENDED_REPLY           "2.16.840.1.113719.1.27.100.28"
#define NLDAP_ABORT_PARTITION_OPERATION_EXTENDED_REQUEST   "2.16.840.1.113719.1.27.100.29"
#define NLDAP_ABORT_PARTITION_OPERATION_EXTENDED_REPLY     "2.16.840.1.113719.1.27.100.30"
#define NLDAP_GET_BIND_DN_EXTENDED_REQUEST                 "2.16.840.1.113719.1.27.100.31"
#define NLDAP_GET_BIND_DN_EXTENDED_REPLY                   "2.16.840.1.113719.1.27.100.32"
#define NLDAP_GET_EFFECTIVE_PRIVILEGES_EXTENDED_REQUEST    "2.16.840.1.113719.1.27.100.33"
#define NLDAP_GET_EFFECTIVE_PRIVILEGES_EXTENDED_REPLY      "2.16.840.1.113719.1.27.100.34"
#define NLDAP_SET_REPLICA_FILTER_EXTENDED_REQUEST          "2.16.840.1.113719.1.27.100.35"
#define NLDAP_SET_REPLICA_FILTER_EXTENDED_REPLY            "2.16.840.1.113719.1.27.100.36"
#define NLDAP_GET_REPLICA_FILTER_EXTENDED_REQUEST          "2.16.840.1.113719.1.27.100.37"
#define NLDAP_GET_REPLICA_FILTER_EXTENDED_REPLY            "2.16.840.1.113719.1.27.100.38"
#define NLDAP_CREATE_ORPHAN_PARTITION_EXTENDED_REQUEST     "2.16.840.1.113719.1.27.100.39"
#define NLDAP_CREATE_ORPHAN_PARTITION_EXTENDED_REPLY       "2.16.840.1.113719.1.27.100.40"
#define NLDAP_REMOVE_ORPHAN_PARTITION_EXTENDED_REQUEST     "2.16.840.1.113719.1.27.100.41"
#define NLDAP_REMOVE_ORPHAN_PARTITION_EXTENDED_REPLY       "2.16.840.1.113719.1.27.100.42"
#define NLDAP_DNS_TO_X500_DN_EXTENDED_REQUEST              "2.16.840.1.113719.1.27.100.101"
#define NLDAP_DNS_TO_X500_DN_EXTENDED_REPLY                "2.16.840.1.113719.1.27.100.102"
#define NLDAP_GET_EFFECTIVE_PRIVILEGES_LIST_EXTENDED_REQUEST    "2.16.840.1.113719.1.27.100.103"
#define NLDAP_GET_EFFECTIVE_PRIVILEGES_LIST_EXTENDED_REPLY      "2.16.840.1.113719.1.27.100.104"


/* Deprecated #defines for LDAP Extension OIDS */ 
#define NLDAP_CREATE_NAMING_CONTEXT_EXTENDED_REQUEST            "2.16.840.1.113719.1.27.100.3"
#define NLDAP_CREATE_NAMING_CONTEXT_EXTENDED_REPLY              "2.16.840.1.113719.1.27.100.4"
#define NLDAP_MERGE_NAMING_CONTEXT_EXTENDED_REQUEST             "2.16.840.1.113719.1.27.100.5"
#define NLDAP_MERGE_NAMING_CONTEXT_EXTENDED_REPLY               "2.16.840.1.113719.1.27.100.6"
#define NLDAP_NAMING_CONTEXT_ENTRY_COUNT_EXTENDED_REQUEST       "2.16.840.1.113719.1.27.100.13"
#define NLDAP_NAMING_CONTEXT_ENTRY_COUNT_EXTENDED_REPLY         "2.16.840.1.113719.1.27.100.14"
#define NLDAP_REQUEST_NAMING_CONTEXT_SYNC_EXTENDED_REQUEST      "2.16.840.1.113719.1.27.100.25"
#define NLDAP_REQUEST_NAMING_CONTEXT_SYNC_EXTENDED_REPLY        "2.16.840.1.113719.1.27.100.26"
#define NLDAP_ABORT_NAMING_CONTEXT_OPERATION_EXTENDED_REQUEST   "2.16.840.1.113719.1.27.100.29"
#define NLDAP_ABORT_NAMING_CONTEXT_OPERATION_EXTENDED_REPLY     "2.16.840.1.113719.1.27.100.30"
#define NLDAP_GET_CONTEXT_IDENTITY_NAME_EXTENDED_REQUEST        "2.16.840.1.113719.1.27.100.31"
#define NLDAP_GET_CONTEXT_IDENTITY_NAME_EXTENDED_REPLY          "2.16.840.1.113719.1.27.100.32"
#define NLDAP_CREATE_ORPHAN_NAMING_CONTEXT_EXTENDED_REQUEST     "2.16.840.1.113719.1.27.100.39"
#define NLDAP_CREATE_ORPHAN_NAMING_CONTEXT_EXTENDED_REPLY       "2.16.840.1.113719.1.27.100.40"
#define NLDAP_REMOVE_ORPHAN_NAMING_CONTEXT_EXTENDED_REQUEST     "2.16.840.1.113719.1.27.100.41"
#define NLDAP_REMOVE_ORPHAN_NAMING_CONTEXT_EXTENDED_REPLY       "2.16.840.1.113719.1.27.100.42"


/* Extensions to trigger eDirectory background processes */
#define NLDAP_TRIGGER_BKLINKER_EXTENDED_REQUEST     "2.16.840.1.113719.1.27.100.43"
#define NLDAP_TRIGGER_BKLINKER_EXTENDED_REPLY       "2.16.840.1.113719.1.27.100.44"

#define NLDAP_TRIGGER_JANITOR_EXTENDED_REQUEST      "2.16.840.1.113719.1.27.100.47"
#define NLDAP_TRIGGER_JANITOR_EXTENDED_REPLY        "2.16.840.1.113719.1.27.100.48"

#define NLDAP_TRIGGER_LIMBER_EXTENDED_REQUEST       "2.16.840.1.113719.1.27.100.49"
#define NLDAP_TRIGGER_LIMBER_EXTENDED_REPLY         "2.16.840.1.113719.1.27.100.50"
#define NLDAP_TRIGGER_SKULKER_EXTENDED_REQUEST      "2.16.840.1.113719.1.27.100.51"
#define NLDAP_TRIGGER_SKULKER_EXTENDED_REPLY        "2.16.840.1.113719.1.27.100.52"

#define NLDAP_TRIGGER_SCHEMA_SYNC_EXTENDED_REQUEST  "2.16.840.1.113719.1.27.100.53"
#define NLDAP_TRIGGER_SCHEMA_SYNC_EXTENDED_REPLY    "2.16.840.1.113719.1.27.100.54"

#define NLDAP_TRIGGER_PART_PURGE_EXTENDED_REQUEST   "2.16.840.1.113719.1.27.100.55"
#define NLDAP_TRIGGER_PART_PURGE_EXTENDED_REPLY     "2.16.840.1.113719.1.27.100.56"


/* eDirectory Event System Extension OIDS */
#define NLDAP_MONITOR_EVENTS_REQUEST        "2.16.840.1.113719.1.27.100.79"
#define NLDAP_MONITOR_EVENTS_RESPONSE       "2.16.840.1.113719.1.27.100.80"
#define NLDAP_EVENT_NOTIFICATION            "2.16.840.1.113719.1.27.100.81"
#define NLDAP_FILTERED_MONITOR_EVENTS_REQUEST "2.16.840.1.113719.1.27.100.84"

/* eDirectory LDAP Based Backup/Restore OIDs */
#define NLDAP_LDAP_BACKUP_REQUEST  			"2.16.840.1.113719.1.27.100.96"
#define NLDAP_LDAP_BACKUP_RESPONSE			"2.16.840.1.113719.1.27.100.97"


#define NLDAP_LDAP_RESTORE_REQUEST  		"2.16.840.1.113719.1.27.100.98"
#define NLDAP_LDAP_RESTORE_RESPONSE			"2.16.840.1.113719.1.27.100.99"

/* LBURP Extension OIDS */
#define NLDAP_LBURP_START_REQUEST		"2.16.840.1.113719.1.142.100.1"
#define NLDAP_LBURP_START_RESPONSE		"2.16.840.1.113719.1.142.100.2" 

#define NLDAP_LBURP_FULL_UPDATE			"2.16.840.1.113719.1.142.1.4.1"
#define NLDAP_LBURP_INCREMENTAL_UPDATE	"2.16.840.1.113719.1.142.1.4.2"

#define NLDAP_LBURP_END_REQUEST			"2.16.840.1.113719.1.142.100.4"
#define NLDAP_LBURP_END_RESPONSE		"2.16.840.1.113719.1.142.100.5"

#define NLDAP_LBURP_OPERATION_REQUEST 	"2.16.840.1.113719.1.142.100.6"
#define NLDAP_LBURP_OPERATION_RESPONSE  "2.16.840.1.113719.1.142.100.7"

/* Replica types */
typedef enum LDAP_REPLICA_TYPE {
   LDAP_RT_MASTER = 0,
   LDAP_RT_SECONDARY = 1,
   LDAP_RT_READONLY = 2,
   LDAP_RT_SUBREF = 3,
   LDAP_RT_SPARSE_WRITE = 4,
   LDAP_RT_SPARSE_READ = 5,
   LDAP_RT_COUNT = 6
} LDAP_REPLICA_TYPE;

/* Replica State can have any of the following values */
#define LDAP_RS_ON              0
#define LDAP_RS_NEW_REPLICA     1
#define LDAP_RS_DYING_REPLICA   2
#define LDAP_RS_LOCKED          3
#define LDAP_RS_TRANSITION_ON   6
#define LDAP_RS_DEAD_REPLICA    7
#define LDAP_RS_BEGIN_ADD       8
#define LDAP_RS_MASTER_START    11
#define LDAP_RS_MASTER_DONE     12
#define LDAP_RS_SS_0            48 /* Replica Splitting State 0 */
#define LDAP_RS_SS_1            49 /* Replica Splitting State 1 */
#define LDAP_RS_JS_0            64 /* Replica Joining State 0 */
#define LDAP_RS_JS_1            65 /* Replica Joining State 1 */
#define LDAP_RS_JS_2            66 /* Replica Joining State 2 */

#define L_MAX_DN_CHARS              257
#define L_MAX_BYTES_IN_UTF_CHAR     3
#define L_MAX_DN_BYTES              L_MAX_BYTES_IN_UTF_CHAR * L_MAX_DN_CHARS
#define FILTER_SEP                  '$'

/* eDirectory background processes*/
#define LDAP_BK_PROCESS_BKLINKER    1
#define LDAP_BK_PROCESS_JANITOR     2
#define LDAP_BK_PROCESS_LIMBER      3
#define LDAP_BK_PROCESS_SKULKER     4
#define LDAP_BK_PROCESS_SCHEMA_SYNC 5
#define LDAP_BK_PROCESS_PART_PURGE  6

/* This structure is used by the ldap_get_replica_info api */
typedef struct ldapreplicainfo {
      int               rootID;      
      int               state;     
      int               modificationTime; 
      int               purgeTime;        
      int               localReplicaID; 
      char              namingContextDN[L_MAX_DN_BYTES];  /* replace with symbol */
      LDAP_REPLICA_TYPE replicaType;      
      int               flags;
} LDAPReplicaInfo;

/* Values for flags in the LDAPReplicaInfo structure */
#define LDAP_DS_FLAG_BUSY     0x000000000001L
#define LDAP_DS_FLAG_BOUNDARY 0x000000000002L

/* This flag can be passed as a flag parameter in the partitioning APIs */
#define LDAP_ENSURE_SERVERS_UP         0x00000001

/*
 * eDirectory split partition. The dn identifies the root of new partition.
 */
LDAP_F(int) ldap_split_partition(LDAP* ld, char* dn, int flags);

/* 
 * Merge partitions.  The dn identifies the parent and the child partitions.
 */
LDAP_F(int) ldap_merge_partitions(LDAP* ld, char* dn, int flags);

/*
 * Add replica rooted at dn to server specified by serverName of
 * type replicaType
 */
LDAP_F(int) ldap_add_replica(LDAP* ld, char* dn, char* serverDN, LDAP_REPLICA_TYPE replicaType, int flags);

/*
 * Remove the replica from server specified by serverName.
 * The replica is specified by dn
 */
LDAP_F(int) ldap_remove_replica(LDAP* ld, char* dn, char* serverDN, int flags);

/*
 * Get the number of entries in the partition rooted at dn
 */
LDAP_F(int) ldap_partition_entry_count(LDAP* ld, char* dn, unsigned long* count);

/*
 * Change the type of the replica rooted at dn on server serverName
 * to new type identified by replicaType
 */
LDAP_F(int) ldap_change_replica_type(LDAP* ld, char* dn, char* serverName, LDAP_REPLICA_TYPE replicaType, int flags);

/*
 * Fills in the LDAPReplicaInfo structure for the replica rooted at dn on serverDN
 */
LDAP_F(int) ldap_get_replica_info(LDAP* ld, char* dn, char* serverDN, LDAPReplicaInfo* replicaInfo);

/*
 * Lists all the replicas on the server identified by serverDN
 * Caller does not allocate but is reponsible for freeing
 * replicaList by calling ldap_value_free
 */
LDAP_F(int) ldap_list_replicas(LDAP *ld, char *serverDN, char*** replicaList);

/*
 * In the LDUP world we would set some attribute on the replication
 * agreement object triggering a replication event between the
 * toServerDN and fromServerDN.
 * In the eDirectory world we ignore the fromServerDN attribute as 
 * this is always the server holding the master replica of the partition 
 * rooted at partitionRoot.
 */
LDAP_F(int) ldap_receive_all_updates(LDAP *ld, char* partitionRoot, char* toServerDN, char* fromServerDN);  

/*
 * In the LDUP world we would set some attribute on the replication
 * agreement object triggering a replication event that forces the
 * server specified by origServerDN to send updates to all replicas.
 * In the eDirectory world we ignore the origServerDN attribute as this
 * is always the server holding the master replica of the partition
 * rooted at partitionRoot.
 */
LDAP_F(int) ldap_send_all_updates(LDAP *ld, char* partitionRoot, char* origServerDN);

/*
 * eDirectory specific API.
 * Schedules skulker on server after delay seconds. Server must have
 * a replica of the partition identified by partitionRoot
 */
LDAP_F(int) ldap_request_partition_sync(LDAP *ld, char* serverName, char* partitionRoot, int delay);

/*
 * eDirectory-specific API
 * Schedule schema skulker on the server specified by the serverName
 * after delay seconds.
 */
LDAP_F(int) ldap_request_schema_sync(LDAP*ld, char* serverName, int delay);                    

/*
 * eDirectory-specific API.
 * This aborts the the last partition operation that was performed on
 * the partition specified by the dn container.  The flags parameter
 * is currently unused, pass 0
 */
LDAP_F(int) ldap_abort_partition_operation(LDAP* ld, char* dn, int flags);

/*
 * Get the dn of whoever we are logged in as. Caller is responsible 
 * for freeing identity by calling ldapx_memfree
 */
LDAP_F(int) ldap_get_bind_dn(LDAP* ld, char** identity);

/*
 * Get rights of trusteeDN to object dn for attribute attrName
 */
LDAP_F(int) ldap_get_effective_privileges(LDAP* ld, char* dn, char* trusteeDN, char* attrName, int* privileges);

/*
 * Get rights for subjectDN on targetDN for attributes targetAttrs
 */
LDAP_F(int) ldap_get_effective_privileges_list(LDAP* ld, char *subjectDN, char *targetDN, char **targetAttrs, int *privileges);


/*
 * This API is provided for legacy eDirectory applications.  
 * It converts a dot-separated Unicode eDirectory name to 
 * a typed LDAP name. Caller has the responsibility of
 * freeing ldapName by calling ldapx_memfree
 */
LDAP_F(int) ldap_nds_to_ldap(LDAP* ld, unsigned short* ndsName, char** ldapName);


/*
 * This API is provided for legacy eDirectory applications.
 * It converts a DNS name to a X500 name.
 * Caller has the responsibility of freeing ldapName
 * by calling ldapx_memfree
 */
LDAP_F(int) ldap_dns_to_x500_dn(LDAP* ld, char* ndsName, char** x500Name);


/*
 * This API is used to reload the LDAP Server.  The LDAP context
 * identifies the LDAP Server
 */
LDAP_F(int) ldap_refresh_server(LDAP* ld);

/*
 * Applications must use this API to free memory that the library allocates
 */
LDAP_F(void) ldapx_memfree(void* mem);

/*
 * This API is used to set the attribute and class filter on eDirectory 
 * Virtual Replica.  The serverDN identifies the server on which
 * the filter needs to be set.  With eDirectory 8.5 these filters are set
 * on a per server basis.  The filter identifies the classes and
 * attributes that comprise the filter and has the following format:
 * 
 * Example Filter:
 *  class1$attr1$attr2$attr3$$class2$attr1$$$
 */
LDAP_F(int) ldap_set_replication_filter(LDAP* ld, const char* serverDN, const char* filter);

/*
 * This API is used to get the attribute and class filter on an existing
 * eDirectory Server.  The filter has the same format as that in the 
 * set_replication_filter call. The caller must free filter by calling 
 * ldapx_memfree
 */
LDAP_F(int) ldap_get_replication_filter(LDAP* ld, const char* serverDN, char** filter);

/*
 * This API is used to split an orphan partition on the specified server
 */
LDAP_F(int) ldap_split_orphan_partition(LDAP* ld, char* serverDN, char* contextName);

/*
 * This API is used to remove the specified orphan partition from the specified 
 * server. The call fails if the server does not hold the specified partition
 */
LDAP_F(int) ldap_remove_orphan_partition(LDAP* ld, char* serverDN, char* contextName);

/*
 * This API triggers the specified background process on the eDirectory server
 */
LDAP_F(int) ldap_trigger_back_process(LDAP* ld, int processID);


/*
 * The following four functions (ldap_monitor_events, ldap_monitor_events,
 * ldap_parse_ds_event, ldap_event_free) support the development of 
 * the eDirectory event system extension.
 */

/*
 * The ldap_monitor_events function sends a monitorEvents extended request
 * to the ldap server.
 */
LDAP_F( int )
ldap_monitor_events(
    LDAP                 *ld,
    int                  eventCount,
    EVT_EventSpecifier   *events,
    int                  *msgID);

/*
 * The ldap_monitor_events_filtered function sends a filteredMonitorEvents
 * request to the ldap server.
 */
LDAP_F( int)
ldap_monitor_events_filtered(
	LDAP                        *ld,
	int	                        eventCount,
	EVT_FilteredEventSpecifier  *events,
	int                         *msgID);

/* 
 * The ldap_parse_monitor_events_response function parses a monitorEvents
 * response. This response is received if an error or other exceptional 
 * condition occurs as the server responds to the monitorEvents request.
 */
LDAP_F( int )
ldap_parse_monitor_events_response(
    LDAP               *ld, 
    LDAPMessage        *eventMessage,
    int                *resultCode,
    char               **errorMsg,
    int                *badEventCount,
    EVT_EventSpecifier  **badEvents,
    int                freeIt);

/*
 * The ldap_parse_ds_event function parses an eventNotification intermediate
 * response.
 */
LDAP_F( int )
ldap_parse_ds_event(
    LDAP        *ld,
    LDAPMessage *eventMessage,
    int         *eventType,
    int         *eventResult,
    void        **eventData,
    int         freeIt);

/*
 * The ldap_event_free function should be called to free event data returned
 * by the ldap_parse_ds_event function.
 */
LDAP_F( void )
ldap_event_free(void  *eventData);

/* LBURP functionality stuff*/

typedef struct lburpoperationlist {
	int operation;
	char *dn;
	union {
		LDAPMod  **attrs;
		char *newRDN;
		int deleteOldRDN;
		char *newSuperior;
	}value;
	LDAPControl **ServerControls;
	LDAPControl **ClientControls;
} LBURPUpdateOperationList;
															
typedef struct lburpupdateresult {
	int packageID;
	int resultCode;
	char *errorMsg;
} LBURPUpdateResult;


LDAP_F(int)
ldap_lburp_start_request (
	LDAP *ld,
	int *msgID);

LDAP_F(int)
ldap_parse_lburp_start_response(
	LDAP    *ld,
	LDAPMessage *lburpStartMessage,
	int *resultCode,
	char    **errorMsg,
	int *tranSize,
	int freeIt);

LDAP_F(int)
ldap_lburp_operation_request(
	LDAP *ld,
	int packageID,
	LBURPUpdateOperationList **op,
	int *msgID);

LDAP_F (int)
ldap_parse_lburp_operation_response(
	LDAP *ld,
	LDAPMessage *lburpMessage,
	int *resultCode,
	char    **errorMsg,
	int *failedOpsCount,
	LBURPUpdateResult **failedOperations,
	int freeIt);

LDAP_F (int)
ldap_lburp_end_request (
	LDAP *ld,
	int sequenceNumber,
	int *msgID);

LDAP_F(int)
ldap_parse_lburp_end_response (
	LDAP *ld,
	LDAPMessage *lburpEndMessage,
	int *resultCode,
	char **errorMsg,
	int freeIt);

LDAP_F(int)
ldap_backup_object (
	LDAP *ld,
	const char *dn,
	const char *passwd,
	char **objectState,
	char **objectInfo,
	char **chunckSize,
	int *size);

LDAP_F(int)
ldap_restore_object (
	LDAP *ld,
	const char *dn,
	const char *passwd,
	char *objectInfo,
	char *chunckSize,
	int size);

/*
 * Deprecated prototypes.  Obsolete terminology "naming context" has been
 * replaced with "partition".
 */
LDAP_F(int) ldap_create_naming_context(LDAP* ld, char* dn, int flags);
LDAP_F(int) ldap_merge_naming_contexts(LDAP* ld, char*dn, int flags);
LDAP_F(int) ldap_naming_context_entry_count(LDAP* ld, char* dn, unsigned long* count);
LDAP_F(int) ldap_request_naming_context_sync(LDAP *ld, char* serverName, char* partitionRoot, int delay);
LDAP_F(int) ldap_abort_naming_context_operation(LDAP* ld, char* dn, int flags);
LDAP_F(int) ldap_get_context_identity_name(LDAP* ld, char** identity);
LDAP_F(int) ldap_create_orphan_naming_context(LDAP* ld, char* serverDN, char* contextName);
LDAP_F(int) ldap_remove_orphan_naming_context(LDAP* ld, char* serverDN, char* contextName);

LDAP_END_DECL

#endif /*LDAPX_H*/


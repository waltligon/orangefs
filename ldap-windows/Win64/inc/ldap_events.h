/*								 
 * $Novell: ldap_events.h,v 1.16 2003/05/21 06:19:09 $
 ******************************************************************************
 * Copyright (C) 1999, 2000 Novell, Inc. All Rights Reserved.
 * 
 * THIS WORK IS SUBJECT TO U.S. AND INTERNATIONAL COPYRIGHT LAWS AND
 * TREATIES. USE, MODIFICATION, AND REDISTRIBUTION OF THIS WORK IS SUBJECT
 * TO VERSION 2.0.7 OF THE OPENLDAP PUBLIC LICENSE, A COPY OF WHICH IS
 * AVAILABLE AT HTTP://WWW.OPENLDAP.ORG/LICENSE.HTML OR IN THE FILE "LICENSE"
 * IN THE TOP-LEVEL DIRECTORY OF THE DISTRIBUTION. ANY USE OR EXPLOITATION
 * OF THIS WORK OTHER THAN AS AUTHORIZED IN VERSION 2.0.7 OF THE OPENLDAP
 * PUBLIC LICENSE, OR OTHER PRIOR WRITTEN CONSENT FROM NOVELL, COULD SUBJECT
 * THE PERPETRATOR TO CRIMINAL AND CIVIL LIABILITY. 
 ******************************************************************************
 */
#ifndef EVENTS_H
#define EVENTS_H

#include <lber_types.h>

/* The following constants are used to set the event status */
#define EVT_STATUS_ALL        0
#define EVT_STATUS_SUCCESS    1
#define EVT_STATUS_FAILURE    2

typedef struct {
	int eventType;
	int eventStatus;
} EVT_EventSpecifier;

typedef struct {
	int eventType;
	int eventStatus;
	char*  filter;
} EVT_FilteredEventSpecifier;

#define EVT_INVALID 						0
#define EVT_CREATE_ENTRY					1	/* data is EVT_EntryInfo */
#define EVT_DELETE_ENTRY					2	/* data is EVT_EntryInfo */
#define EVT_RENAME_ENTRY					3	/* data is EVT_EntryInfo */
#define EVT_MOVE_SOURCE_ENTRY				4	/* data is EVT_EntryInfo */
#define EVT_ADD_VALUE						5	/* data is EVT_ValueInfo */
#define EVT_DELETE_VALUE					6	/* data is EVT_ValueInfo */
#define EVT_CLOSE_STREAM					7	/* data is EVT_ValueInfo */
#define EVT_DELETE_ATTRIBUTE				8	/* data is EVT_ValueInfo */
#define EVT_SET_BINDERY_CONTEXT 			9	/* no data */
#define EVT_CREATE_BINDERY_OBJECT			10	/* data is EVT_BinderyObjectInfo */
#define EVT_DELETE_BINDERY_OBJECT			11	/* data is EVT_BinderyObjectInfo */
#define EVT_CHECK_SEV						12	/* data is EVT_SEVInfo */
#define EVT_UPDATE_SEV						13	/* no data */
#define EVT_MOVE_DEST_ENTRY 				14	/* data is EVT_EntryInfo */
#define EVT_DELETE_UNUSED_EXTREF			15	/* data is EVT_EntryInfo */

#define EVT_REMOTE_SERVER_DOWN				17	/* data is EVT_NetAddress */
#define EVT_NCP_RETRY_EXPENDED				18	/* data is EVT_NetAddress */

#define EVT_PARTITION_OPERATION_EVENT		20	/* data is EVT_EventData */
#define EVT_CHANGE_MODULE_STATE				21	/* data is EVT_ModuleState */

#define EVT_DB_AUTHEN						26 /* data is EVT_DebugInfo */
#define EVT_DB_BACKLINK 					27 /* data is EVT_DebugInfo */
#define EVT_DB_BUFFERS						28 /* data is EVT_DebugInfo */
#define EVT_DB_COLL 						29 /* data is EVT_DebugInfo */
#define EVT_DB_DSAGENT						30 /* data is EVT_DebugInfo */
#define EVT_DB_EMU							31 /* data is EVT_DebugInfo */
#define EVT_DB_FRAGGER						32 /* data is EVT_DebugInfo */
#define EVT_DB_INIT 						33 /* data is EVT_DebugInfo */
#define EVT_DB_INSPECTOR					34 /* data is EVT_DebugInfo */
#define EVT_DB_JANITOR						35 /* data is EVT_DebugInfo */
#define EVT_DB_LIMBER						36 /* data is EVT_DebugInfo */
#define EVT_DB_LOCKING						37 /* data is EVT_DebugInfo */
#define EVT_DB_MOVE 						38 /* data is EVT_DebugInfo */
#define EVT_DB_MIN							39 /* data is EVT_DebugInfo */
#define EVT_DB_MISC 						40 /* data is EVT_DebugInfo */
#define EVT_DB_PART 						41 /* data is EVT_DebugInfo */
#define EVT_DB_RECMAN						42 /* data is EVT_DebugInfo */

#define EVT_DB_RESNAME						44 /* data is EVT_DebugInfo */
#define EVT_DB_SAP							45 /* data is EVT_DebugInfo */
#define EVT_DB_SCHEMA						46 /* data is EVT_DebugInfo */
#define EVT_DB_SKULKER						47 /* data is EVT_DebugInfo */
#define EVT_DB_STREAMS						48 /* data is EVT_DebugInfo */
#define EVT_DB_SYNC_IN						49 /* data is EVT_DebugInfo */
#define EVT_DB_THREADS						50 /* data is EVT_DebugInfo */
#define EVT_DB_TIMEVECTOR					51 /* data is EVT_DebugInfo */
#define EVT_DB_VCLIENT						52 /* data is EVT_DebugInfo */
#define EVT_AGENT_OPEN_LOCAL				53	/* EVT_EventData */
#define EVT_AGENT_CLOSE_LOCAL				54	/* EVT_EventData */
#define EVT_DS_ERR_VIA_BINDERY				55	/* EVT_EventData */
#define EVT_DSA_BAD_VERB					56	/* EVT_EventData */
#define EVT_DSA_REQUEST_START				57	/* EVT_EventData */
#define EVT_DSA_REQUEST_END 				58	/* EVT_EventData */
#define EVT_MOVE_SUBTREE					59	/* EVT_EventData */
#define EVT_NO_REPLICA_PTR					60	/* EVT_EventData */
#define EVT_SYNC_IN_END 					61	/* EVT_EventData */
#define EVT_BKLINK_SEV						62	/* EVT_EventData */
#define EVT_BKLINK_OPERATOR 				63	/* EVT_EventData */
#define EVT_DELETE_SUBTREE					64	/* EVT_EventData */

#define EVT_REFERRAL						67	/* EVT_EventData */
#define EVT_UPDATE_CLASS_DEF				68	/* EVT_EventData */
#define EVT_UPDATE_ATTR_DEF 				69	/* EVT_EventData */
#define EVT_LOST_ENTRY						70	/* EVT_EventData */
#define EVT_PURGE_ENTRY_FAIL				71	/* EVT_EventData */
#define EVT_PURGE_START 					72	/* EVT_EventData */
#define EVT_PURGE_END						73	/* EVT_EventData */

#define EVT_LIMBER_DONE 					76	/* EVT_EventData */
#define EVT_SPLIT_DONE						77	/* EVT_EventData */
#define EVT_SYNC_SVR_OUT_START				78	/* EVT_EventData */
#define EVT_SYNC_SVR_OUT_END				79	/* EVT_EventData */
#define EVT_SYNC_PART_START 				80	/* EVT_EventData */
#define EVT_SYNC_PART_END					81	/* EVT_EventData */
#define EVT_MOVE_TREE_START 				82	/* EVT_EventData */
#define EVT_MOVE_TREE_END					83	/* EVT_EventData */

#define EVT_JOIN_DONE						86	/* EVT_EventData */
#define EVT_PARTITION_LOCKED				87	/* EVT_EventData */
#define EVT_PARTITION_UNLOCKED				88	/* EVT_EventData */
#define EVT_SCHEMA_SYNC 					89	/* EVT_EventData */
#define EVT_NAME_COLLISION					90	/* EVT_EventData */
#define EVT_NLM_LOADED						91  /* EVT_EventData */

#define EVT_LUMBER_DONE 					94	/* no data */
#define EVT_BACKLINK_PROC_DONE				95	/* no data */
#define EVT_SERVER_RENAME					96	/* EVT_EventData */
#define EVT_SYNTHETIC_TIME					97	/* EVT_EventData */
#define EVT_SERVER_ADDRESS_CHANGE			98	/* no data */
#define EVT_DSA_READ						99	/* EVT_EventData */
#define EVT_LOGIN							100 /* EVT_EventData */
#define EVT_CHGPASS 						101 /* EVT_EventData */
#define EVT_LOGOUT							102 /* EVT_EventData */
#define EVT_ADD_REPLICA 					103 /* EVT_EventData */
#define EVT_REMOVE_REPLICA					104 /* EVT_EventData */
#define EVT_SPLIT_PARTITION 				105 /* EVT_EventData */
#define EVT_JOIN_PARTITIONS 				106 /* EVT_EventData */
#define EVT_CHANGE_REPLICA_TYPE 			107 /* EVT_EventData */
#define EVT_REMOVE_ENTRY					108 /* EVT_EventData */
#define EVT_ABORT_PARTITION_OP				109 /* EVT_EventData */
#define EVT_RECV_REPLICA_UPDATES			110 /* EVT_EventData */
#define EVT_REPAIR_TIME_STAMPS				111 /* EVT_EventData */
#define EVT_SEND_REPLICA_UPDATES			112 /* EVT_EventData */
#define EVT_VERIFY_PASS 					113 /* EVT_EventData */
#define EVT_BACKUP_ENTRY					114 /* EVT_EventData */
#define EVT_RESTORE_ENTRY					115 /* EVT_EventData */
#define EVT_DEFINE_ATTR_DEF 				116 /* EVT_EventData */
#define EVT_REMOVE_ATTR_DEF 				117 /* EVT_EventData */
#define EVT_REMOVE_CLASS_DEF				118 /* EVT_EventData */
#define EVT_DEFINE_CLASS_DEF				119 /* EVT_EventData */
#define EVT_MODIFY_CLASS_DEF				120 /* EVT_EventData */
#define EVT_RESET_DS_COUNTERS				121 /* EVT_EventData */
#define EVT_REMOVE_ENTRY_DIR				122 /* EVT_EventData */
#define EVT_COMPARE_ATTR_VALUE				123 /* EVT_EventData */
#define EVT_STREAM		 					124 /* EVT_EventData */
#define EVT_LIST_SUBORDINATES				125 /* EVT_EventData */
#define EVT_LIST_CONT_CLASSES				126 /* EVT_EventData */
#define EVT_INSPECT_ENTRY					127 /* EVT_EventData */
#define EVT_RESEND_ENTRY					128 /* EVT_EventData */
#define EVT_MUTATE_ENTRY					129 /* EVT_EventData */
#define EVT_MERGE_ENTRIES					130 /* EVT_EventData */
#define EVT_MERGE_TREE						131 /* EVT_EventData */
#define EVT_CREATE_SUBREF					132 /* EVT_EventData */
#define EVT_LIST_PARTITIONS 				133 /* EVT_EventData */
#define EVT_READ_ATTR						134 /* EVT_EventData */
#define EVT_READ_REFERENCES 				135 /* EVT_EventData */
#define EVT_UPDATE_REPLICA					136 /* EVT_EventData */
#define EVT_START_UPDATE_REPLICA			137 /* EVT_EventData */
#define EVT_END_UPDATE_REPLICA				138 /* EVT_EventData */
#define EVT_SYNC_PARTITION					139 /* EVT_EventData */
#define EVT_SYNC_SCHEMA 					140 /* no data */
#define EVT_CREATE_BACKLINK 				141 /* EVT_EventData */
#define EVT_CHECK_CONSOLE_OPERATOR			142 /* EVT_EventData */
#define EVT_CHANGE_TREE_NAME				143 /* EVT_EventData */
#define EVT_START_JOIN						144 /* EVT_EventData */
#define EVT_ABORT_JOIN						145 /* EVT_EventData */
#define EVT_UPDATE_SCHEMA					146 /* EVT_EventData */
#define EVT_START_UPDATE_SCHEMA 			147 /* EVT_EventData */
#define EVT_END_UPDATE_SCHEMA				148 /* EVT_EventData */
#define EVT_MOVE_TREE						149 /* EVT_EventData */
#define EVT_RELOAD_DS						150 /* no data */
#define EVT_ADD_PROPERTY					151 /* EVT_EventData */
#define EVT_DELETE_PROPERTY 				152 /* EVT_EventData */
#define EVT_ADD_MEMBER						153 /* EVT_EventData */
#define EVT_DELETE_MEMBER					154 /* EVT_EventData */
#define EVT_CHANGE_PROP_SECURITY			155 /* EVT_EventData */
#define EVT_CHANGE_OBJ_SECURITY 			156 /* EVT_EventData */

#define EVT_CONNECT_TO_ADDRESS				158 /* EVT_NetAddress */
#define EVT_SEARCH							159 /* EVT_EventData */
#define EVT_PARTITION_STATE_CHG 			160 /* EVT_EventData */
#define EVT_REMOVE_BACKLINK 				161 /* EVT_EventData */
#define EVT_LOW_LEVEL_JOIN					162 /* EVT_EventData */
#define EVT_CREATE_NAMEBASE 				163 /* no data */
#define EVT_CHANGE_SECURITY_EQUALS			164 /* EVT_EventData */

#define EVT_DB_NCPENG						166	/* data is EVT_DebugInfo */
#define EVT_CRC_FAILURE 					167 /* EVT_EventData */
#define EVT_ADD_ENTRY						168 /* EVT_EventData */
#define EVT_MODIFY_ENTRY					169 /* EVT_EventData */

#define EVT_OPEN_BINDERY					171 /* no data */
#define EVT_CLOSE_BINDERY					172 /* no data */
#define EVT_CHANGE_CONN_STATE				173 /* EVT_ChangeConnState */
#define EVT_NEW_SCHEMA_EPOCH				174 /* no data */

#define EVT_DB_AUDIT	                 	175 /* data is EVT_DebugInfo */
#define EVT_DB_AUDIT_NCP 	            	176 /* data is EVT_DebugInfo */
#define EVT_DB_AUDIT_SKULK	         	    177 /* data is EVT_DebugInfo */
#define EVT_MODIFY_RDN			        	178 /* EVT_EventData */

#define EVT_ENTRYID_SWAP					181 /* EVT_EventData */
#define EVT_INSIDE_NCP_REQUEST				182 /* no data */

#define EVT_DB_LOST_ENTRY					183 /* data is EVT_DebugInfo */
#define EVT_DB_CHANGE_CACHE 	 			184 /* data is EVT_DebugInfo */

#define EVT_LOW_LEVEL_SPLIT					185 /* EVT_EventData */
#define EVT_DB_PURGE   						186 /* data is EVT_DebugInfo */
#define EVT_END_NAMEBASE_TRANSACTION 		187 /* no data */
#define EVT_ALLOW_LOGIN						188 /* EVT_EventData */

#define EVT_DB_CLIENT_BUFFERS				189 /* data is EVT_DebugInfo */
#define EVT_DB_WANMAN						190 /* data is EVT_DebugInfo */

#define EVT_LOCAL_REPLICA_CHANGE			197 /* EVT_EventData */
#define EVT_DB_DRL							198 /* data is EVT_DebugInfo */

#define EVT_MOVE_ENTRY_SOURCE				199 /* EVT_EventData */
#define EVT_MOVE_ENTRY_DEST					200 /* EVT_EventData */
#define EVT_NOTIFY_REF_CHANGE				201 /* EVT_EventData */
#define EVT_DB_ALLOC						202 /* data is EVT_DebugInfo */

#define EVT_CONSOLE_OPERATION				203 /* EVT_EventData */
#define EVT_DB_SERVER_PACKET				204 /* data is EVT_DebugInfo */

#define EVT_DB_OBIT 	 			        207 /* data is EVT_DebugInfo */
#define EVT_REPLICA_IN_TRANSITION			208 /* EVT_EventData */
#define EVT_DB_SYNC_DETAIL 	    	        209 /* data is EVT_DebugInfo */
#define EVT_DB_CONN_TRACE   			    210 /* data is EVT_DebugInfo */
#define EVT_CHANGE_CONFIG_PARM				211 /* EVT_ChangeConfigParm */
#define EVT_COMPUTE_CONN_SEV_INLINE			212 /* EVT_ChangeConnState */
#define EVT_BEGIN_NAMEBASE_TRANSACTION		213 /* no data */
#define EVT_DB_DIRXML						214 /* data is EVT_DebugInfo */
#define EVT_VR_DRIVER_STATE_CHANGE			215 /* EVT_EntryInfo */
#define EVT_REQ_UPDATE_SERVER_STATUS		216 /* no data */
#define EVT_DB_DIRXML_DRIVERS				217 /* data is EVT_DebugInfo */
#define EVT_DB_NDSMON						218 /* data is EVT_DebugInfo */
#define EVT_CHANGE_SERVER_ADDRS				219 /* EVT_ChangeServerAddr */
#define EVT_DB_DNS							220 /* data is EVT_DebugInfo */
#define EVT_DB_REPAIR						221 /* data is EVT_DebugInfo */
#define EVT_DB_REPAIR_DEBUG 				222 /* data is EVT_DebugInfo */

#define EVT_ITERATOR						224 /* EVT_EventData */
#define EVT_DB_SCHEMA_DETAIL				225 /* data is EVT_DebugInfo */
#define EVT_LOW_LEVEL_JOIN_BEGIN    		226 /* EVT_EventData */
#define EVT_DB_IN_SYNC_DETAIL				227 /* data is EVT_DebugInfo */
#define EVT_PRE_DELETE_ENTRY                228 /* EVT_EventData */

#define EVT_DB_SSL							229 /* data is EVT_DebugInfo */
#define EVT_DB_PKI							230 /* data is EVT_DebugInfo */
#define EVT_DB_HTTPSTK						231 /* data is EVT_DebugInfo */
#define EVT_DB_LDAPSTK						232 /* data is EVT_DebugInfo */
#define EVT_DB_NICIEXT						233 /* data is EVT_DebugInfo */
#define EVT_DB_SECRET_STORE				    234 /* data is EVT_DebugInfo */
#define EVT_DB_NMAS					    	235 /* data is EVT_DebugInfo */
#define EVT_DB_BACKLINK_DETAIL		   	    236 /* data is EVT_DebugInfo */
#define EVT_DB_DRL_DETAIL					237 /* data is EVT_DebugInfo */
#define EVT_DB_OBJECT_PRODUCER			    238 /* data is EVT_DebugInfo */
#define EVT_DB_SEARCH						239 /* data is EVT_DebugInfo */
#define EVT_DB_SEARCH_DETAIL				240 /* data is EVT_DebugInfo */
#define EVT_STATUS_LOG					    241	/* EVT_StatusLogInfo */
#define EVT_DB_NPKI_API						242 /* data is EVT_DebugInfo */

#define EVT_LDAP_BIND                     247   /* data is EVT_AuthEventData */ 
#define EVT_LDAP_BINDRESPONSE             248	/* data is EVT_ResponseEventData */ 
#define EVT_LDAP_UNBIND                   249	/* data is EVT_AuthEventData */ 
#define EVT_LDAP_CONNECTION               250	/* data is EVT_ConnectionEventData */
#define EVT_LDAP_SEARCH                   251	/* data is EVT_SearchEventData */ 
#define EVT_LDAP_SEARCHRESPONSE           252	/* data is EVT_ResponseEventData */ 	
#define EVT_LDAP_SEARCHENTRYRESPONSE      253	/* data is EVT_SearchEntryResponseEventData */ 
#define EVT_LDAP_ADD                      254	/* data is EVT_UpdateEventData */ 
#define EVT_LDAP_ADDRESPONSE              255	/* data is EVT_ResponseEventData */ 
#define EVT_LDAP_COMPARE                  256	/* data is EVT_CompareEventData */ 
#define EVT_LDAP_COMPARERESPONSE          257	/* data is EVT_ResponseEventData */ 
#define EVT_LDAP_MODIFY                   258	/* data is EVT_UpdateEventData */ 
#define EVT_LDAP_MODIFYRESPONSE           259	/* data is EVT_ResponseEventData */ 
#define EVT_LDAP_DELETE                   260	/* data is EVT_UpdateEventData */ 
#define EVT_LDAP_DELETERESPONSE           261	/* data is EVT_ResponseEventData */ 
#define EVT_LDAP_MODDN                    262	/* data is EVT_ModDNEventData */ 
#define EVT_LDAP_MODDNRESPONSE            263	/* data is EVT_ResponseEventData */ 
#define EVT_LDAP_ABANDON                  264	/* data is EVT_AbandonEventData */ 
#define EVT_LDAP_EXTOP                    265	/* data is EVT_ExtOpEventData */ 
#define EVT_LDAP_SYSEXTOP                 266	/* data is EVT_SysExtOpEventData */ 
#define EVT_LDAP_EXTOP_RESPONSE           267	/* data is EVT_ResponseEventData */ 
#define EVT_LDAP_MODLDAPSERVER            268	/* Only Event is generated. No corresponding structure available */ 
#define EVT_LDAP_PASSWORDMODIFY           269	/* data is EVT_PasswordModifyEventData */ 
#define EVT_LDAP_UNKNOWNOP                270	/* data is EVT_UnknownEventData*/ 

#define EVT_MAX_EVENTS	   					271


typedef struct 
{	unsigned int 	seconds; 
    unsigned int 	replicaNumber; 
    unsigned int 	event;
} EVT_TimeStamp;

typedef struct
{   char            *perpetratorDN;
    char  		    *entryDN; 
    char  		    *className; 
    unsigned int   	verb; 
    unsigned int   	flags;
	EVT_TimeStamp 	creationTime;
    char		    *newDN;
} EVT_EntryInfo;

/* EVT_EntryInfo flag definitions */
#define EVT_F_PARTITION_ROOT 	0x0001
#define EVT_F_EXTREF 		   	0x0002
#define EVT_F_ALIAS				0x0004

typedef struct
{   unsigned int  verb; 
    char          *perpetratorDN;
    char		  *entryDN; 
    char		  *attributeName;
    char		  *syntaxOID; 
    char		  *className;
	EVT_TimeStamp timeStamp;
	unsigned int  size;
	char  		  *value;
} EVT_ValueInfo;

typedef struct
{	char 		  *entryDN; 
    unsigned int  type; 
    unsigned int  emuObjFlags;
    unsigned int  security;
	char 		  *name;
} EVT_BinderyObjectInfo;

typedef struct
{
    int     type;
    int     length;
    char    *address;
} EVT_ReferralAddress;

typedef struct
{	char 		         *entryDN;
    unsigned int         retryCount;
	char 		         *valueDN;
    int                  referralCount;
	EVT_ReferralAddress  *referrals;
} EVT_SEVInfo;

typedef struct
{   unsigned int	type;
	unsigned int	length;
	char		    data[1];
} EVT_NetAddress;

typedef struct
{   char*			connectionDN;
	unsigned int    oldFlags;
    unsigned int	newFlags;
	char* 			sourceModule;
} EVT_ChangeConnState;


/*	Configuration parameter change data - data used for events generated when
 */
#define EVT_CFG_TYPE_NULL		0
#define EVT_CFG_TYPE_BINARY		1
#define EVT_CFG_TYPE_INT		2
#define EVT_CFG_TYPE_STRING		3
#define EVT_CFG_TYPE_BOOLEAN	5

typedef struct EVT_ChangeConfigParm {
    int		                type;
	char 	                *name;
    union {
        int                 integer;
		int					boolean;
        char*               utf8Str;
		struct {
			int             size;
		    unsigned char*  data;
		} binary;
    } value;   
} EVT_ChangeConfigParm;


#define EVT_MOD_HIDDEN	  		0x0001
#define EVT_MOD_SYSTEM	  		0x0002
#define EVT_MOD_ENGINE	  		0x0004
#define EVT_MOD_AUTOMATIC 		0x0008
#define EVT_MOD_FILE_MASK		0x00FF

#define EVT_MOD_POSTEVENT 		0x0100
#define EVT_MOD_AVAILABLE 		0x0200
#define EVT_MOD_LOADING	  		0x0400
#define EVT_MOD_MODIFY			0x0800
#define EVT_MOD_NEGATE_BIT		0x8000
#define EVT_MOD_EVENT_MASK		0xFF00

#define EVT_MAX_MODULE_NAME	    32
#define EVT_MAX_MODULE_DESCR	128

typedef struct 
{   char        *connectionDN;
	unsigned 	flags;
    char*       sourceModule;
	char 		name[EVT_MAX_MODULE_NAME];
	char 		description[EVT_MAX_MODULE_DESCR];
} EVT_ModuleState;


#define EVT_CSA_ADDRESS			0x0001
#define EVT_CSA_PSTACK			0x0002	
#define EVT_CSA_EVENT_MASK		0x00FF	

#define EVT_CSA_REMOVED			0x0100
#define EVT_CSA_POST_EVENT		0x0200
#define EVT_CSA_MOD_MASK		0xFF00	

#define EVT_MAX_PSTK_NAME		15

typedef struct EVT_ChangeServerAddr
{   unsigned int 	flags;
	int 			proto;
    int         	addrFamily;
    int         	addrSize;
	unsigned char 	*addr;
	char 			*pstkname;
    char            *sourceModule;
} EVT_ChangeServerAddr;

typedef struct
{   unsigned int	dstime;
    unsigned int	milliseconds;	
	unsigned int	curProcess;	
	unsigned int	verb;		
    char            *perpetratorDN;
    unsigned int	intValues[4];
    char*           strValues[4];
} EVT_EventData;

typedef struct
{
	unsigned int    logTime;
    char            *moduleName;
    char            *messageLevel;
    char			*messageEvent;
    char            *messageCode;
    char            *messageDesc;
    char            *statusDoc;
} EVT_StatusLogInfo;


#define DB_PARAM_TYPE_ENTRYID      1
#define DB_PARAM_TYPE_STRING       2
#define DB_PARAM_TYPE_BINARY       3
#define DB_PARAM_TYPE_INTEGER      4
#define DB_PARAM_TYPE_ADDRESS      5
#define DB_PARAM_TYPE_TIMESTAMP    6
#define DB_PARAM_TYPE_TIMEVECTOR   7


typedef struct {
	unsigned int  type;
	unsigned int  length;
	char*		  data;
    }   DB_netAddress;

typedef struct {
	unsigned int  size;
	void*         data;
    } DB_binary;

typedef struct {
	unsigned int   count;
	EVT_TimeStamp  *timeStamps;
    } DB_timeStampVector;

typedef union {
    int                 integer;
    char* 			    utf8Str;
    EVT_TimeStamp       timeStamp;
    DB_netAddress       netAddress;
    DB_binary           binary;
    DB_timeStampVector  timeStampVector;
    } DB_value;

typedef struct {
	int       type;
    DB_value  value;
    } DB_Parameter;   

typedef struct
{
   unsigned int     dsTime;
	unsigned int     milliseconds;
	char             *perpetratorDN;
   char             *formatString;
   int              verb;
   int              paramCount;
   DB_Parameter     *parameters;
} EVT_DebugInfo;

/* structures of ldap auditing client requirement as per LDAP Event Data Encoder Sequences */

typedef struct
{
   unsigned int connection;	
   unsigned int time;
   char *inetAddr;
}EVT_ConnectionEventData;


typedef struct
{
   EVT_ConnectionEventData *connectionData;
   unsigned int msgID;
   unsigned int time;
   char *bindDN;
   unsigned int bindType;
   char *authMechanism;
   char **controlOID;
   int resultCode;
}EVT_AuthEventData;



typedef struct
{
   EVT_ConnectionEventData *connectionData;
   unsigned int msgID;
   unsigned int time;
   char *bindDN;
   char *base;
   unsigned int scope;
   char *filter;
   char **attrs;
   char **controlOID;
   int resultCode;
}EVT_SearchEventData;


typedef struct
{
   EVT_ConnectionEventData *connectionData;
   unsigned int msgID;
   unsigned int time;
   char* entryDN;
   char *className;
   char **attrs;
   int resultCode;
}EVT_SearchEntryResponseEventData;

typedef struct
{
   EVT_ConnectionEventData *connectionData;
   unsigned int msgID;
   unsigned int time;
   unsigned int operation;
   char *bindDN;
   char *entryDN;
   char *className;
   char **controlOID;
   int resultCode;
}EVT_UpdateEventData; 

typedef struct
{
   EVT_ConnectionEventData *connectionData;
   unsigned int msgID;
   unsigned int time;
   char *bindDN;
   char *compareDN;
   char *assertionType;
   char *assertionValue;
   char *className;
   int resultCode;
}EVT_CompareEventData;



typedef struct
{
   EVT_ConnectionEventData *connectionData;
   unsigned int msgID;
   unsigned int time;
   char *bindDN;
   char *oldRDN;
   char *newRDN;	
   char *className;
   char **controlOID;
   int resultCode;
}EVT_ModDNEventData;

typedef struct
{
   EVT_ConnectionEventData *connectionData;
   unsigned int msgID;
   unsigned int time;
   unsigned int  operation;
   char *bindDN;
   int resultCode;
}EVT_AbandonEventData; 

typedef struct
{
   EVT_ConnectionEventData *connectionData;
   unsigned int msgID;
   unsigned int time;
   unsigned int operation;
   char *extensionOID;
   char *bindDN;
   int resultCode;
}EVT_ExtOpEventData; 


typedef struct
{
   EVT_ConnectionEventData *connectionData;
   unsigned int msgID;
   unsigned int time;
   unsigned int operation;
   char *extensionOID;
   char *bindDN;
   char *value1;
   char *value2;
   char *value3;
   char *value4;
   int resultCode;
} EVT_SysExtOpEventData;


typedef struct
{
   EVT_ConnectionEventData *connectionData;
   unsigned int msgID;
   unsigned int time;
   unsigned int operation;		
   int resultCode;
   char *matchedDN;
   char **referral;
}EVT_ResponseEventData;

typedef struct 
{
   EVT_ConnectionEventData *connectionData;
   unsigned int msgID;
   unsigned int time;
   char* bindDN;
   char* entryDN; 
   int passwordModifyType; 
   int resultCode;
}EVT_PasswordModifyEventData;   



typedef struct
{
   unsigned int time;
   char *inetAddr;
}EVT_UnknownEventData; 



#endif /*EVENTS_H*/

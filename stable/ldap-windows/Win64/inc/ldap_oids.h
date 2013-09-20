/* **************************************************************************
 * $Novell: ldap_oids.h,v 1.11 2003/05/13 13:50:57 $
 *
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
 ******************************************************************************/
#define R1 "RFC 2252: Lightweight Directory Access Protocol(v3)"
#define D1 "Internet Draft: draft-sermersheim-nds-ldap-schema-**.txt"
#define D2 "Internet Draft: draft-khan-ldapext-replica-mgmt-**.txt"
#define D3 "Internet Draft: draft-rharrision-lburp-**.txt"
#define D4 "Internet Draft: draft-ietf-ldapext-psearch-**.txt"
#define D5 "Internet Draft: draft-ietf-ldapext-ldapv3-vlv-**.txt"
#define D6 "Internet Draft: draft-zeilenga-ldap-namedref-**.txt"

#define TOTAL_LEN  143
#define ELEM       5

/* Each line in NamesAndOIDs array has five fields. The fields are:
 *     field 1: type of the line. s-syntax, e-extension, and c-control. 
 *     field 2: description of the operation represented by the oid.
 *     field 3: syntax name.
 *     field 4: oid.
 *     field 5: reference.
 */
static char *namesandoids[TOTAL_LEN][ELEM]
        = {    
        /* defined in "RFC 2252: Lightweight Directory Access Protocol (v3)" */
        {"s","ACI Item","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.1",R1},
        {"s","Access Point","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.2",R1},
        {"s","Attribute Type Description","SYN_CI_STRING","1.3.6.1.4.1.1466.115.121.1.3",R1},
        {"s","Audio","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.4",R1},
        {"s","Binary","SYN_STREAM","1.3.6.1.4.1.1466.115.121.1.5",R1},
        {"s","Bit String","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.6",R1},
        {"s","Boolean","SYN_BOOLEAN","1.3.6.1.4.1.1466.115.121.1.7",R1},
        {"s","Certificate","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.8",R1},
        {"s","Certificate List","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.9",R1},
        {"s","Certificate Pair","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.10",R1},
        {"s","Country String","SYN_CI_STRING","1.3.6.1.4.1.1466.115.121.1.11",R1},
        {"s","DN","SYN_DIST_NAME","1.3.6.1.4.1.1466.115.121.1.12",R1},
        {"s","Data Quality Syntax","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.13",R1},
        {"s","Delivery Method","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.14",R1},
        {"s","Directory String","SYN_CI_STRING","1.3.6.1.4.1.1466.115.121.1.15",R1},
        {"s","DIT Content Rule Description","SYN_CI_STRING","1.3.6.1.4.1.1466.115.121.1.16",R1},
        {"s","DIT Structure Rule Description","SYN_CI_STRING","1.3.6.1.4.1.1466.115.121.1.17",R1},
        {"s","DL Submit Permission","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.18",R1},
        {"s","DSA Quality Syntax","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.19",R1},
        {"s","DSE Type","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.20",R1},
        {"s","Enhanced Guide","SYN_INTEGER","1.3.6.1.4.1.1466.115.121.1.21",R1},
        {"s","Facsimile Telephone Number","SYN_FAX_NUMBER","1.3.6.1.4.1.1466.115.121.1.22",R1},
        {"s","Fax","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.23", R1},
        {"s","Generalized Time","SYN_TIME","1.3.6.1.4.1.1466.115.121.1.24",R1},
        {"s","Guide","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.25",R1},
        {"s","IA5 String","SYN_CE_STRING","1.3.6.1.4.1.1466.115.121.1.26",R1},
        {"s","INTEGER","SYN_INTEGER","1.3.6.1.4.1.1466.115.121.1.27",R1},
        {"s","INTEGER","SYN_INTERVAL","1.3.6.1.4.1.1466.115.121.1.27",R1},
        {"s","JPEG","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.28",R1},
        {"s","LDAP Syntax Description","SYN_CI_STRING","1.3.6.1.4.1.1466.115.121.1.54",R1},
        {"s","LDAP Schema Definition","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.56",R1},
        {"s","LDAP Schema Description","SYN_CI_STRING","1.3.6.1.4.1.1466.115.121.1.57",R1},
        {"s","Master And Shadow Access Points","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.29",R1},
        {"s","Matching Rule Description","SYN_CI_STRING","1.3.6.1.4.1.1466.115.121.1.30",R1},
        {"s","Matching Rule Use Description","SYN_CI_STRING","1.3.6.1.4.1.1466.115.121.1.31",R1},
        {"s","Mail Preference","SYN_CI_STRING","1.3.6.1.4.1.1466.115.121.1.32",R1},
        {"s","MHS OR Address","SYN_CI_STRING","1.3.6.1.4.1.1466.115.121.1.33",R1},
        {"s","Modify Rights","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.55",R1},
        {"s","Name And Optional UID","SYN_CI_STRING","1.3.6.1.4.1.1466.115.121.1.34",R1},
        {"s","Name Form Description","SYN_CI_STRING","1.3.6.1.4.1.1466.115.121.1.35",R1},
        {"s","Numeric String","SYN_CI_STRING","1.3.6.1.4.1.1466.115.121.1.36",R1},
        {"s","Object Class Description","SYN_CI_STRING","1.3.6.1.4.1.1466.115.121.1.37",R1},
        {"s","Octet String","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.40",R1},
        {"s","OID","SYN_CI_STRING","1.3.6.1.4.1.1466.115.121.1.38",R1},
        {"s","Other Mailbox","SYN_CI_STRING","1.3.6.1.4.1.1466.115.121.1.39",R1},
        {"s","Postal Address","SYN_PO_ADDRESS","1.3.6.1.4.1.1466.115.121.1.41",R1},
        {"s","Protocol Information","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.42",R1},
        {"s","Presentation Address","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.43",R1},
        {"s","Printable String","SYN_PR_STRING","1.3.6.1.4.1.1466.115.121.1.44",R1},
        {"s","Substring Assertion","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.58",R1},
        {"s","Subtree Specification","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.45",R1},
        {"s","Supplier Information","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.46",R1},
        {"s","Supplier Or Consumer","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.47",R1},
        {"s","Supplier And Consumer","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.48",R1},
        {"s","Supported Algorithm","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.49",R1},
        {"s","Telephone Number","SYN_TEL_NUMBER","1.3.6.1.4.1.1466.115.121.1.50",R1},
        {"s","Teletex Terminal Identifier","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.51",R1},
        {"s","Telex Number","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.52",R1},
        {"s","UTC Time","SYN_OCTET_STRING","1.3.6.1.4.1.1466.115.121.1.53",R1},
        /* defined in"Internet Draft: draft-sermersheim-nds-ldap-schema-**.txt" */
        {"s","Case Ignore List","SYN_CI_LIST","2.16.840.1.113719.1.1.5.1.6",D1},
        {"s","Tagged Data","SYN_NET_ADDRESS","2.16.840.1.113719.1.1.5.1.12",D1},
        {"s","Octet List","SYN_OCTET_LIST","2.16.840.1.113719.1.1.5.1.13",D1},
        {"s","Tagged String","SYN_EMAIL_ADDRESS","2.16.840.1.113719.1.1.5.1.14",D1},
        {"s","Tagged Name and String","SYN_PATH","2.16.840.1.113719.1.1.5.1.15",D1},
        {"s","NDS Replica Pointer","SYN_REPLICA_POINTER","2.16.840.1.113719.1.1.5.1.16",D1},
        {"s","ACL","SYN_OBJECT_ACL","2.16.840.1.113719.1.1.5.1.17",D1},
        {"s","NDS Timestamp","SYN_TIMESTAMP","2.16.840.1.113719.1.1.5.1.19",D1},
        {"s","Counter","SYN_COUNTER","2.16.840.1.113719.1.1.5.1.22",D1},
        {"s","Tagged Name","SYN_BACK_LINK","2.16.840.1.113719.1.1.5.1.23",D1},
        {"s","Typed Name","SYN_TYPED_NAME","2.16.840.1.113719.1.1.5.1.25",D1},
        /* defined in"Internet Draft: draft-khan-ldapext-replica-mgmt-**.txt" */
        {"e","ndsToLdapResponse","","2.16.840.1.113719.1.27.100.1",D2},
        {"e","ndsToLdapRequest","","2.16.840.1.113719.1.27.100.2",D2},
        {"e","Split Partition Request","","2.16.840.1.113719.1.27.100.3",D2},
        {"e","Split Partition Response","","2.16.840.1.113719.1.27.100.4",D2},
        {"e","Merge Partition Request","","2.16.840.1.113719.1.27.100.5",D2},
        {"e","Merge Partition Response","","2.16.840.1.113719.1.27.100.6",D2},
        {"e","Add Replica Request","","2.16.840.1.113719.1.27.100.7",D2},
        {"e","Add Replica Response","","2.16.840.1.113719.1.27.100.8",D2},
        {"e","Refresh Server Request","","2.16.840.1.113719.1.27.100.9",D2},
        {"e","Refresh Server Response","","2.16.840.1.113719.1.27.100.10",D2},
        {"e","Delete Replica Request","","2.16.840.1.113719.1.27.100.11",D2},
        {"e","Delete Replica Response","","2.16.840.1.113719.1.27.100.12",D2},
        {"e","Partition Entry Count Request","","2.16.840.1.113719.1.27.100.13",D2},
        {"e","Partition Entry Count Response","","2.16.840.1.113719.1.27.100.14",D2},
        {"e","Change Replica Type Request","","2.16.840.1.113719.1.27.100.15",D2},
        {"e","Change Replica Type Response","","2.16.840.1.113719.1.27.100.16",D2},
        {"e","Get Replica Info Request","","2.16.840.1.113719.1.27.100.17",D2},
        {"e","Get Replica Info Response","","2.16.840.1.113719.1.27.100.18",D2},
        {"e","List Replicas Request","","2.16.840.1.113719.1.27.100.19",D2},
        {"e","List Replicas Response","","2.16.840.1.113719.1.27.100.20",D2},
        {"e","Receive All Updates Request","","2.16.840.1.113719.1.27.100.21",D2},
        {"e","Receive All Updates Response","","2.16.840.1.113719.1.27.100.22",D2},
        {"e","Send All Updates Request","","2.16.840.1.113719.1.27.100.23",D2},
        {"e","Send All Updates Response","","2.16.840.1.113719.1.27.100.24",D2},
        {"e","Partition Sync Request","","2.16.840.1.113719.1.27.100.25",D2},
        {"e","Partition Sync Response","","2.16.840.1.113719.1.27.100.26",D2},
        {"e","Schema Sync Request","","2.16.840.1.113719.1.27.100.27",D2},
        {"e","Schema Sync Response","","2.16.840.1.113719.1.27.100.28",D2},
        {"e","Abort Partition Operation Request","","2.16.840.1.113719.1.27.100.29",D2},
        {"e","Abort Partition Operation Response","","2.16.840.1.113719.1.27.100.30",D2},
        {"e","Get Bind DN Request","","2.16.840.1.113719.1.27.100.31",D2},
        {"e","Get Bind DN Response","","2.16.840.1.113719.1.27.100.32",D2},
        {"e","Get Effective Privileges Request","","2.16.840.1.113719.1.27.100.33",D2},
        {"e","Get Effective Privileges Response","","2.16.840.1.113719.1.27.100.34",D2},
        {"e","Set Replication Filter Request","","2.16.840.1.113719.1.27.100.35",D2},
        {"e","Set Replication Filter Response","","2.16.840.1.113719.1.27.100.36",D2},
        {"e","Get Replication Filter Request","","2.16.840.1.113719.1.27.100.37",D2},
        {"e","Get Replication Filter Response","","2.16.840.1.113719.1.27.100.38",D2},
        {"e","Create Orphan Partition Request","","2.16.840.1.113719.1.27.100.39",D2},
        {"e","Create Orphan Partition Response","","2.16.840.1.113719.1.27.100.40",D2},
        {"e","Remove Orphan Partition Request","","2.16.840.1.113719.1.27.100.41",D2},
        {"e","Remove Orphan Partition Response","","2.16.840.1.113719.1.27.100.42",D2},
        {"e","Trigger Back Linker Request","","2.16.840.1.113719.1.27.100.43",D2},
        {"e","Trigger Back Linker Response","","2.16.840.1.113719.1.27.100.44",D2},
        /* the following two extensions are not yet documentated */
        {"e","Trigger DRL process Request","","2.16.840.1.113719.1.27.100.45",""},
        {"e","Trigger DRL process Response","","2.16.840.1.113719.1.27.100.46",""},
        {"e","Trigger Janitor Request","","2.16.840.1.113719.1.27.100.47",D2},
        {"e","Trigger Janitor Response","","2.16.840.1.113719.1.27.100.48",D2},
        {"e","Trigger Limber Request","","2.16.840.1.113719.1.27.100.49",D2},
        {"e","Trigger Limber Response","","2.16.840.1.113719.1.27.100.50",D2},
        {"e","Trigger Skulker Request","","2.16.840.1.113719.1.27.100.51",D2},
        {"e","Trigger Skulker Response","","2.16.840.1.113719.1.27.100.52",D2},
        {"e","Trigger Schema Sync Request","","2.16.840.1.113719.1.27.100.53",D2},
        {"e","Trigger Schema Sync Response","","2.16.840.1.113719.1.27.100.54",D2},
        {"e","Trigger Partition Purge Request","","2.16.840.1.113719.1.27.100.55",D2},
        {"e","Trigger Partition Purge Response","","2.16.840.1.113719.1.27.100.56",D2},
        /* defined in"Internet Draft: draft-rharrision-lburp-**.txt" */
        {"e","Start LBURP Request","","2.16.840.1.113719.1.142.100.1",D3},
        {"e","Start LBURP Response","","2.16.840.1.113719.1.142.100.2",D3},
        {"e","End LBURP Request","","2.16.840.1.113719.1.142.100.4",D3},
        {"e","End LBURP Response","","2.16.840.1.113719.1.142.100.5",D3},
        {"e","LBURP Operation","","2.16.840.1.113719.1.142.100.6", D3},
        {"e","LBURP Operation Done","","2.16.840.1.113719.1.142.100.7",D3},        
        /* OIDS for the Event Monitoring extension */
		{"e","Monitor Events Request","","2.16.840.1.113719.1.27.100.79",""},
		{"e","Monitor Events Response","","2.16.840.1.113719.1.27.100.80",""},
		{"e","Event Notification Intermediate Response","","2.16.840.1.113719.1.27.100.81",""},
		{"e","Filtered Monitor Events Request","","2.16.840.1.113719.1.27.100.84",""},
        /* the following two controls are not yet documentated */ 
        {"c","Simple Password","","2.16.840.1.113719.1.27.101.5",""},
        {"c","Forward Reference","","2.16.840.1.113719.1.27.101.6",""},
        /* defined in"Internet Draft: draft-ietf-ldapext-psearch-**.txt" */
        {"c","Persistent Search","","2.16.840.1.113730.3.4.3", D4},
        {"c","Entry Change Notification","","2.16.840.1.113730.3.4.7",D4},
        /* defined in"Internet Draft: draft-ietf-ldapext-ldapv3-vlv-**.txt */
        {"c","VLV Control Request","","2.16.840.1.113730.3.4.9",D5},
        {"c","VLV Control Response","","2.16.840.1.113730.3.4.10", D5},
        /* defined in"Internet Draft: draft-zeilenga-ldap-namedref-**.txt */
        {"c","ManageDsaIT Request","","2.16.840.1.113730.3.2.6",D6}};


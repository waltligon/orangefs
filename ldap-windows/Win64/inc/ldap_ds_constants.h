/* **************************************************************************
 * $Novell: ldap_ds_constants.h,v 1.3 2003/05/13 13:48:14 $
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

/* ldap_ds_constants.h contains bit values for [Entry Rights], [All Attributes
 * Rights], attribute rights and entry flags in Novell eDirectory.
 */

/******************************************************************************
 * bit values for [Entry Rights] of access control in Novell eDirecroty 
 *****************************************************************************/ 

/* Allows a trustee to discover objects in the Novell eDirecroty tree.
 */ 
#define LDAP_DS_ENTRY_BROWSE      0x00000001L

/* Allows a trustee to create child objects (new objects that are subordinate
 * to the object in the Novell eDirectory tree.)
 */
#define LDAP_DS_ENTRY_ADD         0x00000002L

/* Allows a trustee to delete an object. This right does not allow a trustee
 * to delete a container object that has subordinate objects.
 */
#define LDAP_DS_ENTRY_DELETE      0x00000004L

/* Allows a trustee to rename the object.
 */
#define LDAP_DS_ENTRY_RENAME      0x00000008L

/* Gives a trustee all rights to an object and its attributes.
 */
#define LDAP_DS_ENTRY_SUPERVISOR  0x00000010L

/* Allows a trustee to inherit the rights granted in the ACL
 * and exercise them on subordinate objects.
 */
#define LDAP_DS_ENTRY_INHERIT_CTL 0x00000040L


/******************************************************************************
 * bit values for [All Attributes Rights] and attribute rights of access 
 * control in Novell eDirecroty
 *****************************************************************************/
/* Allows a trustee to compare a value with an attribute's value. This allows 
 * the trustee to see if the attribute contains the value without having 
 * rights to see the value.
 */
#define LDAP_DS_ATTR_COMPARE      0x00000001L

/* Allows a trustee to read an attribute value. This right confers
 * the Compare right.
 */
#define LDAP_DS_ATTR_READ         0x00000002L

/* Allows a trustee to add, delete, or modify an attribute value. This right
 * also gives the trustee the Self (Add or Delete Self) right.
 */
#define LDAP_DS_ATTR_WRITE        0x00000004L

/* Allows a trustee to add or delete its name as an attribute value on those
 * attributes that take object names as their values.
 */
#define LDAP_DS_ATTR_SELF         0x00000008L

/* Gives a trustee all rights to the object's attributes.
 */
#define LDAP_DS_ATTR_SUPERVISOR   0x00000020L

/* Allows a trustee to inherit the rights granted in the ACL and exercise
 * these attribute rights on subordinate objects.
 */
#define LDAP_DS_ATTR_INHERIT_CTL  0x00000040L

/* This bit will be set if the trustee in the ACL is a dynamic group 
 * and its dynamic members should be considered for ACL rights 
 * calculation purposes. If this bit is reset, the trustee's static 
 * members alone will be considered for rights calculation purposes.
 */
#define LDAP_DS_DYNAMIC_ACL       0x20000000L


/******************************************************************************
 * bit values of entry flags in Novell eDirecroty
 *****************************************************************************/
/* Indicates that the entry is an alias object.
 */
#define LDAP_DS_ALIAS_ENTRY            0x0001

/* Indicates that the entry is the root partition.
 */
#define LDAP_DS_PARTITION_ROOT         0x0002

/* Indicates that the entry is a container object and not a container alias.
 */
#define LDAP_DS_CONTAINER_ENTRY        0x0004

/* Indicates that the entry is a container alias.
 */
#define LDAP_DS_CONTAINER_ALIAS        0x0008

/* Indicates that the entry matches the List filter.
 */
#define LDAP_DS_MATCHES_LIST_FILTER    0x0010
       
/* Indicates that the entry has been created as a reference rather than an 
 * entry. The synchronization process is still running and has not created 
 * an entry for the object on this replica. 
 */
#define LDAP_DS_REFERENCE_ENTRY        0x0020

/* Indicates that the entry is a reference rather than the object. The 
 * reference is in the older 4.0x form and appears only when upgrading.
 */
#define LDAP_DS_40X_REFERENCE_ENTRY    0x0040

/* Indicates that the entry is being back linked.
 */
#define LDAP_DS_BACKLINKED             0x0080

/* Indicates that the entry is new and replicas are still being updated.
 */
#define LDAP_DS_NEW_ENTRY              0x0100

/* Indicates that an external reference has been temporarily created for
 * authentication; when the object logs out, the temporary reference is deleted.
 */
#define LDAP_DS_TEMPORARY_REFERENCE    0x0200

/* Indicates that the entry is being audited.
 */
#define LDAP_DS_AUDITED                0x0400

/* Indicates that the state of the entry is not present.
 */
#define LDAP_DS_ENTRY_NOT_PRESENT      0x0800

/* Indicates the entry's creation timestamp needs to be verified. Novell 
 * eDirectory sets this flag when a replica is removed or upgraded from 
 * NetWare 4.11 to NetWare 5.
 */
#define LDAP_DS_ENTRY_VERIFY_CTS       0x1000

/* Indicates that the entry's information does not conform to the standard 
 * format and is therefore damaged.
 */
#define LDAP_DS_ENTRY_DAMAGED          0x2000


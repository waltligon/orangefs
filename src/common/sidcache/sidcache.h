
#include <db.h>
#include <uuid/uuid.h>
#include <policy.h>

/* these are defines just to temporarily allow compile */
typedef int BMI_addr;
#define PVFS_LAYOUT_ROUND_ROBIN 0
#define PVFS_LAYOUT_RANDOM 1
/* remove when integrating into main orange code */

/* main SID cache database */
extern DB     *SID_db;

/* main SID transaction */
extern DB_TXN *SID_txn;

/* attribute secondary DBs */
extern DB     *SID_attr_index[SID_NUM_ATTR]; 

/* cursor for each secondary DB */
extern DBC    *SID_attr_cursor[SID_NUM_ATTR];

/* type of a SID */
typedef uuid_t SID;


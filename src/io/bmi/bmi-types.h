/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* Header file for types and other defines used throughout the BMI.
 */

#ifndef __BMI_TYPES_H
#define __BMI_TYPES_H

#include <stdlib.h>
#include "id-generator.h"
#include "pvfs2-debug.h"
#include "pvfs2-types.h"

/* PVFS type mapping */
typedef PVFS_flag bmi_flag_t;	/* generic flag */
typedef PVFS_size bmi_size_t;	/* data region size */
typedef PVFS_msg_tag_t bmi_msg_tag_t;	/* message tag */
typedef id_gen_t bmi_op_id_t;	/* network operation handle */
typedef id_gen_t bmi_addr_t;	/* network address handle */

/* TODO: not using a real type for this yet; need to specify what
 * error codes look like */
typedef int32_t bmi_error_code_t;	/* error code information */

/* BMI method initialization flags */
enum
{
    BMI_INIT_SERVER = 1
};

/* BMI memory buffer flags */
enum
{
    BMI_SEND_BUFFER = 1,
    BMI_RECV_BUFFER = 2,
    BMI_PRE_ALLOC = 4,
    BMI_EXT_ALLOC = 8
};

/* BMI get_info and set_info options */
enum
{
    BMI_DROP_ADDR = 1,
    BMI_CHECK_INIT = 2,
    BMI_CHECK_MAXSIZE = 3
};

#endif /* __BMI_TYPES_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sw=4 noexpandtab
 */

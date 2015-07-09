/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

#ifndef __BMI_METHOD_CALLBACK_H
#define __BMI_METHOD_CALLBACK_H

#include "pvfs2-internal.h"
#include "bmi-method-support.h"

BMI_addr_t bmi_method_addr_reg_callback(bmi_method_addr_p map);
int bmi_method_addr_forget_callback(BMI_addr_t addr);
void bmi_method_addr_drop_callback(char *method_name);
int bmi_fill_cq_callback(bmi_context_id context_id,
                         method_op_p completed_op);

#endif /* __BMI_METHOD_CALLBACK_H */

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

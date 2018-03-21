
/*
 * (C) 2017 Clemson University and Omnibond, Inc.
 *
 * See COPYING in top-level directory.
 */

/** \file
 *  \ingroup policy
 *
 *  Parser for policy source-to-source translator.
 */

#ifndef CODEGEN_H
#define CODEGEN_H
void gen_initialize(void);
void gen_finalize(void);
void gen_attrib_table(void);
void gen_policy_table(void);
void gen_save_attr_name(char *name);
void gen_begin_enum_decl(void);
void gen_end_enum_decl(void);
void gen_int_decl(int low, int high);
void gen_value(char *value);
void gen_value_comma(char *value);
void gen_init_join_criteria(void);
void gen_end_join_criteria(void);
void gen_init_set_criteria(void);
void gen_inc_pnum(void);
void gen_end_set_criteria(void);
void gen_jc_separator(void);
void gen_output_join_criteria(char *attr, char *value);
void gen_spread_attr(char *attr);
void gen_set_separator(void);
void gen_set_criteria_count(int count);
void gen_start_scfunc(void);
void gen_end_scfunc(void);
void gen_or(void);
void gen_and(void);
void gen_lp(void);
void gen_rp(void);
void gen_compare(char *attr, int cmpop, char *value);
#endif

/*
 * Local variables:
 *    c-indent-level: 4
 *    c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../quicklist/quicklist.h"
#include "../quickhash/quickhash.h"

#include "policyeval.h"
#include "policycomp.h"

#define NAME_BUF_SZ 50

static QLIST_HEAD(attr_list);
static QLIST_HEAD(policy_list);

static char *attr_name;
static int pnum = 0;

void gen_initialize()
{
}

void gen_finalize()
{
}

void gen_attrib_table()
{
    int i = 0;
    list_item_t *attr;
    fprintf(code, "int (* SID_extract_key[])(DB *pri, "
            "const DBT *pkey, const DBT *pdata, DBT *skey) =\n{\n");
    fprintf(header, "typedef enum SID_attr_e\n{\n");
    qlist_for_each_entry(attr, &attr_list, link)
    {
        if (i != 0)
        {
            fprintf(code, ",\n");
            fprintf(header, ",\n");
        }
        fprintf(code, "    SID_get_%s", attr->name);
        fprintf(header, "    SID_%s = %d", attr->name, i++);
    }
    fprintf(code, "\n}\n\n");
    fprintf(header, "\n} SID_attr_t;\n\n");
    fprintf(header, "#define SID_NUM_ATTR %d\n\n", i);
}

void gen_policy_table()
{
    int i = 0;
    list_item_t *policy;
    fprintf(code, "SID_policy_t SID_policies[] =\n{{\n");
    qlist_for_each_entry(policy, &policy_list, link)
    {
        if (i != 0)
        {
            fprintf(code, "},{\n");
        }
        fprintf(code, "    .join_count = sizeof(SID_jc%d)"
                "/sizeof(SID_join_criteria_t),\n", i);
        fprintf(code, "    .jc = SID_jc%d,\n", i);
        if (policy->name == NULL)
        {
            fprintf(code, "    .spread_attr = NULL,\n");
        }
        else
        {
            fprintf(code, "    .spread_attr = SID_%s,\n", policy->name);
        }
        fprintf(code, "    .rule_count = sizeof(SID_sc%d)"
                "/sizeof(SID_set_criteria_t),\n", i);
        fprintf(code, "    .sc = SID_sc%d\n", i);
        i++;
    }
    fprintf(code, "}};\n\n");
}

void gen_save_attr_name(char *name)
{
    list_item_t *new;
    new = (list_item_t *)malloc(sizeof(list_item_t));
    new->name = estrdup(name);
    attr_name = new->name;
    /* stash attr name in attr list */
    qlist_add_tail(&new->link, &attr_list);
    /* generate extractor function */
    fprintf(code, "int SID_get_%s (DB *pri, const DBT *pkey, "
            "const DBT *pdata, DBT *skey)\n{\n    "
            "SID_get_attr (pri, pkey, pdata, skey, SID_%s);\n}\n\n",
            name, name);
}

void gen_begin_enum_decl()
{
    fprintf(header, "typedef enum %s_e\n{\n", attr_name);
}

void gen_end_enum_decl()
{
    fprintf(header, "} %s_t;\n\n", attr_name);
}

void gen_int_decl(int low, int high)
{
    /* eventually store low and high in symbol table */
    fprintf(header, "typedef int %s_t;\n\n", attr_name);
}

void gen_value(char *value)
{
    fprintf(header, "    SID_%s_%s\n", attr_name, value);
}

void gen_value_comma(char *value)
{
    fprintf(header, "    SID_%s_%s,\n", attr_name, value);
}

void gen_init_join_criteria()
{
    fprintf(code, "SID_join_criteria_t SID_jc%d[] =\n{{\n", pnum);
}

void gen_end_join_criteria()
{
    fprintf(code, "}};\n\n");
}

void gen_init_set_criteria()
{
    fprintf(code, "SID_set_criteria_t SID_sc%d[] =\n{{\n", pnum);
}

void gen_inc_pnum()
{
    pnum++;
}

void gen_end_set_criteria()
{
    fprintf(code, "}};\n\n");
}

void gen_jc_separator()
{
    fprintf(code, "},{\n");
}

void gen_output_join_criteria(char *attr, char *value)
{
    fprintf(code, "    .attr = SID_%s,\n    .value = SID_%s_%s\n",
            attr, attr, value);
}

void gen_spread_attr(char *attr)
{
    list_item_t *new;
    new = (list_item_t *)malloc(sizeof(list_item_t));
    new->name = estrdup(attr);
    attr_name = new->name;
    /* stash attr name in attr list */
    qlist_add_tail(&new->link, &policy_list);
}

void gen_set_separator()
{
    fprintf(code, "},{\n");
}

void gen_set_criteria_count(int count)
{
    fprintf(code, "    .count = 0,\n", count);
    fprintf(code, "    .count_max = %d,\n", count);
}

void gen_start_scfunc()
{
    static int funcno = 0;
    int newfunc = funcno++;
    fprintf(code2, "int SID_scfunc%d(DBT *DBval){\n\treturn\n", newfunc);
    fprintf(code, "    .scfunc = SID_scfunc%d\n", newfunc);
    fprintf(header, "int SID_scfunc%d(DBT *DBval);\n\n", newfunc);
}

void gen_end_scfunc()
{
    fprintf(code2, ";}\n\n");
}

void gen_or()
{
    fprintf(code2, " || ");
}

void gen_and()
{
    fprintf(code2, " && ");
}

void gen_lp()
{
    fprintf(code2, " ( ");
}

void gen_rp()
{
    fprintf(code2, " ) ");
}

void gen_compare(char *attr, int cmpop, char *value)
{
    long v;
    char *ep;
    fprintf(code2, "\t\tSID_ATTR(SID_%s) ", attr);
    switch (cmpop)
    {
        case SID_EQ :
            fprintf(code2, "==");
            break;
        case SID_NE :
            fprintf(code2, "!=");
            break;
        case SID_GT :
            fprintf(code2, ">");
            break;
        case SID_GE :
            fprintf(code2, ">=");
            break;
        case SID_LT :
            fprintf(code2, "<");
            break;
        case SID_LE :
            fprintf(code2, "<=");
            break;
    }
    v = strtol(value, &ep, 10);
    if (*ep == 0)
    {
        /* valid integer */
        fprintf(code2, " %d\n", v);
    }
    else
    {
        /* monething non integer was found */
        fprintf(code2, " SID_%s_%s\n", attr, value);
    }
}


